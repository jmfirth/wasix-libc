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

	/* Sample the DISPATCH counter (firebox#XT7), NOT sigsuspend_tick.
	 *
	 * sigsuspend must return only after a signal was actually DELIVERED to a
	 * handler (or terminated the process) — never when a signal merely became
	 * pending behind the temporary mask. sigsuspend_tick is bumped on BOTH the
	 * dispatch path and the enqueue/pend path (S5 added the pend-path bump so
	 * the sigtimedwait/sigwait family wakes on an awaited-signal pend). Parking
	 * sigsuspend on it makes it wake spuriously when a signal BLOCKED by the
	 * temporary `mask` pends; sigsuspend would then restore the saved mask,
	 * whose SIG_SETMASK drain delivers that just-pended blocked signal OUT OF
	 * ORDER — before the unblocked signal that was supposed to wake the suspend
	 * (the Open POSIX sigsuspend/1-1 regression: SIGUSR2 is blocked by the temp
	 * mask, must stay pending, and must be delivered only after sigsuspend
	 * returns; the unblocked SIGUSR1 handler is what wakes the suspend).
	 * sigdispatch_tick advances ONLY when a handler ran — exactly the POSIX
	 * wake condition.
	 *
	 * Sample BEFORE installing the temporary mask so a handler that runs
	 * between the install and the park is observed (sample-then-recheck). A
	 * previously-pending signal that the temporary mask now UNBLOCKS is drained
	 * by the pthread_sigmask(SIG_SETMASK, mask) below and dispatched, which
	 * advances sigdispatch_tick — so the wake still catches that wave. */
	int tick_before = self->sigdispatch_tick;
	if (pthread_sigmask(SIG_SETMASK, mask, NULL) != 0) {
		errno = EINVAL;
		return -1;
	}

	/* Wait until the dispatch counter advances — i.e. a signal handler
	 * ran on this thread. Re-check around __futexwait because spurious
	 * wakeups are possible and harmless. */
	while (self->sigdispatch_tick == tick_before) {
		__futexwait(&self->sigdispatch_tick, tick_before, 1);
	}

	/* Restore the caller's mask. This also drains any signals that
	 * became pending during the wait window. */
	pthread_sigmask(SIG_SETMASK, &saved, NULL);

	/* POSIX contract: sigsuspend always returns -1 with errno=EINTR. */
	errno = EINTR;
	return -1;
#endif
}