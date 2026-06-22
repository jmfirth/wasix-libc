#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#endif
#include "pthread_impl.h"
#ifndef __wasilibc_unmodified_upstream
#include <wasi/api.h>
#endif

int sigqueue(pid_t pid, int sig, const union sigval value)
{
	siginfo_t si;
	sigset_t set;
	int r;
	memset(&si, 0, sizeof si);
	si.si_signo = sig;
	si.si_code = SI_QUEUE;
	si.si_value = value;
	si.si_uid = getuid();
#ifdef __wasilibc_unmodified_upstream
	__block_app_sigs(&set);
#endif
	si.si_pid = getpid();
#ifdef __wasilibc_unmodified_upstream
	r = syscall(SYS_rt_sigqueueinfo, pid, sig, &si);
	__restore_sigs(&set);
	return r;
#else
	/* firebox#43B / S5 — faithful sigqueue(2).
	 *
	 * POSIX: sigqueue() sends signal `sig` (with the application value
	 * `value`) to process `pid`. The error contract the Open POSIX suite
	 * checks:
	 *   - sig == 0 (the "null signal"): perform error checking on `pid`
	 *     but send NO signal — used to test for the existence of `pid`
	 *     (sigqueue/2-1, sigqueue/11-1's ESRCH probe).
	 *   - sig < 0 or sig >= _NSIG: invalid signal number → -1/EINVAL
	 *     (sigqueue/10-1).
	 *   - pid does not exist → -1/ESRCH (sigqueue/11-1, 2-2).
	 *
	 * The previous body was a stub (`r = EINVAL`), so every assertion
	 * failed (positive return, no errno). Map a nonzero host errno to the
	 * POSIX -1/errno convention.
	 *
	 * SELF vs CROSS-PROCESS delivery — the load-bearing distinction:
	 *   - SELF (pid == getpid()): route through __wasi_thread_signal on the
	 *     calling thread, EXACTLY as raise() does. WHY: the host routes a
	 *     thread-directed signal through the guest's __wasm_signal, which
	 *     consults the per-thread block mask — so a BLOCKED rtsig pends
	 *     into __wasm_pending_sigs (visible to sigwaitinfo/sigtimedwait)
	 *     instead of being delivered to a non-existent default disposition
	 *     and TERMINATING the process. A process-directed __wasi_proc_signal
	 *     to self does not take that in-guest blocked-mask path, so the
	 *     queued-then-accepted pattern the suite uses (sigwaitinfo/2-1,
	 *     sigqueue/5-1: sighold all rtsigs, sigqueue them to self, then
	 *     sigwaitinfo the lowest) would kill the process. raise() already
	 *     proves thread_signal-to-self respects the block (sigwait/1-1).
	 *   - CROSS-PROCESS: __wasi_proc_signal(pid, sig) — the kill(2) path.
	 *
	 * KNOWN GAP (tracked, NOT a stub): the queued `si_value` payload is
	 * NOT carried across either delivery — the WASI signal primitives take
	 * only (pid/tid, signal), so an SA_SIGINFO handler reads a zero
	 * si_value (sigqueue/1-1 checks si_value.sival_int across fork; the
	 * self-delivery siginfo built by __wasm_signal is likewise minimal).
	 * Faithfully passing the value needs a host signal-queue assist (a
	 * wasmer change) — out of scope for this libc fix; see RESULT.md. */
	(void)set;
	if (sig < 0 || sig >= _NSIG) {
		errno = EINVAL;
		return -1;
	}
	if (sig == 0) {
		/* Null signal: existence check only. Probe via proc_signal with
		 * signal 0 (the host treats 0 as the POSIX existence-check null
		 * signal) and map its errno (ESRCH for a nonexistent pid). */
		__wasi_errno_t e = __wasi_proc_signal((__wasi_pid_t)pid,
		                                      (__wasi_signal_t)0);
		if (e != 0) { errno = (int)e; return -1; }
		return 0;
	}
	{
		struct pthread *self = __pthread_self();
		__wasi_errno_t e;
		if (self && (pid == (pid_t)getpid())) {
			/* Self-delivery: respect the per-thread block mask via the
			 * thread-signal → __wasm_signal path (mirrors raise()). */
			e = __wasi_thread_signal((__wasi_tid_t)self->tid,
			                         (__wasi_signal_t)sig);
		} else {
			e = __wasi_proc_signal((__wasi_pid_t)pid,
			                       (__wasi_signal_t)sig);
		}
		if (e != 0) { errno = (int)e; return -1; }
		return 0;
	}
#endif
}
