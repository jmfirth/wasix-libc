#include "pthread_impl.h"
#include <unistd.h>

int pthread_mutexattr_setpshared(pthread_mutexattr_t *a, int pshared)
{
	if (pshared > 1U) return EINVAL;
#ifndef __wasilibc_unmodified_upstream
	if (pshared == PTHREAD_PROCESS_SHARED && sysconf(_SC_THREAD_PROCESS_SHARED) < 0)
		return ENOTSUP;
#endif
	a->__attr &= ~128U;
	a->__attr |= pshared<<7;
	return 0;
}
