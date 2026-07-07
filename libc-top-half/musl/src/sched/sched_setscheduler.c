#include <sched.h>
#include <errno.h>
#include "sched_impl.h"

/* firebox#QAF — sched_setscheduler for the single-scheduling-class substrate,
 * modeling an unprivileged Linux process (the UNPRIVILEGED-SEMANTICS tier; see
 * sched_impl.h). The error ladder mirrors the kernel's, in the order the Open
 * POSIX tests pin:
 *   - invalid policy               -> EINVAL
 *   - priority outside policy range-> EINVAL  (sched_setscheduler/19-1 sets
 *                                     max+1 for each policy and expects EINVAL,
 *                                     checked BEFORE the ESRCH/EPERM legs)
 *   - nonexistent/reaped pid       -> ESRCH   (sched_setscheduler/21-1: valid
 *                                     SCHED_FIFO prio on a reaped child)
 *   - real-time policy (FIFO/RR)   -> EPERM   (unprivileged cannot select a
 *                                     real-time class; 16-1/22-x accept EPERM)
 * Selecting SCHED_OTHER/BATCH/IDLE at priority 0 — the class the substrate
 * already provides — is a faithful no-op that returns the previous policy, which
 * is always SCHED_OTHER. */

int sched_setscheduler(pid_t pid, int policy, const struct sched_param *param)
{
	if (!param) {
		errno = EINVAL;
		return -1;
	}
	if (!__sched_policy_valid(policy)) {
		errno = EINVAL;
		return -1;
	}
	if (param->sched_priority < __sched_priority_min(policy)
	    || param->sched_priority > __sched_priority_max(policy)) {
		errno = EINVAL;
		return -1;
	}
	if (__sched_pid_check(pid) != 0)
		return -1;
	if (__sched_policy_is_realtime(policy)) {
		errno = EPERM;
		return -1;
	}
	return SCHED_OTHER;
}
