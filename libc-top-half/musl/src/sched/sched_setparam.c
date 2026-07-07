#include <sched.h>
#include <errno.h>
#include "sched_impl.h"

/* firebox#QAF — sched_setparam for the single-class substrate. Every task's
 * policy is SCHED_OTHER, whose only valid static priority is 0, so a nonzero
 * request is out of range and fails EINVAL (Open POSIX sched_setparam/23-1 sets
 * max(policy)+1 and expects EINVAL); setting priority 0 is the faithful no-op
 * the substrate already satisfies and succeeds (sched_setparam/22-1). pid is
 * validated as the kernel does: EINVAL for negative, ESRCH for a reaped pid. */

int sched_setparam(pid_t pid, const struct sched_param *param)
{
	if (!param) {
		errno = EINVAL;
		return -1;
	}
	if (pid < 0) {
		errno = EINVAL;
		return -1;
	}
	/* Current policy is SCHED_OTHER: valid priority range is [0, 0]. */
	if (param->sched_priority != 0) {
		errno = EINVAL;
		return -1;
	}
	if (__sched_pid_check(pid) != 0)
		return -1;
	return 0;
}
