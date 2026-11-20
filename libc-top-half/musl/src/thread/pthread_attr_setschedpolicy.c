#include "pthread_impl.h"
#include "sched_impl.h"

int pthread_attr_setschedpolicy(pthread_attr_t *a, int policy)
{
	/* Accept the policies sched_setscheduler understands, including Linux's
	 * BATCH and IDLE. Validate the priority when the policy is applied. */
	if (!__sched_policy_valid(policy)) return EINVAL;
	a->_a_policy = policy;
	return 0;
}
