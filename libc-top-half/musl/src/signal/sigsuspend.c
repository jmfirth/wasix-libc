#include <signal.h>
#include <errno.h>
#include <string.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#else
#include "pthread_impl.h"
#include "atomic.h"
#endif

int sigsuspend(const sigset_t *mask)
{
#ifdef __wasilibc_unmodified_upstream
	return syscall_cp(SYS_rt_sigsuspend, mask, _NSIG/8);
#else
	if (!mask) {
		errno = EFAULT;
		return -1;
	}
	struct pthread *self = __pthread_self();
	if (!self) {
		errno = EINVAL;
		return -1;
	}

	/* Snapshot the current mask so we can restore it on return. */
	sigset_t saved;
	sigemptyset(&saved);
	if (pthread_sigmask(SIG_SETMASK, NULL, &saved) != 0) {
		/* pthread_sigmask never fails in our impl, but be safe. */
		errno = EINVAL;
		return -1;
	}

	/* Install the temporary mask. Any previously-pending signal whose
	 * bit is NOT set in `mask` will now be deliverable; pthread_sigmask
	 * drains the pending bitmask when the mask loosens, so the wake
	 * below can catch that wave. */
	int tick_before = self->sigsuspend_tick;
	if (pthread_sigmask(SIG_SETMASK, mask, NULL) != 0) {
		errno = EINVAL;
		return -1;
	}

	/* Wait until the wake counter advances — i.e. a signal handler
	 * ran on this thread. Re-check around __futexwait because spurious
	 * wakeups are possible and harmless. */
	while (self->sigsuspend_tick == tick_before) {
		__futexwait(&self->sigsuspend_tick, tick_before, 1);
	}

	/* Restore the caller's mask. This also drains any signals that
	 * became pending during the wait window. */
	pthread_sigmask(SIG_SETMASK, &saved, NULL);

	/* POSIX contract: sigsuspend always returns -1 with errno=EINTR. */
	errno = EINTR;
	return -1;
#endif
}