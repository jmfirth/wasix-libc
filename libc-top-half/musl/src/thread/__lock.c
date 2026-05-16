#include "pthread_impl.h"
#include "firebox_lock_trace.h"

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

void __lock(volatile int *l)
{
	int need_locks = libc.need_locks;
	if (!need_locks) {
		/* Trace even the no-op path so we see whether __lock is being
		 * called at all from the hang context. The need_locks==0 case
		 * is the single-threaded fast-path and SHOULD NOT block, but
		 * the trace is cheap and disambiguates "lock wasn't entered"
		 * from "lock was entered but no-op'd". Firebox #384. */
		FIREBOX_LOCK_TRACE("lock_skip", "need_locks=0", l, *l);
		return;
	}
	FIREBOX_LOCK_TRACE("lock_enter", "__lock", l, *l);
	/* fast path: INT_MIN for the lock, +1 for the congestion */
	int current = a_cas(l, 0, INT_MIN + 1);
	if (need_locks < 0) libc.need_locks = 0;
	if (!current) {
		FIREBOX_LOCK_TRACE("lock_exit", "__lock_fast", l, *l);
		return;
	}
	/* A first spin loop, for medium congestion. */
	for (unsigned i = 0; i < 10; ++i) {
		if (current < 0) current -= INT_MIN + 1;
		// assertion: current >= 0
		int val = a_cas(l, current, INT_MIN + (current + 1));
		if (val == current) {
			FIREBOX_LOCK_TRACE("lock_exit", "__lock_spin", l, *l);
			return;
		}
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
			FIREBOX_LOCK_TRACE("lock_wait_pre", "__lock_heavy", l, current);
			__futexwait(l, current, 1);
			FIREBOX_LOCK_TRACE("lock_wait_post", "__lock_heavy", l, *l);
			current -= INT_MIN + 1;
		}
		/* assertion: current > 0, the count includes us already. */
		int val = a_cas(l, current, INT_MIN + current);
		if (val == current) {
			FIREBOX_LOCK_TRACE("lock_exit", "__lock_heavy", l, *l);
			return;
		}
		current = val;
	}
}

void __unlock(volatile int *l)
{
	FIREBOX_LOCK_TRACE("unlock_enter", "__unlock", l, *l);
	/* Check l[0] to see if we are multi-threaded. */
	if (l[0] < 0) {
		if (a_fetch_add(l, -(INT_MIN + 1)) != (INT_MIN + 1)) {
			__wake(l, 1, 1);
		}
	}
	FIREBOX_LOCK_TRACE("unlock_exit", "__unlock", l, *l);
}
