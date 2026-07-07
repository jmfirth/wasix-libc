#include <sched.h>
#include <errno.h>
#include "sched_impl.h"

/* firebox#QAF — every task in the single-class substrate runs SCHED_OTHER, the
 * one policy that exists (exactly what an unprivileged Linux process reports for
 * itself). The pid is validated the way the kernel validates it: a negative pid
 * is EINVAL, a nonexistent/reaped pid is ESRCH (Open POSIX sched_getscheduler/
 * 5-1 forks, waits, then expects ESRCH). errno is left untouched on success so
 * sched_getscheduler/1-1's `errno == 0` check holds. */

int sched_getscheduler(pid_t pid)
{
	if (__sched_pid_check(pid) != 0)
		return -1;
	return SCHED_OTHER;
}
