#define _GNU_SOURCE
#include "pthread_impl.h"
#ifdef __wasilibc_unmodified_upstream
#include <sys/mman.h>
#else
#include <wasi/api.h>   /* firebox#GMC — __wasi_thread_join host import */
#endif

#ifdef __wasilibc_unmodified_upstream
static void dummy1(pthread_t t)
{
}
weak_alias(dummy1, __tl_sync);
#endif

static int __pthread_timedjoin_np(pthread_t t, void **res, const struct timespec *at)
{
	int state, cs, r = 0;
	__pthread_testcancel();
	__pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cs);
	if (cs == PTHREAD_CANCEL_ENABLE) __pthread_setcancelstate(cs, 0);
	while ((state = t->detach_state) && r != ETIMEDOUT && r != EINVAL) {
		if (state >= DT_DETACHED) a_crash();
		r = __timedwait_cp(&t->detach_state, state, CLOCK_REALTIME, at, 1);
	}
	__pthread_setcancelstate(cs, 0);
	if (r == ETIMEDOUT || r == EINVAL) return r;
	__tl_sync(t);
	if (res) *res = t->result;
#ifdef __wasilibc_unmodified_upstream
	if (t->map_base) __munmap(t->map_base, t->map_size);
#else
	/*
	 * firebox#GMC — t->map_base is the joined thread's allocation, and it
	 * contains BOTH that thread's pthread struct AND its live stack. The
	 * thread is still executing guest code (its __pthread_exit tail:
	 * __tl_unlock, the #456 orphan-lock sweep, and the __wasi_thread_exit
	 * loop itself) on that stack AFTER it published detach_state==DT_EXITED
	 * to release us (the detach_state loop above). Freeing now would hand the
	 * still-running stack to the next pthread_create's malloc, which links a
	 * *new* thread onto the same struct while the old one keeps mutating it —
	 * corrupting the musl thread list. A non-last worker then reads
	 * self->next==self and takes __pthread_exit's single-thread exit(0)
	 * fast-path, terminating the whole process mid-work (premature rc=0; the
	 * #WY3/#GMC codex-tui init wall).
	 *
	 * Upstream musl is immune because the joined thread's exit is SYS_exit
	 * (the kernel frees its stack only once the thread is truly gone) and the
	 * joiner's __munmap is correctly ordered against that. The faithful WASIX
	 * equivalent is __wasi_thread_join(): it blocks on the host until the
	 * joined thread's task has fully resolved — i.e. the thread is off its
	 * stack — so the subsequent free()/reuse can never race the thread's own
	 * teardown. firebox_join_tid carries the host tid (the value
	 * __wasi_thread_spawn returned at create); plain `tid` is unusable here
	 * because __pthread_exit zeroes it mid-teardown. A spawn that never
	 * started, or a thread already reaped, makes thread_join a fast no-op
	 * (the host returns immediately when the tid is unknown).
	 *
	 * firebox#VTS — this host-completion wait was originally (the #GMC commit)
	 * placed at the FRONT of pthread_join, before the detach_state loop. That
	 * was a deadlock: __wasi_thread_join is a HARD host-completion block, so
	 * the joiner could not make any further guest progress until the joinee's
	 * host thread had fully terminated — even when the joinee had not yet
	 * committed to exiting at all (it was still in its work loop / blocked in
	 * sem_wait). A legal teardown pattern where the joinee can only terminate
	 * via work the joiner's OWN continued progress enables (e.g. Open POSIX
	 * pthread_kill/8-1 + pthread_mutex_lock/3-1: main joins a sem_wait-blocked
	 * sender whose release depends on a sibling that main must still post to,
	 * or on main reaching a later sem_post) then wedges forever — passes on
	 * real Linux/glibc and on the pre-#GMC guest-futex path, TIMEOUT under the
	 * front-positioned host join.
	 *
	 * The UAF #GMC fixes only exists in the window AFTER the joinee publishes
	 * DT_EXITED (line 233 of pthread_create.c) and BEFORE its __wasi_thread_exit
	 * tail leaves the stack — i.e. exactly the gap this free() must not race.
	 * So the host-completion wait belongs HERE, right before the free, AFTER
	 * the detach_state loop has confirmed the joinee committed to exit. The
	 * detach_state loop is a cooperative guest-futex park (other threads run
	 * while the joiner waits), so the system can drain to the point where the
	 * joinee reaches __pthread_exit and publishes DT_EXITED — no deadlock.
	 *
	 * The #GMC comment's other claimed benefit (host-join-first sidesteps the
	 * detach_state __wake lost-wake class #444/#804/#807) is now redundant: the
	 * host futex_wait re-reads the guest futex word before parking (firebox#M3T
	 * value-recheck), so a __wake(&detach_state) landing in the park gap is no
	 * longer lost — the next loop iteration re-reads DT_EXITED and returns.
	 * A timed join (at != NULL) still keeps the pure guest path so the caller's
	 * deadline is honored; only the unbounded join takes the host wait here.
	 */
	if (!at && t->firebox_join_tid)
		(void)__wasi_thread_join((__wasi_tid_t)t->firebox_join_tid);
	if (t->map_base) free(t->map_base);
#endif
	return 0;
}

int __pthread_join(pthread_t t, void **res)
{
	return __pthread_timedjoin_np(t, res, 0);
}

static int __pthread_tryjoin_np(pthread_t t, void **res)
{
	return t->detach_state==DT_JOINABLE ? EBUSY : __pthread_join(t, res);
}

weak_alias(__pthread_tryjoin_np, pthread_tryjoin_np);
weak_alias(__pthread_timedjoin_np, pthread_timedjoin_np);
weak_alias(__pthread_join, pthread_join);
