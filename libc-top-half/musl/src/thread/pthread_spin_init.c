#include "pthread_impl.h"
#include <unistd.h>

int pthread_spin_init(pthread_spinlock_t *s, int shared)
{
#ifndef __wasilibc_unmodified_upstream
	if (shared == PTHREAD_PROCESS_SHARED && sysconf(_SC_THREAD_PROCESS_SHARED) < 0)
		return ENOTSUP;
#endif
	return *s = 0;
}
