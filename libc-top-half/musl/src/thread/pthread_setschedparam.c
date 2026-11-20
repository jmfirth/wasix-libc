#include "pthread_impl.h"
#include "lock.h"
#include "sched_impl.h"

#ifdef __wasilibc_unmodified_upstream
int pthread_setschedparam(pthread_t t, int policy, const struct sched_param *param)
{
	int r;
	sigset_t set;
#ifdef __wasilibc_unmodified_upstream
	__block_app_sigs(&set);
#endif
	LOCK(t->killlock);
	r = !t->tid ? ESRCH : -__syscall(SYS_sched_setscheduler, t->tid, policy, param);
	UNLOCK(t->killlock);
#ifdef __wasilibc_unmodified_upstream
	__restore_sigs(&set);
#endif
	return r;
}
#else
/* Keep the TID live across the same policy update used by sched_setscheduler. */
int pthread_setschedparam(pthread_t t, int policy, const struct sched_param *param)
{
	if (!param) return EINVAL;
	int r = __sched_validate_param(policy, param->sched_priority);
	if (r) return r;
	LOCK(t->killlock);
	r = !t->tid ? ESRCH : __sched_set_thread_policy(t, policy);
	UNLOCK(t->killlock);
	return r;
}
#endif
