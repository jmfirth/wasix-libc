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
#ifndef __wasilibc_unmodified_upstream
	/*
	 * firebox#GMC — block on the host until the joined thread has actually
	 * terminated, BEFORE the detach_state futex loop. __wasi_thread_join is
	 * a host-driven completion wait (await_termination), not a guest futex,
	 * so it is immune to the __pthread_exit __wake(&detach_state) lost-wake
	 * class (firebox#444/#804/#807) that the guest detach_state wait can hit
	 * under heavy create/join churn — there the joiner parks in
	 * __timedwait_cp just after the worker's one-shot wake, then waits
	 * forever (the host watchdog kills it with EDEADLK). Doing the host join
	 * first means that by the time the detach_state loop below runs, the
	 * thread is gone and detach_state is already DT_EXITED, so the loop
	 * returns without ever parking. It ALSO closes the map_base
	 * use-after-free: the host has fully reaped the thread (it is off its
	 * stack) before we free t->map_base below. A timed join (at != NULL)
	 * keeps using the guest path so the caller's deadline is honored; only
	 * the unbounded join takes the host-completion fast path. A spawn that
	 * never started or a thread already reaped makes thread_join a fast
	 * no-op. See pthread_create.c's #GMC comment + firebox_join_tid.
	 */
	if (!at && t->firebox_join_tid)
		(void)__wasi_thread_join((__wasi_tid_t)t->firebox_join_tid);
#endif
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
	 * to release us above. Freeing now would hand the still-running stack to
	 * the next pthread_create's malloc, which links a *new* thread onto the
	 * same struct while the old one keeps mutating it — corrupting the musl
	 * thread list. A non-last worker then reads self->next==self and takes
	 * __pthread_exit's single-thread exit(0) fast-path, terminating the whole
	 * process mid-work (premature rc=0; the #WY3/#GMC codex-tui init wall).
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
	 */
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
