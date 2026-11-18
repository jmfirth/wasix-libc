#include <signal.h>
#include <errno.h>
#include <wasi/libc.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#else
#include <wasi/api.h>
#include <string.h>
#include <unistd.h>
#include "pthread_impl.h"
#include "atomic.h"
/* firebox#VYD — RT-signal queue DEPTH for a SELF-directed kill(getpid(), RT).
 * Same rationale as raise()/pthread_kill(): the host doorbell coalesces, so the
 * guest owns depth via the __fbx_rtsigq ring. Feed it (+ the pending bitmask,
 * both words — see the firebox#4MP note below) before the proc_signal nudge.
 * No-op for a non-RT
 * signal or a cross-process target (which routes to the host stash instead). */
extern int __fbx_rtsigq_push(int sig, union sigval value, int code,
                             pid_t pid, uid_t uid);
/* firebox#4MP — THE paired pending-bit write (sigaction.c). It sets BOTH the
 * calling thread's `self->pending_sigs[]` mirror AND the process-wide
 * `__wasm_pending_sigs[]` word, which is the joint invariant the teardown
 * depends on: __wasm_drain_pending_sigs clears the process-wide word by
 * AND-NOTing the bits it SWAPS OUT of the per-thread mirror, and the
 * post-handler drain in __wasm_signal only runs at all when that mirror is
 * nonzero. A site that writes the process-wide word alone therefore sets a bit
 * that nothing on the consumption path can ever clear. Use this helper rather
 * than an open-coded a_or so the pair cannot be separated again. */
extern void __fbx_rtsig_repend(int sig);
int __wasm_signal_self(int sig, int code);
int __wasm_thread_sig_blocked(int sig);
#endif

int kill(pid_t pid, int sig)
{
#ifdef __wasilibc_unmodified_upstream
	return syscall(SYS_kill, pid, sig);
#else
	/* firebox#XCJ / #KZ0 — translate the WASI host convention into the POSIX
	 * kill(2) convention. __wasi_proc_signal returns an errno-typed value (0 on
	 * success, a nonzero __wasi_errno_t on failure — e.g. __WASI_ERRNO_SRCH for
	 * a nonexistent/finished process per firebox#MK4). POSIX kill() must instead
	 * return 0 on success and -1 with errno set on failure. The previous body
	 * returned that raw errno value directly, so kill(999999, 0) returned 71
	 * (truthy, not -1) and the Open POSIX kill/2-2 ESRCH check (`-1 == kill(...)`)
	 * deterministically FAILED.
	 *
	 * The errno values are consistent without a translation table: the firebox
	 * sysroot installs __errno_values.h, where the POSIX errno macros are ALIASED
	 * to the __WASI_ERRNO_* numbers (ESRCH == __WASI_ERRNO_SRCH == 71), so storing
	 * the raw host errno into `errno` matches what the program's <errno.h>
	 * compares against. This mirrors sigqueue()'s errno mapping in this dir.
	 *
	 * sig == 0 (the null signal) is the existence/permission error-check probe:
	 * it sends nothing but still validates `pid`, so its errno (ESRCH/EPERM) flows
	 * through the same mapping. */
	/* firebox#90Y — class sweep: an out-of-range signo must fail with EINVAL,
	 * NOT truncate to u8 (`__wasi_signal_t`) and cross-deliver a bogus in-range
	 * signal (the kill.c sibling of the raise.c truncation bug). Mirrors
	 * raise.c / pthread_kill.c (#SE3); `sig+0U >= (unsigned)_NSIG` lets sig==0
	 * through as the null-signal existence probe handled above. */
	if (sig+0U >= (unsigned)_NSIG) {
		errno = EINVAL;
		return -1;
	}
	/* Match raise/pthread_kill for an in-handler self-target. Host delivery
	 * defers inside its signal callback, after the interrupted handler's
	 * saved jump context may already have returned. Only an unblocked caller
	 * may take this process-directed delivery: a blocked caller must leave
	 * selection to the host so another unblocked thread or sigwaiter can win. */
	struct pthread *self = __pthread_self();
	if (self && self->in_handler_depth > 0 && pid == (pid_t)getpid() &&
	    !__wasm_thread_sig_blocked(sig) &&
	    __wasm_signal_self(sig, SI_USER))
		return 0;
	/* firebox#VYD — a SELF-directed RT kill queues depth in the guest ring (the
	 * 1-arg / SA_SIGINFO drain reads it); a cross-process RT kill queues a bare
	 * record in the HOST stash instead (proc_signal → #VYD-H1), reachable there
	 * because the target holds a separate linear memory. */
	if (sig >= 35 && sig <= (_NSIG - 1) && pid == (pid_t)getpid()) {
		union sigval sv; memset(&sv, 0, sizeof sv);
		(void)__fbx_rtsigq_push(sig, sv, SI_USER, getpid(), getuid());
		/* firebox#4MP — mark pending through the PAIRED write.
		 *
		 * This line used to be a bare
		 *     a_or(&__wasm_pending_sigs[word], bit);
		 * which set the process-wide word and NOT the calling thread's
		 * `pending_sigs[]` mirror. Every other set-path in the tree writes
		 * both (pthread_kill.c:66-67, timer_create.c:381-382,
		 * sigaction.c:648-649 and :1038-1042); kill.c was the sole outlier,
		 * and the mirror is what ARMS the clear. __wasm_drain_pending_sigs
		 * claims each bit SET IN `self->pending_sigs[word]` and only then
		 * clears it from `__wasm_pending_sigs[word]`, so with the mirror
		 * never set no bit is claimed and nothing is cleared — and the
		 * post-handler drain gate in __wasm_signal never even calls it,
		 * because that gate tests the mirror too. Result: after the ring
		 * record this bit represents had been consumed and the handler had
		 * run, the process-wide bit survived, so sigpending(2) kept
		 * reporting a signal that was not pending and timer_create's
		 * `if (pending & bit) { overrun++; return; }` expiry check treated
		 * every later expiry of that signo as already-pending — a POSIX
		 * timer on that signal never fired again.
		 *
		 * __fbx_rtsig_repend performs exactly the pair. The pending state
		 * and the RT ring are two stores that must agree; keeping the write
		 * behind the one helper is what stops them being separated again. */
		__fbx_rtsig_repend(sig);
	}
	/* firebox#C3E — do NOT mark a blocked self-kill in the process-wide word
	 * here. That word is what any thread's sigwait claims from, and the host
	 * router (#JRS) publishes a process-directed instance there only once
	 * every thread's copy has been routed, because a parked sigwaiter that
	 * takes it before its own copy is routed has no token: its copy then
	 * pends the signal a second time and the router publishes it again.
	 * An eager mark here reopened exactly that. MEASURED (LH2, LSR `r2
	 * kwaith`: main blocks SIGUSR2, a sibling parks in sigwait, main's
	 * SIGUSR1 handler kills itself with SIGUSR2): the sibling woke on the
	 * copy's arrival, took the marked bit, and the copy routed afterwards
	 * left SIGUSR2 pending everywhere, `mainpending=1` 19 of 20 at both
	 * widths; Linux prints 0. Without the mark the C3E probe's case B (a
	 * thread created after kill(getpid()) sees it pending) still matches
	 * Linux: outside a handler this thread's own copy is routed when
	 * proc_signal returns, which publishes a single thread's instance before
	 * pthread_create runs. (Inside a handler that route waits for the
	 * handler's return, so a thread created by the handler itself does not
	 * see it yet; Linux would show it.) */
	__wasi_errno_t e = __wasi_proc_signal(pid, (__wasi_signal_t)sig);
	if (e != __WASI_ERRNO_SUCCESS) {
		errno = __wasilibc_errno_from_wasi((int)e);
		return -1;
	}
	return 0;
#endif
}
