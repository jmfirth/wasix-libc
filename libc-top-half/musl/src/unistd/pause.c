#include <unistd.h>
#ifndef __wasilibc_unmodified_upstream
#include "pthread_impl.h"
#else
#include "syscall.h"
#endif

int pause(void)
{
#ifndef __wasilibc_unmodified_upstream
	/* firebox#QMA: wait with the current mask, without a query/install gap
	 * that could lose the wake. Only an actual handler advances this tick;
	 * a blocked signal merely becoming pending does not satisfy pause(). */
	struct pthread *self = __pthread_self();
	int tick = self->sigdispatch_tick;
	__testcancel();
	while (self->sigdispatch_tick == tick) {
		__timedwait_cp(&self->sigdispatch_tick, tick, CLOCK_MONOTONIC, 0, 1);
		__testcancel();
	}
	errno = EINTR;
	return -1;
#else
#ifdef SYS_pause
	return syscall_cp(SYS_pause);
#else
	return syscall_cp(SYS_ppoll, 0, 0, 0, 0);
#endif
#endif
}
