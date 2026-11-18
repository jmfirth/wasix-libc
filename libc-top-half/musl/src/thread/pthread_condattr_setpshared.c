#include "pthread_impl.h"
#include <unistd.h>

int pthread_condattr_setpshared(pthread_condattr_t *a, int pshared)
{
	if (pshared > 1U) return EINVAL;
#ifndef __wasilibc_unmodified_upstream
	if (pshared == PTHREAD_PROCESS_SHARED && sysconf(_SC_THREAD_PROCESS_SHARED) < 0)
		return ENOTSUP;
#endif
	a->__attr &= 0x7fffffff;
	a->__attr |= (unsigned)pshared<<31;
	return 0;
}
