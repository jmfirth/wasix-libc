#include <signal.h>
#include <errno.h>
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
		 * computes `bits = a_swap(&self->pending_sigs[word], 0)` and then
		 * `a_and(&__wasm_pending_sigs[word], ~bits)`, so with the mirror
		 * never set `bits == 0` and the AND-NOT is a no-op — and the
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
	__wasi_errno_t e = __wasi_proc_signal(pid, (__wasi_signal_t)sig);
	if (e != __WASI_ERRNO_SUCCESS) {
		errno = (int)e;
		return -1;
	}
	return 0;
#endif
}