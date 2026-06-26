#include <signal.h>
#include <stdint.h>
#include <errno.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#else
#include <wasi/api.h>
#endif
#include "pthread_impl.h"

#ifndef __wasilibc_unmodified_upstream
/* firebox SA_NODEFER synchronous self-raise (defined in sigaction.c).
 * Returns 1 if it dispatched the handler synchronously in-guest for the
 * "raise the SAME signal whose SA_NODEFER handler is currently running on
 * this thread" case; 0 to fall through to the normal host delivery path.
 * POSIX SA_NODEFER requires the re-raised signal to re-enter the handler
 * IMMEDIATELY (before raise() returns), which the host's #912
 * non-reentrant-dispatch guard would otherwise defer. See sigaction.c. */
int __wasm_raise_self(int sig);
#endif

int raise(int sig)
{
#ifdef __wasilibc_unmodified_upstream
	sigset_t set;
	__block_app_sigs(&set);
	int ret = syscall(SYS_tkill, __pthread_self()->tid, sig);
	__restore_sigs(&set);
	return ret;
#else
	/* firebox#90Y — POSIX/musl argument validation. raise() of an
	 * out-of-range signal must fail with -1/EINVAL, NOT truncate to u8
	 * (`__wasi_signal_t`) and self-deliver a bogus in-range signal (e.g.
	 * raise(10000) -> (u8)16 == SIGSTKFLT -> "Stack fault" termination).
	 * `sig+0U >= (unsigned)_NSIG` is musl's own idiom (mirrors pthread_kill.c
	 * #SE3): one unsigned compare folds the negative case (a negative int
	 * wraps to a huge unsigned) and the too-large case, while letting sig==0
	 * through as the POSIX null-signal probe. raise.c was missed by the #SE3
	 * wave because raise() calls __wasi_thread_signal directly, bypassing
	 * pthread_kill's guard. */
	if (sig+0U >= (unsigned)_NSIG) {
		errno = EINVAL;
		return -1;
	}
	/* SA_NODEFER synchronous re-entry: if this is a self-raise of a
	 * signal whose SA_NODEFER handler is currently executing on this
	 * thread, dispatch it synchronously in-guest (Linux delivers it on
	 * the syscall return path, nesting the handler before raise()
	 * returns). Otherwise fall through to normal host delivery. */
	if (__wasm_raise_self(sig)) {
		return 0;
	}
	/* firebox#90Y — honor the C raise() contract: 0 on success, -1 with
	 * errno set on failure (the raw __wasi_errno_t was previously returned
	 * directly). Mirrors kill.c / sigqueue.c errno mapping. */
	__wasi_errno_t e = __wasi_thread_signal(__pthread_self()->tid, (__wasi_signal_t)sig);
	if (e != __WASI_ERRNO_SUCCESS) {
		errno = (int)e;
		return -1;
	}
	return 0;
#endif
}