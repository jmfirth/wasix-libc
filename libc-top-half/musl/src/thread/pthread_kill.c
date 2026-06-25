#include "pthread_impl.h"
#include "lock.h"
#include <errno.h>
#ifdef __wasilibc_unmodified_upstream
#else
#include <wasi/api.h>
#include "signal.h"
#endif

#ifdef __wasilibc_unmodified_upstream
int pthread_kill(pthread_t t, int sig)
{
	int r;
	sigset_t set;
	/* Block not just app signals, but internal ones too, since
	 * pthread_kill is used to implement pthread_cancel, which
	 * must be async-cancel-safe. */
	__block_all_sigs(&set);
	LOCK(t->killlock);
	r = t->tid ? -__syscall(SYS_tkill, t->tid, sig)
		: (sig+0U >= _NSIG ? EINVAL : 0);
	UNLOCK(t->killlock);
	__restore_sigs(&set);
	return r;
}
#else
int pthread_kill(pthread_t t, int sig)
{
	/* firebox#SE3 — POSIX/musl argument validation: an out-of-range signal
	 * number must fail with EINVAL, NOT be passed to the host. `sig+0U >=
	 * _NSIG` is musl's own idiom: it folds the negative case (a negative int
	 * wraps to a huge unsigned, so e.g. sig=-1 → EINVAL) and the too-large
	 * case into one unsigned comparison, while letting sig==0 through as the
	 * POSIX null-signal probe (error-checks the thread, delivers nothing).
	 * Without this guard the firebox path handed an invalid signo straight to
	 * __wasi_thread_signal, which returned success — Open POSIX
	 * pthread_kill/7-1 ("did not fail on EINVAL"). The upstream branch above
	 * already performs this check; this restores it on the firebox branch. */
	if (sig+0U >= (unsigned)_NSIG) return EINVAL;
	sigset_t set;
	__block_all_sigs(&set);
	int r = __wasi_thread_signal(t->tid, (__wasi_signal_t)sig);
	__restore_sigs(&set);
	return r;
}
#endif