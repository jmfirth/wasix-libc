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
/* firebox#C2Q/#HPT — guest-side RT-signal siginfo FIFO (defined in
 * sigaction.c). For a SELF-directed RT sigqueue, the queued si_value is
 * stashed here before the host nudge so the SA_SIGINFO / sigwaitinfo
 * consumers can hand it back in FIFO order. No-op (returns 0) for a
 * standard signal. */
extern int __fbx_rtsigq_push(int sig, union sigval value, int code,
                             pid_t pid, uid_t uid);
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
	 * si_value PAYLOAD (firebox#C2Q/#HPT):
	 *   - SELF-directed (pid == getpid()): the queued value lives in the
	 *     sender's (== receiver's) guest memory, so we enqueue the full
	 *     {value, SI_QUEUE, pid, uid} siginfo record into the guest-side
	 *     RT-signal FIFO (__fbx_rtsigq_push, sigaction.c) BEFORE nudging the
	 *     host. The SA_SIGINFO handler dispatch and the synchronous
	 *     sigtimedwait/sigwaitinfo accept dequeue it in FIFO order — closing
	 *     sigaction/29-1 + the sigqueue/sigwaitinfo RT-FIFO witnesses.
	 *   - CROSS-PROCESS: the queued value CANNOT be carried in guest memory
	 *     across the fork boundary, and the WASI proc_signal seam takes
	 *     (pid, signal) only. Faithful cross-process si_value needs a host
	 *     signal-queue assist (a deliberate WASI-ABI extension) — tracked as
	 *     #27M (sigqueue/1-1, sigwaitinfo/8-1), out of scope for this libc
	 *     fix. Such a delivery still arrives with a zero si_value until then. */
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
			/* firebox#C2Q/#HPT — stash the queued si_value record (RT signals
			 * only; a no-op for standard signals) BEFORE the host nudge, so
			 * the delivery consumer hands back the real value in FIFO order.
			 * A full per-signal queue is the POSIX over-limit error EAGAIN. */
			if (__fbx_rtsigq_push(sig, value, SI_QUEUE,
			                      getpid(), getuid()) != 0) {
				errno = EAGAIN;
				return -1;
			}
			/* Self-delivery: respect the per-thread block mask via the
			 * thread-signal → __wasm_signal path (mirrors raise()). */
			e = __wasi_thread_signal((__wasi_tid_t)self->tid,
			                         (__wasi_signal_t)sig);
		} else {
			/* Cross-process: si_value not carried (needs host assist, #27M). */
			e = __wasi_proc_signal((__wasi_pid_t)pid,
			                       (__wasi_signal_t)sig);
		}
		if (e != 0) { errno = (int)e; return -1; }
		return 0;
	}
#endif
}
