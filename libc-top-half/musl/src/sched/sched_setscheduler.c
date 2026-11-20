#include <sched.h>
#include <errno.h>
#include "sched_impl.h"

int sched_setscheduler(pid_t pid, int policy, const struct sched_param *param)
{
	if (!param) {
		errno = EINVAL;
		return -1;
	}
	int error = __sched_validate_param(policy, param->sched_priority);
	if (!error) {
#ifdef __wasilibc_unmodified_upstream
		if (__sched_pid_check_write(pid) != 0) return -1;
		error = __sched_policy_permission(SCHED_OTHER, policy);
#else
		error = __sched_access_local(pid, &policy, NULL);
		if (error == ESRCH) {
			/* The host can check a foreign process's owner and lifetime.
			 * Its guest policy is outside this libc's linear memory. */
			if (__sched_pid_check_write(pid) != 0) return -1;
			error = __sched_policy_is_realtime(policy) ? EPERM : ENOSYS;
		}
#endif
	}
	if (error) {
		errno = error;
		return -1;
	}
	return 0;
}
