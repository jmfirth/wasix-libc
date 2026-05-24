#include "pthread_impl.h"

/* This lock primitive combines a flag (in the sign bit) and a
 * congestion count (= threads inside the critical section, CS) in a
 * single int that is accessed through atomic operations. The states
 * of the int for value x are:
 *
 * x == 0: unlocked and no thread inside the critical section
 *
 * x < 0: locked with a congestion of x-INT_MIN, including the thread
 * that holds the lock
 *
 * x > 0: unlocked with a congestion of x
 *
 * or in an equivalent formulation x is the congestion count or'ed
 * with INT_MIN as a lock flag.
 */

#ifndef __wasilibc_unmodified_upstream
/* firebox#456 Phase 9 — register/deregister the lock-word address in
 * the calling thread's held-lock array. Used by the __pthread_exit
 * sweep-wake fix; see pthread_impl.h struct pthread for the full
 * rationale.
 *
 * register_held_lock is called from __lock once the CAS confirms we
 * own the lock; deregister_held_lock is called from __unlock as the
 * first step of release (BEFORE the atomic that announces the
 * release-to-waiters), so a concurrent __pthread_exit sweep on this
 * thread cannot observe the lock as on-our-held-list AFTER it has
 * been released to other threads.
 *
 * Both helpers are TLS-safe (they operate on fields of the pthread
 * struct reached via __pthread_self() — heap-resident, set up by
 * wasi_thread_start.s's __wasm_init_tls before any C code runs) and
 * branch-free in the steady-state common case (single store, no
 * search beyond the slot just set).
 *
 * Deregister searches from the most-recently-added slot first (LIFO
 * is the empirically-dominant lock/unlock nesting pattern in musl, so
 * the match is usually at slot count-1). On match we swap-with-end
 * and decrement count — the array semantics are an unordered set,
 * not a stack, so the swap is safe. */
/* firebox#473 (2026-05-23): exposed as hidden symbols (was: static) so
 * the file-lock primitive in stdio/__lockfile.c can use the same
 * per-thread held-lock array for orphan-protection. The lock-word
 * shape and the sweep-wake invariant are identical between the
 * __lock/__unlock primitive and __lockfile/__unlockfile — both store
 * an atomic int that 'locked' callers CAS-acquire and 'unlocking'
 * callers a_swap-release, and both are followed by an optional
 * __wake. Sharing the held-list means Phase 9's __pthread_exit sweep
 * transparently covers file locks too, with no struct-pthread change
 * and no second sweep helper. See work/tasks/473-*. */
hidden void __firebox_register_held_lock(volatile int *l)
{
	pthread_t self = __pthread_self();
	unsigned n = self->firebox_held_locks_count;
	if (n >= __FIREBOX_HELD_LOCKS_CAP) {
		self->firebox_held_locks_overflow++;
		return;
	}
	self->firebox_held_locks[n] = l;
	self->firebox_held_locks_count = n + 1;
}

hidden void __firebox_deregister_held_lock(volatile int *l)
{
	pthread_t self = __pthread_self();
	unsigned n = self->firebox_held_locks_count;
	for (unsigned i = n; i-- > 0; ) {
		if (self->firebox_held_locks[i] == l) {
			self->firebox_held_locks[i] = self->firebox_held_locks[n - 1];
			self->firebox_held_locks[n - 1] = 0;
			self->firebox_held_locks_count = n - 1;
			return;
		}
	}
	/* Not found: an __unlock(file) without a matching __lock(file) on
	 * this thread. This happens normally during the asyncify-escape
	 * teardown path (the very class of bug this fix exists to mitigate)
	 * — the rewound continuation can call __unlock without having
	 * called the matching __lock on the same logical control-flow path.
	 * Silently ignore; the sweep-wake at __pthread_exit will close any
	 * orphans. */
}

/* firebox#470/#472 — targeted per-export-boundary sweep-wake helper.
 *
 * Phase 9 sweeps the entire held-lock array at __pthread_exit, which
 * covers the worker-thread teardown class. But several lock-using
 * helpers in libc (__funcs_on_exit, __fork_handler) walk a handler
 * list with LOCK/UNLOCK gated around each callback invocation, and the
 * Binaryen-asyncify rewind can land the post-callback continuation
 * past the matching UNLOCK without ever reaching __pthread_exit. The
 * orphan persists; any later call to that same function blocks
 * forever on the lock word.
 *
 * __firebox_lock_sweep_wake_one(l) is meant to be called at the export
 * boundary of those helpers (after the legitimate UNLOCK position):
 *
 *   - Healthy path: the prior UNLOCK already deregistered `l` from
 *     this thread's held-list and cleared the lock word. The scan
 *     finds nothing; helper is a no-op.
 *
 *   - Asyncify-escape path: `l` is still on this thread's held-list
 *     because the UNLOCK was skipped. The helper:
 *       1. Removes `l` from the held-list (so a later __pthread_exit
 *          sweep doesn't double-wake).
 *       2. Atomically forces the lock word to 0 (a_swap), so future
 *          acquirers see an unlocked state.
 *       3. Wakes every waiter (__wake(l, -1, 1) — INT_MAX cnt, priv=1
 *          matching __unlock's wake call). They re-evaluate
 *          __futexwait's predicate against the now-0 lock word and
 *          proceed past the wait.
 *
 * Force-clearing the lock word from outside the proper __unlock path
 * is safe in this regime: the asyncify rewind has logically advanced
 * past the UNLOCK position (the function "returned" successfully); the
 * skipped atomic is exactly what we are restoring here. The thread no
 * longer believes it owns the lock — its held-list entry is the only
 * remaining record, which we are also clearing.
 *
 * This helper is hidden + exported (linked as a normal symbol, not
 * static) so it can be called from atexit.c, fork.c, and any future
 * caller in the class. See work/tasks/470-*, work/tasks/472-*. */
hidden void __firebox_lock_sweep_wake_one(volatile int *l)
{
	pthread_t self = __pthread_self();
	unsigned n = self->firebox_held_locks_count;
	for (unsigned i = n; i-- > 0; ) {
		if (self->firebox_held_locks[i] == l) {
			/* Remove from held-list first so any concurrent sweep
			 * (e.g. __pthread_exit's bulk walk) doesn't see this slot
			 * after we've fired the wake below. Same ordering rationale
			 * as __firebox_deregister_held_lock above. */
			self->firebox_held_locks[i] = self->firebox_held_locks[n - 1];
			self->firebox_held_locks[n - 1] = 0;
			self->firebox_held_locks_count = n - 1;
			/* Force-clear the lock word so waiters' __futexwait
			 * predicate (`l[0] == expected`) flips to false and they
			 * proceed past the wait when woken below. Without this,
			 * the wake is wasted: waiters re-check, see the lock-word
			 * still in the locked-contended state the asyncify-rewound
			 * continuation left behind, and re-sleep. */
			a_swap(l, 0);
			__wake(l, -1, 1);
			return;
		}
	}
	/* Not on the held-list: nothing to sweep. Either the legitimate
	 * UNLOCK already ran (healthy path — the common case), or we never
	 * acquired the lock in this dynamic extent in the first place. */
}
#endif

void __lock(volatile int *l)
{
	int need_locks = libc.need_locks;
	if (!need_locks) return;
	/* fast path: INT_MIN for the lock, +1 for the congestion */
	int current = a_cas(l, 0, INT_MIN + 1);
	if (need_locks < 0) libc.need_locks = 0;
#ifndef __wasilibc_unmodified_upstream
	if (!current) {
		__firebox_register_held_lock(l);
		/* firebox#489 Phase 4a: probe fast-path acquire (kind=1 LOCK) */
		__firebox_trace_lock_acquire((uintptr_t)l, 1u);
		return;
	}
#else
	if (!current) return;
#endif
	/* A first spin loop, for medium congestion. */
	for (unsigned i = 0; i < 10; ++i) {
		if (current < 0) current -= INT_MIN + 1;
		// assertion: current >= 0
		int val = a_cas(l, current, INT_MIN + (current + 1));
#ifndef __wasilibc_unmodified_upstream
		if (val == current) {
			__firebox_register_held_lock(l);
			/* firebox#489 Phase 4a: probe spin-loop acquire (kind=1 LOCK) */
			__firebox_trace_lock_acquire((uintptr_t)l, 1u);
			return;
		}
#else
		if (val == current) return;
#endif
		current = val;
	}
	// Spinning failed, so mark ourselves as being inside the CS.
	current = a_fetch_add(l, 1) + 1;
	/* The main lock acquisition loop for heavy congestion. The only
	 * change to the value performed inside that loop is a successful
	 * lock via the CAS that acquires the lock. */
	for (;;) {
		/* We can only go into wait, if we know that somebody holds the
		 * lock and will eventually wake us up, again. */
		if (current < 0) {
#ifndef __wasilibc_unmodified_upstream
			/* firebox#489 Phase 4a: probe contended wait (kind=1 LOCK) */
			__firebox_trace_lock_contended((uintptr_t)l, 1u);
#endif
			__futexwait(l, current, 1);
			current -= INT_MIN + 1;
		}
		/* assertion: current > 0, the count includes us already. */
		int val = a_cas(l, current, INT_MIN + current);
#ifndef __wasilibc_unmodified_upstream
		if (val == current) {
			__firebox_register_held_lock(l);
			/* firebox#489 Phase 4a: probe contended-loop acquire (kind=1 LOCK) */
			__firebox_trace_lock_acquire((uintptr_t)l, 1u);
			return;
		}
#else
		if (val == current) return;
#endif
		current = val;
	}
}

void __unlock(volatile int *l)
{
	/* Check l[0] to see if we are multi-threaded. */
	if (l[0] < 0) {
#ifndef __wasilibc_unmodified_upstream
		/* firebox#456 Phase 9: deregister BEFORE the release-to-waiters
		 * atomic. If we deregistered after, a __pthread_exit racing
		 * with us could sweep this address (which we genuinely no
		 * longer hold) and emit a spurious __wake — harmless but
		 * wasteful. Doing it first means the sweep only ever wakes
		 * addresses we provably still hold. */
		__firebox_deregister_held_lock(l);
		/* firebox#489 Phase 4a: probe release (kind=1 LOCK).  Emitted
		 * BEFORE the atomic release so the host trace orders this
		 * event before any __wake on the same lock — matches the
		 * "ordered before release-to-waiters" invariant the held-list
		 * deregister already relies on. */
		__firebox_trace_lock_release((uintptr_t)l, 1u);
#endif
		if (a_fetch_add(l, -(INT_MIN + 1)) != (INT_MIN + 1)) {
			__wake(l, 1, 1);
		}
	}
}
