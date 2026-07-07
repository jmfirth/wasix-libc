#include <sched.h>
#include <errno.h>
#include "sched_impl.h"

/* firebox#QAF — faithful sched_get_priority_{max,min}. Upstream wasi-libc
 * guards these out (WASI has no scheduler); Firebox reports the real Linux
 * ranges for the single-class substrate. See sched_impl.h for the tier framing.
 * An invalid policy fails EINVAL exactly as Linux does; a valid policy returns
 * the range value without touching errno (Open POSIX asserts errno==0 on the
 * success path — sched_get_priority_max/1-1). */

int sched_get_priority_max(int policy)
{
	if (!__sched_policy_valid(policy)) {
		errno = EINVAL;
		return -1;
	}
	return __sched_priority_max(policy);
}

int sched_get_priority_min(int policy)
{
	if (!__sched_policy_valid(policy)) {
		errno = EINVAL;
		return -1;
	}
	return __sched_priority_min(policy);
}
