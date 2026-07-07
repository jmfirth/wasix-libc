#include <sched.h>
#include <errno.h>
#include "sched_impl.h"

/* firebox#QAF — SCHED_OTHER runs at static priority 0, so that is the faithful
 * sched_param the substrate reports (an unprivileged Linux process sees the
 * same). pid validated as the kernel does (EINVAL for negative, ESRCH for a
 * reaped pid — Open POSIX sched_getparam/4-1). On success errno is untouched and
 * sched_priority is written, so sched_getparam/1-1 (`param.sched_priority`
 * changed from its -1 sentinel, errno==0) passes. */

int sched_getparam(pid_t pid, struct sched_param *param)
{
	if (!param) {
		errno = EINVAL;
		return -1;
	}
	if (__sched_pid_check(pid) != 0)
		return -1;
	param->sched_priority = 0;
	return 0;
}
