#include "pthread_impl.h"

int pthread_barrier_destroy(pthread_barrier_t *b)
{
#ifndef __wasilibc_unmodified_upstream /* firebox#8RH: glibc-faithful EBUSY on destroy-in-use */
	/* Return EBUSY when a thread is currently blocked in
	 * pthread_barrier_wait() on a process-private barrier. POSIX only
	 * *recommends* this (destroying an in-use barrier is otherwise
	 * undefined), but glibc and the Open POSIX suite (barrier_destroy/2-1)
	 * reward EBUSY, so matching the dominant-Linux behavior is Inv-2. This
	 * is behavioral only — no struct field or symbol is added, so the wasix
	 * import namespace (Inv-8) is untouched, and it is pure libc bookkeeping
	 * with no native dependency, so it holds identically on both profiles.
	 *
	 * Field keyed on (race-safety): _b_inst. For a private barrier
	 * (_b_limit >= 0) the FIRST thread to arrive publishes a live instance
	 * pointer under _b_lock — pthread_barrier_wait.c does `b->_b_inst = inst`
	 * while holding the lock, *before* it parks on inst->finished — and the
	 * thread that COMPLETES the barrier clears it under the same lock
	 * (`b->_b_inst = 0`) before any thread leaves. So a non-NULL _b_inst
	 * means at least one thread is parked waiting for the barrier to fill;
	 * the value is stable for the whole blocking window (an under-filled
	 * barrier never clears it) and is a single aligned pointer word, so the
	 * relaxed read here is not torn. _b_inst is 0 both when idle (the
	 * compound-literal init in pthread_barrier_init zeroes it) and after a
	 * completed cycle, so destroy-when-idle (barrier_destroy/1-1) still
	 * returns 0. The detection is deliberately best-effort/relaxed — a
	 * destroy racing an about-to-block waiter is caller UB — which is
	 * exactly glibc's own relaxed probe. Process-shared barriers
	 * (_b_limit < 0) do not use _b_inst and keep musl's self-synchronized-
	 * destruction path below untouched. */
	if (b->_b_limit >= 0 && b->_b_inst)
		return EBUSY;
#endif
	if (b->_b_limit < 0) {
		if (b->_b_lock) {
			int v;
			a_or(&b->_b_lock, INT_MIN);
			while ((v = b->_b_lock) & INT_MAX)
				__wait(&b->_b_lock, 0, v, 0);
		}
#ifdef __wasilibc_unmodified_upstream /* WASI does not understand processes or locking between them. */
		__vm_wait();
#endif
	}
	return 0;
}
