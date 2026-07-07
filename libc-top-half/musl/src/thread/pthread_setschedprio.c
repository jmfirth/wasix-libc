#include "pthread_impl.h"
#include "lock.h"
#include "sched_impl.h"

#ifdef __wasilibc_unmodified_upstream
int pthread_setschedprio(pthread_t t, int prio)
{
	int r;
	sigset_t set;
#ifdef __wasilibc_unmodified_upstream
	__block_app_sigs(&set);
#endif
	LOCK(t->killlock);
	r = !t->tid ? ESRCH : -__syscall(SYS_sched_setparam, t->tid, &prio);
	UNLOCK(t->killlock);
#ifdef __wasilibc_unmodified_upstream
	__restore_sigs(&set);
#endif
	return r;
}
#else
/* firebox#QAF — set a thread's priority within its current policy. Threads are
 * always SCHED_OTHER (single class), whose only valid static priority is 0, so a
 * nonzero prio is out of range (EINVAL) and 0 is a faithful no-op. A cleared tid
 * is ESRCH. See src/internal/sched_impl.h. */
int pthread_setschedprio(pthread_t t, int prio)
{
	if (prio < __sched_priority_min(SCHED_OTHER)
	    || prio > __sched_priority_max(SCHED_OTHER)) return EINVAL;
	int r;
	LOCK(t->killlock);
	r = !t->tid ? ESRCH : 0;
	UNLOCK(t->killlock);
	return r;
}
#endif