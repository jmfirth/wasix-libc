#include "pthread_impl.h"
#include <unistd.h>

int pthread_rwlockattr_setpshared(pthread_rwlockattr_t *a, int pshared)
{
	if (pshared > 1U) return EINVAL;
#ifndef __wasilibc_unmodified_upstream
	if (pshared == PTHREAD_PROCESS_SHARED && sysconf(_SC_THREAD_PROCESS_SHARED) < 0)
		return ENOTSUP;
#endif
	a->__attr[0] = pshared;
	return 0;
}
