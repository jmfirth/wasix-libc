#include "pthread_impl.h"

int pthread_cond_destroy(pthread_cond_t *c)
{
#ifndef __wasilibc_unmodified_upstream /* firebox#8RH: glibc-faithful EBUSY on destroy-in-use */
	/* Return EBUSY when a thread is currently blocked in
	 * pthread_cond_wait()/timedwait() on a process-private cv. POSIX only
	 * *recommends* this (destroying an in-use cv is otherwise undefined),
	 * but glibc and the Open POSIX suite (cond_destroy/speculative/4-1)
	 * reward EBUSY, so matching the dominant-Linux behavior is Inv-2. This
	 * is behavioral only — no struct field or symbol is added (Inv-8-safe),
	 * pure libc bookkeeping with no native dependency (holds on both
	 * profiles).
	 *
	 * Field keyed on (race-safety): _c_head. A private-cv waiter links a
	 * stack-local `struct waiter` into the cv's list under _c_lock —
	 * pthread_cond_timedwait.c does `c->_c_head = &node` while holding the
	 * lock, *before* it parks in __timedwait_cp — and is unlinked from that
	 * list under _c_lock when it is signaled (__private_cond_signal) or
	 * times out / cancels (the LEAVING path). So a non-NULL _c_head means a
	 * thread is parked on the cv; the value is a single aligned pointer word
	 * (relaxed read is not torn) and is stable while the waiter blocks. It
	 * is 0 for a fresh cv (PTHREAD_COND_INITIALIZER / zero init) and once
	 * every waiter has been signaled or left, so the destroy-when-idle rows
	 * still return 0 — crucially cond_destroy/3-1, which FAILs on a spurious
	 * EBUSY for an idle cv, stays green. The detection is deliberately
	 * best-effort/relaxed (a destroy racing an about-to-block waiter is
	 * caller UB), matching glibc's relaxed probe. NOTE: _c_waiters is the
	 * *process-shared* waiter counter only — the private wait path never
	 * touches it — so shared cvs keep musl's self-synchronized-destruction
	 * path below untouched. */
	if (!c->_c_shared && c->_c_head)
		return EBUSY;
#endif
	if (c->_c_shared && c->_c_waiters) {
		int cnt;
		a_or(&c->_c_waiters, 0x80000000);
		a_inc(&c->_c_seq);
		__wake(&c->_c_seq, -1, 0);
		while ((cnt = c->_c_waiters) & 0x7fffffff)
			__wait(&c->_c_waiters, 0, cnt, 0);
	}
	return 0;
}
