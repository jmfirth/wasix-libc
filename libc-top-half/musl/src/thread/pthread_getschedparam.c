#include "pthread_impl.h"
#include "lock.h"

#ifdef __wasilibc_unmodified_upstream
int pthread_getschedparam(pthread_t t, int *restrict policy, struct sched_param *restrict param)
{
	int r;
	sigset_t set;
#ifdef __wasilibc_unmodified_upstream
	__block_app_sigs(&set);
#endif
	LOCK(t->killlock);
	if (!t->tid) {
		r = ESRCH;
	} else {
		r = -__syscall(SYS_sched_getparam, t->tid, param);
		if (!r) {
			*policy = __syscall(SYS_sched_getscheduler, t->tid);
		}
	}
	UNLOCK(t->killlock);
#ifdef __wasilibc_unmodified_upstream
	__restore_sigs(&set);
#endif
	return r;
}
#else
/* firebox#QAF — every thread in the single-scheduling-class substrate runs
 * SCHED_OTHER at static priority 0 (the state an unprivileged Linux thread
 * reports for itself). Report it faithfully under killlock; a cleared tid means
 * the thread has exited (ESRCH). See src/internal/sched_impl.h for the tier
 * framing. Returns an errno value directly, per POSIX. */
int pthread_getschedparam(pthread_t t, int *restrict policy, struct sched_param *restrict param)
{
	int r;
	LOCK(t->killlock);
	if (!t->tid) {
		r = ESRCH;
	} else {
		*policy = SCHED_OTHER;
		param->sched_priority = 0;
		r = 0;
	}
	UNLOCK(t->killlock);
	return r;
}
#endif
