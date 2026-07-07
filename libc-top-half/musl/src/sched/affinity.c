#define _GNU_SOURCE
#include <sched.h>
#include <errno.h>
#include <string.h>
#include "pthread_impl.h"
#include "sched_impl.h"

/* firebox#QAF — CPU affinity for the single-logical-CPU substrate. The only
 * online CPU is #0, so the affinity mask is always exactly {0} — precisely what
 * an unprivileged Linux process pinned to a one-CPU cpuset reports. get returns
 * that mask; set succeeds when the requested mask includes CPU 0 (Linux requires
 * the mask to intersect the set of online CPUs) and fails EINVAL otherwise.
 * (Upstream drove these through SYS_sched_{get,set}affinity, absent under WASI.)
 * No Open POSIX row exercises affinity — affinity.h in the suite is __linux__-
 * gated — but the header declares these (sched.h), so shipping them faithfully
 * closes a latent undefined-symbol gap rather than leaving a promise unmet. */

int sched_getaffinity(pid_t pid, size_t size, cpu_set_t *set)
{
	if (!set || !size) {
		errno = EINVAL;
		return -1;
	}
	if (__sched_pid_check(pid) != 0)
		return -1;
	memset(set, 0, size);
	CPU_SET_S(0, size, set);
	return 0;
}

int sched_setaffinity(pid_t pid, size_t size, const cpu_set_t *set)
{
	if (!set || !size) {
		errno = EINVAL;
		return -1;
	}
	if (__sched_pid_check(pid) != 0)
		return -1;
	/* The requested mask must include the one online CPU (#0). */
	if (!CPU_ISSET_S(0, size, set)) {
		errno = EINVAL;
		return -1;
	}
	return 0;
}

/* The _np variants return an errno value directly (0 on success) rather than
 * setting errno/-1, matching glibc. A cleared tid means the thread has exited. */
int pthread_getaffinity_np(pthread_t td, size_t size, cpu_set_t *set)
{
	if (!set || !size)
		return EINVAL;
	if (!td->tid)
		return ESRCH;
	memset(set, 0, size);
	CPU_SET_S(0, size, set);
	return 0;
}

int pthread_setaffinity_np(pthread_t td, size_t size, const cpu_set_t *set)
{
	if (!set || !size)
		return EINVAL;
	if (!td->tid)
		return ESRCH;
	if (!CPU_ISSET_S(0, size, set))
		return EINVAL;
	return 0;
}
