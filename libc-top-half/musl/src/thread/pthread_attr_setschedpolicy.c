#include "pthread_impl.h"

int pthread_attr_setschedpolicy(pthread_attr_t *a, int policy)
{
	/* firebox: validate the policy against the set POSIX/glibc permit for
	 * thread scheduling attributes (SCHED_OTHER/SCHED_FIFO/SCHED_RR), and
	 * reject anything else with EINVAL.
	 *
	 * WHY this diverges from upstream musl (which stored any int and always
	 * returned 0): glibc's __pthread_attr_setschedpolicy rejects an unknown
	 * policy with EINVAL — `if (policy != SCHED_OTHER && policy != SCHED_FIFO
	 * && policy != SCHED_RR) return EINVAL;`. POSIX lists [EINVAL] "The value
	 * of policy is not valid" as a *may fail*, so musl's lax behavior was
	 * conformant-but-unfaithful; Linux (the substrate we emulate) rejects.
	 * Open POSIX pthread_attr_setschedpolicy/4-1 asserts the EINVAL for an
	 * out-of-range policy (999). Matching glibc here also keeps the
	 * getschedpolicy round-trip meaningful: only a valid policy is ever
	 * stored, so a later get returns a policy the attr could actually honor.
	 *
	 * Pure behavioral change — function signature and pthread_attr_t layout
	 * are unchanged, so the upstream wasix-libc/wasmer.io ABI is preserved
	 * (Invariant 8). EINVAL + SCHED_* come via pthread_impl.h -> <errno.h>
	 * and <pthread.h> -> <sched.h>. */
	if (policy != SCHED_OTHER && policy != SCHED_FIFO && policy != SCHED_RR)
		return EINVAL;
	a->_a_policy = policy;
	return 0;
}
