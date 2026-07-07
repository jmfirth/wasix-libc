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
/* firebox#QAF — thread scheduling on the single-class substrate, unprivileged
 * semantics (mirrors sched_setscheduler): validate policy and priority range
 * (EINVAL), then refuse a real-time policy with EPERM; SCHED_OTHER/BATCH/IDLE at
 * priority 0 is the faithful no-op the substrate already provides. A cleared tid
 * is ESRCH. Returns an errno value directly, per POSIX. The shared numbers live
 * in src/internal/sched_impl.h so this cannot drift from sched_setscheduler. */
int pthread_setschedparam(pthread_t t, int policy, const struct sched_param *param) {
	if (!param) return EINVAL;
	if (!__sched_policy_valid(policy)) return EINVAL;
	if (param->sched_priority < __sched_priority_min(policy)
	    || param->sched_priority > __sched_priority_max(policy)) return EINVAL;
	int r;
	LOCK(t->killlock);
	if (!t->tid) r = ESRCH;
	else if (__sched_policy_is_realtime(policy)) r = EPERM;
	else r = 0;
	UNLOCK(t->killlock);
	return r;
}
#endif