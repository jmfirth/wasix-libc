#include "pthread_impl.h"

#ifndef __wasilibc_unmodified_upstream
#include <common/clock.h>
#include <wasi/api.h>
#include <stdint.h>
#include <stddef.h>
#endif

#ifndef __wasilibc_unmodified_upstream
/* firebox#529 — pthread_cond_wait return-path instrumentation.
 *
 * Predecessor: Edge.js cascade-9 cycle-6 (#527) instrumented libuv's
 * abort() call sites and pinned the wedge to
 *   libuv:unix/thread.c:940:cond_wait_fail
 * — i.e. `uv_cond_wait()` calls `pthread_cond_wait(cond, mutex)` and
 * receives a non-zero return, which libuv (correctly per POSIX) treats
 * as a contract violation and aborts on. libuv's usage is textbook-
 * correct (the worker holds the global mutex per src/threadpool.c:77);
 * the wedge is in the cond_wait implementation beneath.
 *
 * `pthread_cond_wait(c, m)` is a one-liner that tail-calls
 * `pthread_cond_timedwait(c, m, 0)` (libc-top-half/musl/src/thread/
 * pthread_cond_wait.c), so instrumenting THIS function with ts==NULL
 * covers both entry points.
 *
 * Non-zero return paths in __pthread_cond_timedwait:
 *   - EPERM  (stage=1): line 120, ERRORCHECK/RECURSIVE mutex not owned
 *                       by calling thread.
 *   - EINVAL (stage=2): line 123, ts->tv_nsec out of range (cannot fire
 *                       for pthread_cond_wait — ts==NULL).
 *   - e from inner do-while loop (stage=3): __timedwait_cp result.
 *                       Per __timedwait.c only 0/EINTR/ETIMEDOUT/
 *                       ECANCELED can surface; ECANCELED can leak out
 *                       via the masked cancel-state path.
 *   - tmp from pthread_mutex_lock(m) at relock (stage=4): overrides
 *                       e on the relock path. This is the most
 *                       contended candidate per POSIX — mutex
 *                       ownership tracking under contention.
 *
 * Emission shape mirrors firebox_526_abort_trace (libc-top-half/musl/
 * src/exit/abort.c) per #526's emission discipline:
 *   - raw __wasi_fd_write (no stdio — stdio mutex may be the wedge actor)
 *   - hand-rolled itoa (no snprintf — locale lock + reentrancy)
 *   - stack buffer (no malloc — heap may be wedged)
 * The one syscall per emit is intentional for atomicity wrt other
 * threads' stderr writes.
 *
 * Retirement: when #529's RCA closes cascade-9 at the actual layer
 * (tracked in docs/reference/forks.md §5 retirement registry). The
 * stamping site is removed in lockstep.
 */
static __attribute__((noinline,used))
void firebox_529_cond_wait_trace(int stage, int rc) {
	/* Buffer sized for the prefix + " stage=NN rc=NNNNN\n" + slack. */
	char buf[64];
	size_t off = 0;
	static const char prefix[] = "FIREBOX_529_COND_WAIT_RETURN stage=";
	for (size_t i = 0; i < sizeof(prefix) - 1; ++i) {
		buf[off++] = prefix[i];
	}
	/* Async-signal-safe single-digit itoa for stage (1..4). */
	if (stage < 0) { buf[off++] = '-'; stage = -stage; }
	if (stage == 0) {
		buf[off++] = '0';
	} else {
		char tmp[12]; size_t n = 0;
		while (stage > 0) { tmp[n++] = (char)('0' + (stage % 10)); stage /= 10; }
		while (n--) buf[off++] = tmp[n];
	}
	static const char mid[] = " rc=";
	for (size_t i = 0; i < sizeof(mid) - 1; ++i) {
		buf[off++] = mid[i];
	}
	/* Async-signal-safe itoa for rc (can be negative if a raw -errno
	 * leaked through; we render the sign for full fidelity). */
	if (rc < 0) { buf[off++] = '-'; rc = -rc; }
	if (rc == 0) {
		buf[off++] = '0';
	} else {
		char tmp[12]; size_t n = 0;
		while (rc > 0) { tmp[n++] = (char)('0' + (rc % 10)); rc /= 10; }
		while (n--) buf[off++] = tmp[n];
	}
	buf[off++] = '\n';

	__wasi_ciovec_t iov;
	iov.buf = (const uint8_t *)buf;
	iov.buf_len = off;
	__wasi_size_t nwritten;
	(void)__wasi_fd_write(2, &iov, 1, &nwritten);
}

/* firebox#533 — unconditional reached-probe at function entry.
 *
 * #529 instrumented every non-zero return path (stages 1..4) and found
 * the libuv cond_wait_fail crumb fires on 3-4 runs across 30+ but ZERO
 * stage stamps emit. Three hypotheses:
 *
 *   H1 (most likely): trap/longjmp/asyncify-unwind escape before normal
 *       return. libuv's `if (pthread_cond_wait(...))` sees a non-zero
 *       return via stack unwinding (asyncify rewind into a frame with
 *       the wrong %rax), not via normal `ret`. Sibling to
 *       [[class_lesson_thread_teardown_via_guest_asyncify_escape]].
 *
 *   H2: prologue check at line 207 (__pthread_self()->tid lookup) traps
 *       OR symbol gets linker-rewritten elsewhere.
 *
 *   H3: LLVM LTO inlined the calls, then DCE'd via constant-prop on
 *       `e == 0` dataflow inferred from the inlined call site.
 *
 * If the reached-probe stamps AND cond_wait_fail crumb fires → H2 or H3
 *   (function IS entered but normal-return stamps don't survive).
 * If the reached-probe does NOT stamp AND cond_wait_fail crumb fires →
 *   H1 CONFIRMED: function is NEVER entered yet libuv reads a non-zero
 *   return — fix layer is wasmer-fork asyncify executor (per
 *   [[class_lesson_consumer_side_annotation_over_host_side_heuristic]]).
 *
 * CRITICAL: this function deliberately does NOT touch TLS, c, or m. The
 * earlier draft passed `__pthread_self()->tid` but that caused 20/20
 * canary OOB regressions — a wasi thread can enter pthread_cond_wait
 * before its TLS is fully wired (or under the very wedge condition we're
 * trying to observe), so the probe MUST be TLS-free. Emits an unsigned
 * marker only. */
static __attribute__((noinline,used))
void firebox_533_cond_wait_reached(void) {
	static const char msg[] = "FIREBOX_533_COND_WAIT_REACHED\n";
	__wasi_ciovec_t iov;
	iov.buf = (const uint8_t *)msg;
	iov.buf_len = sizeof(msg) - 1;
	__wasi_size_t nwritten;
	(void)__wasi_fd_write(2, &iov, 1, &nwritten);
}
#endif

/*
 * struct waiter
 *
 * Waiter objects have automatic storage on the waiting thread, and
 * are used in building a linked list representing waiters currently
 * waiting on the condition variable or a group of waiters woken
 * together by a broadcast or signal; in the case of signal, this is a
 * degenerate list of one member.
 *
 * Waiter lists attached to the condition variable itself are
 * protected by the lock on the cv. Detached waiter lists are never
 * modified again, but can only be traversed in reverse order, and are
 * protected by the "barrier" locks in each node, which are unlocked
 * in turn to control wake order.
 *
 * Since process-shared cond var semantics do not necessarily allow
 * one thread to see another's automatic storage (they may be in
 * different processes), the waiter list is not used for the
 * process-shared case, but the structure is still used to store data
 * needed by the cancellation cleanup handler.
 */

struct waiter {
	struct waiter *prev, *next;
	volatile int state, barrier;
	volatile int *notify;
};

/* Self-synchronized-destruction-safe lock functions */

static inline void lock(volatile int *l)
{
	if (a_cas(l, 0, 1)) {
		a_cas(l, 1, 2);
		do __wait(l, 0, 2, 1);
		while (a_cas(l, 0, 2));
	}
#ifndef __wasilibc_unmodified_upstream
	/* firebox#489 — register the lock-word in the calling thread's
	 * Phase 9 held-lock array. This primitive protects c->_c_lock (the
	 * pthread_cond_t internal mutex) and per-waiter node.barrier; both
	 * are vulnerable to the [[thread_teardown_via_guest_asyncify_escape]]
	 * orphan class. Phase 4 diagnosis confirmed cargo (Python #444
	 * wedge) parks on a stale l=2 cv lock-word with no live owner; the
	 * static-inline lock here had no held-list participation, so the
	 * __pthread_exit sweep couldn't release it.
	 *
	 * Same shape as __lock.c (#456 Phase 9) and __lockfile.c (#473):
	 * register on every successful acquire, deregister-before-release
	 * in unlock(). The two acquire paths above (fast a_cas(l,0,1) and
	 * the contended a_cas(l,0,2) at loop exit) both reach this point
	 * having taken ownership of the lock, so a single register at the
	 * function's tail covers both. */
	__firebox_register_held_lock(l);
#endif
}

static inline void unlock(volatile int *l)
{
#ifndef __wasilibc_unmodified_upstream
	/* firebox#489 — deregister BEFORE the a_swap release-to-waiters,
	 * same ordering rationale as __unlock in __lock.c and __unlockfile
	 * in __lockfile.c (#473): a concurrent __pthread_exit sweep on this
	 * thread must not observe the lock as on-our-held-list AFTER it has
	 * been released to other threads, or we'd emit a spurious wake on
	 * an address some other cv caller now legitimately holds. */
	__firebox_deregister_held_lock(l);
#endif
	if (a_swap(l, 0)==2)
		__wake(l, 1, 1);
}

static inline void unlock_requeue(volatile int *l, volatile int *r, int w)
{
#ifndef __wasilibc_unmodified_upstream
	/* firebox#489 — same deregister-before-release as unlock() above.
	 * unlock_requeue is invoked from the wake-and-handoff path
	 * (pthread_cond_timedwait.c:170) where the calling thread holds
	 * either c->_c_lock or a node.barrier acquired via lock() above.
	 * Without deregistration here, the held-list entry survives the
	 * a_store(l, 0) release, and a subsequent __pthread_exit sweep
	 * would emit a spurious wake on a lock-word some other waiter now
	 * legitimately holds. */
	__firebox_deregister_held_lock(l);
#endif
	a_store(l, 0);
#ifdef __wasilibc_unmodified_upstream
	if (w) __wake(l, 1, 1);
	else __syscall(SYS_futex, l, FUTEX_REQUEUE|FUTEX_PRIVATE, 0, 1, r) != -ENOSYS
		|| __syscall(SYS_futex, l, FUTEX_REQUEUE, 0, 1, r);
#else
	// Always wake due to lack of requeue system call in WASI
	// This can impact the performance, so we might need to re-visit that decision
	__wake(l, 1, 1);
#endif
}

enum {
	WAITING,
	SIGNALED,
	LEAVING,
};

int __pthread_cond_timedwait(pthread_cond_t *restrict c, pthread_mutex_t *restrict m, const struct timespec *restrict ts)
{
#ifndef __wasilibc_unmodified_upstream
	/* firebox#533 — unconditional reached-probe; FIRST statement of
	 * the function body, before any field-deref of c, m, OR TLS, so that
	 * even a wedge that corrupts c/m/TLS can't suppress this emit. The
	 * H1 discriminator: if libuv reports cond_wait_fail (a non-zero
	 * return from this function) AND this stamp does NOT appear on the
	 * same run, then the function was never entered — the non-zero
	 * return libuv observes is a stack-unwinding/asyncify-rewind
	 * artifact, and the wedge is below this layer in the wasmer-fork
	 * asyncify executor.
	 *
	 * Note: this DOES introduce a stderr write on EVERY
	 * pthread_cond_wait call in the canary, which is intentional — we
	 * need the per-run presence/absence signal. Volume is bounded by
	 * libuv's call pattern (small fixed number of cond_wait calls per
	 * worker, not per fd op). The probe is intentionally TLS-free: an
	 * earlier draft passed __pthread_self()->tid but that caused 20/20
	 * canary OOB regressions because a wasi thread can enter
	 * pthread_cond_wait before its TLS is fully initialised. */
	firebox_533_cond_wait_reached();
#endif

	struct waiter node = { 0 };
	int e, seq, clock = c->_c_clock, cs, shared=0, oldstate, tmp;
#ifndef __wasilibc_unmodified_upstream
	struct __clockid clock_id = { .id = clock };
#endif
	volatile int *fut;

	if ((m->_m_type&15) && (m->_m_lock&INT_MAX) != __pthread_self()->tid) {
#ifndef __wasilibc_unmodified_upstream
		/* firebox#529 stage=1: ERRORCHECK/RECURSIVE mutex not owned
		 * by calling thread. Most likely surface if the wedge is
		 * mutex-ownership tracking drift under concurrent contention. */
		firebox_529_cond_wait_trace(1, EPERM);
#endif
		return EPERM;
	}

	if (ts && ts->tv_nsec >= 1000000000UL) {
#ifndef __wasilibc_unmodified_upstream
		/* firebox#529 stage=2: invalid ts->tv_nsec. CANNOT FIRE for
		 * pthread_cond_wait callers (ts==NULL); stamped only for
		 * completeness — its appearance would mean the caller is a
		 * pthread_cond_timedwait user with bad input, NOT the
		 * cascade-9 wedge. */
		firebox_529_cond_wait_trace(2, EINVAL);
#endif
		return EINVAL;
	}

	__pthread_testcancel();

	if (c->_c_shared) {
		shared = 1;
		fut = &c->_c_seq;
		seq = c->_c_seq;
		a_inc(&c->_c_waiters);
	} else {
		lock(&c->_c_lock);

		seq = node.barrier = 2;
		fut = &node.barrier;
		node.state = WAITING;
		node.next = c->_c_head;
		c->_c_head = &node;
		if (!c->_c_tail) c->_c_tail = &node;
		else node.next->prev = &node;

		unlock(&c->_c_lock);
	}

	__pthread_mutex_unlock(m);

	__pthread_setcancelstate(PTHREAD_CANCEL_MASKED, &cs);
	if (cs == PTHREAD_CANCEL_DISABLE) __pthread_setcancelstate(cs, 0);

	do e = __timedwait_cp(fut, seq, clock, ts, !shared);
	while (*fut==seq && (!e || e==EINTR));
	if (e == EINTR) e = 0;

	if (shared) {
		/* Suppress cancellation if a signal was potentially
		 * consumed; this is a legitimate form of spurious
		 * wake even if not. */
		if (e == ECANCELED && c->_c_seq != seq) e = 0;
		if (a_fetch_add(&c->_c_waiters, -1) == -0x7fffffff)
			__wake(&c->_c_waiters, 1, 0);
		oldstate = WAITING;
		goto relock;
	}

	oldstate = a_cas(&node.state, WAITING, LEAVING);

	if (oldstate == WAITING) {
		/* Access to cv object is valid because this waiter was not
		 * yet signaled and a new signal/broadcast cannot return
		 * after seeing a LEAVING waiter without getting notified
		 * via the futex notify below. */

		lock(&c->_c_lock);

		if (c->_c_head == &node) c->_c_head = node.next;
		else if (node.prev) node.prev->next = node.next;
		if (c->_c_tail == &node) c->_c_tail = node.prev;
		else if (node.next) node.next->prev = node.prev;

		unlock(&c->_c_lock);

		if (node.notify) {
			if (a_fetch_add(node.notify, -1)==1)
				__wake(node.notify, 1, 1);
		}
	} else {
		/* Lock barrier first to control wake order. */
		lock(&node.barrier);
	}

relock:
	/* Errors locking the mutex override any existing error or
	 * cancellation, since the caller must see them to know the
	 * state of the mutex. */
	if ((tmp = pthread_mutex_lock(m))) {
#ifndef __wasilibc_unmodified_upstream
		/* firebox#529 stage=4: pthread_mutex_lock(m) failed on the
		 * relock path. POSIX: pthread_mutex_lock returns EAGAIN,
		 * ENOMEM, EDEADLK, EOWNERDEAD, ENOTRECOVERABLE, EPERM.
		 * In a libuv-style usage (NORMAL or RECURSIVE init), the
		 * realistic failures are EAGAIN/EOWNERDEAD/ENOTRECOVERABLE
		 * — all of which point at lower-layer wasi-libc mutex
		 * bookkeeping under concurrent contention rather than libuv. */
		firebox_529_cond_wait_trace(4, tmp);
#endif
		e = tmp;
	}

	if (oldstate == WAITING) goto done;

	if (!node.next && !(m->_m_type & 8))
		a_inc(&m->_m_waiters);

	/* Unlock the barrier that's holding back the next waiter, and
	 * either wake it or requeue it to the mutex. */
	if (node.prev) {
		int val = m->_m_lock;
		if (val>0) a_cas(&m->_m_lock, val, val|0x80000000);
		unlock_requeue(&node.prev->barrier, &m->_m_lock, m->_m_type & (8|128));
	} else if (!(m->_m_type & 8)) {
		a_dec(&m->_m_waiters);		
	}

	/* Since a signal was consumed, cancellation is not permitted. */
	if (e == ECANCELED) e = 0;

done:
	__pthread_setcancelstate(cs, 0);

	if (e == ECANCELED) {
		__pthread_testcancel();
		__pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0);
	}

#ifndef __wasilibc_unmodified_upstream
	/* firebox#529 stage=3: composite non-zero return at function exit.
	 * Catches anything that wasn't caught by stage=1/2/4: ECANCELED
	 * leaking through the masked-cancel guard, an unexpected
	 * __timedwait_cp result, or any e value re-assembled across the
	 * relock + cancellation epilogue. Stage 4 already fires for
	 * pthread_mutex_lock failures, so a stage=3 with a value that
	 * matches a recent stage=4 means BOTH paths contributed. */
	if (e != 0) {
		firebox_529_cond_wait_trace(3, e);
	}
#endif

	return e;
}

int __private_cond_signal(pthread_cond_t *c, int n)
{
	struct waiter *p, *first=0;
	volatile int ref = 0;
	int cur;

	lock(&c->_c_lock);
	for (p=c->_c_tail; n && p; p=p->prev) {
		if (a_cas(&p->state, WAITING, SIGNALED) != WAITING) {
			ref++;
			p->notify = &ref;
		} else {
			n--;
			if (!first) first=p;
		}
	}
	/* Split the list, leaving any remainder on the cv. */
	if (p) {
		if (p->next) p->next->prev = 0;
		p->next = 0;
	} else {
		c->_c_head = 0;
	}
	c->_c_tail = p;
	unlock(&c->_c_lock);

	/* Wait for any waiters in the LEAVING state to remove
	 * themselves from the list before returning or allowing
	 * signaled threads to proceed. */
	while ((cur = ref)) __wait(&ref, 0, cur, 1);

	/* Allow first signaled waiter, if any, to proceed. */
	if (first) unlock(&first->barrier);

	return 0;
}

weak_alias(__pthread_cond_timedwait, pthread_cond_timedwait);
