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
 * guest owns depth via the __fbx_rtsigq ring. Feed it before the proc_signal
 * nudge; the host marks the process's pending set (firebox#KFC, below).
 * No-op for a non-RT
 * signal or a cross-process target (which routes to the host stash instead). */
extern int __fbx_rtsigq_push(int sig, union sigval value, int code,
                             pid_t pid, uid_t uid);
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
		/* firebox#KFC — and NO pending mark here. kill(getpid()) is
		 * process-directed: the host pends it ONCE in the process's set
		 * (__wasm_pending_sigs) before proc_signal returns, the same as a
		 * standard signal. This used to mark the caller's own set AND the
		 * process's (__fbx_rtsig_repend, firebox#4MP), which made one kill two
		 * pending instances: a sibling's sigwait accepted one and the caller
		 * still read it pending. MEASURED (LH2 kwdiag `rt-wait-h`, the probe
		 * `rt-wait-h` here): main sigpending 1, Linux 0. #4MP's defect — a word
		 * bit nothing could clear — cannot come back: every consumer now claims
		 * the process's bit itself (sigtimedwait, the unblock drain). */
	}
	/* firebox#KFC — the host pends a process-directed kill in the process's
	 * set (__wasm_pending_sigs) before proc_signal returns, once, on behalf of
	 * every thread that blocks it (WasiEnv::publish_process_signal); an
	 * unblocked thread's copy claims it from there. That includes a kill sent
	 * inside a handler, so a thread the handler creates sees it pending (#WW0
	 * residual 3; Linux t_pending=1, Firebox was 0 5 of 5).
	 *
	 * firebox#C3E history: an eager guest mark here was wrong under the #JRS
	 * router, which copied the instance into every blocking thread's own set
	 * and published the process-wide bit only once every copy was routed (a
	 * parked sigwaiter that took the early mark had no copy-token, so its copy
	 * pended the signal a second time; LSR `r2 kwaith` mainpending=1 19 of 20).
	 * Under the shared set there are no copies to race, and no mark is needed. */
	__wasi_errno_t e = __wasi_proc_signal(pid, (__wasi_signal_t)sig);
	if (e != __WASI_ERRNO_SUCCESS) {
		errno = __wasilibc_errno_from_wasi((int)e);
		return -1;
	}
	return 0;
#endif
}
