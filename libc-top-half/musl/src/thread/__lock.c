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
static void register_held_lock(volatile int *l)
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

static void deregister_held_lock(volatile int *l)
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
	/* Not found: a __unlock without a matching __lock on this thread.
	 * This happens normally during the asyncify-escape teardown path
	 * (the very class of bug this fix exists to mitigate) — the rewound
	 * continuation can call __unlock without having called the matching
	 * __lock on the same logical control-flow path. Silently ignore;
	 * the sweep-wake at __pthread_exit will close any orphans. */
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
	if (!current) { register_held_lock(l); return; }
#else
	if (!current) return;
#endif
	/* A first spin loop, for medium congestion. */
	for (unsigned i = 0; i < 10; ++i) {
		if (current < 0) current -= INT_MIN + 1;
		// assertion: current >= 0
		int val = a_cas(l, current, INT_MIN + (current + 1));
#ifndef __wasilibc_unmodified_upstream
		if (val == current) { register_held_lock(l); return; }
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
			__futexwait(l, current, 1);
			current -= INT_MIN + 1;
		}
		/* assertion: current > 0, the count includes us already. */
		int val = a_cas(l, current, INT_MIN + current);
#ifndef __wasilibc_unmodified_upstream
		if (val == current) { register_held_lock(l); return; }
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
		deregister_held_lock(l);
#endif
		if (a_fetch_add(l, -(INT_MIN + 1)) != (INT_MIN + 1)) {
			__wake(l, 1, 1);
		}
	}
}
