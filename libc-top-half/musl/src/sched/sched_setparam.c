#include <sched.h>
#include <errno.h>
#include "sched_impl.h"

/* firebox#QAF — every supported policy's only valid priority is 0, so a nonzero
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
	/* All supported policies share the static priority range [0, 0]. */
	if (param->sched_priority != 0) {
		errno = EINVAL;
		return -1;
	}
	/* Write path: a foreign-owner pid is denied EPERM, not swallowed as the
	 * read probe would (firebox#R63 — sched_setparam/26-1). */
	if (__sched_pid_check_write(pid) != 0)
		return -1;
	return 0;
}
