#include <sched.h>
#include <errno.h>
#include "sched_impl.h"

int sched_getscheduler(pid_t pid)
{
#ifdef __wasilibc_unmodified_upstream
	if (__sched_pid_check(pid) != 0)
		return -1;
	return SCHED_OTHER;
#else
	int policy;
	int error = __sched_access_local(pid, NULL, &policy);
	if (!error) return policy;
	if (error == ESRCH) {
		/* An existing foreign process's policy needs a host query. */
		if (__sched_pid_check(pid) != 0) return -1;
		error = ENOSYS;
	}
	errno = error;
	return -1;
#endif
}
