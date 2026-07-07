#include "pthread_impl.h"
#include "lock.h"
#include <errno.h>
#ifdef __wasilibc_unmodified_upstream
#else
#include <wasi/api.h>
#include "signal.h"
#include <string.h>
#include <unistd.h>
/* firebox#VYD — RT-signal queue DEPTH for a CROSS-THREAD pthread_kill. The
 * pending state the host tracks is a coalescing doorbell (WasiThread::signal
 * dedups its Vec), so N pthread_kills of the same RT signal to a compute-bound
 * target collapse to a single dispatch. The guest owns RT depth via the
 * process-global __fbx_rtsigq ring (sigaction.c); before #VYD only self-raise
 * and sigqueue fed it, so a cross-thread pthread_kill carried no depth anywhere
 * the target could see. Feed the ring here (threads share linear memory) + set
 * the TARGET thread's pending bit so its next raise()/syscall boundary drains
 * the queued instances. __fbx_rtsigq_push no-ops for a non-RT signal. */
extern int __fbx_rtsigq_push(int sig, union sigval value, int code,
                             pid_t pid, uid_t uid);
extern volatile int __wasm_pending_sigs[];
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
	/* firebox#VYD — for an RT signal, enqueue a counted ring record + mark the
	 * TARGET thread's (and the process-wide) pending bit BEFORE the host nudge,
	 * so the depth survives the host's coalescing doorbell. SI_TKILL is Linux's
	 * si_code for a tgkill-family delivery; a 1-arg handler (raise-race) never
	 * observes it, an SA_SIGINFO handler would. */
	if (sig >= 35 && sig <= (_NSIG - 1) && t->tid) {
		union sigval sv; memset(&sv, 0, sizeof sv);
		(void)__fbx_rtsigq_push(sig, sv, SI_TKILL, getpid(), getuid());
		int word = (sig - 1) / 32;
		int bit  = 1 << ((sig - 1) % 32);
		a_or((volatile int *)&t->pending_sigs[word], bit);
		a_or(&__wasm_pending_sigs[word], bit);
	}
	int r = __wasi_thread_signal(t->tid, (__wasi_signal_t)sig);
	__restore_sigs(&set);
	return r;
}
#endif