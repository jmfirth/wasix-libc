#include <signal.h>
#include <stdint.h>
#include <errno.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#else
#include <wasi/api.h>
#include <string.h>
#include <unistd.h>
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

/* firebox#RSQ — RT-signal queue DEPTH machinery (defined in sigaction.c /
 * pthread_sigmask.c). The pending state is a single BIT per signal, so it
 * cannot represent the QUEUE DEPTH that POSIX realtime signals require:
 * raising a blocked SIGRTMIN N times must leave N instances queued, and
 * sigpending() must keep reporting the signal pending until sigwait()/
 * sigtimedwait() have accepted all N (Open POSIX sigwait/2-1). The RT-signal
 * FIFO ring (__fbx_rtsigq) is that depth's source of truth; until now it was
 * fed only by sigqueue(). __fbx_rtsigq_push no-ops (returns 0) for a non-RT
 * signal. __wasm_thread_sig_blocked / __wasm_signals_blocked report whether
 * this raise will PEND rather than dispatch. */
extern int __fbx_rtsigq_push(int sig, union sigval value, int code,
                             pid_t pid, uid_t uid);
extern int __wasm_thread_sig_blocked(int sig);
extern volatile int __wasm_signals_blocked;
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
	/* firebox#RSQ — RT-signal queue DEPTH. If this is a self-raise of a
	 * currently-BLOCKED realtime signal, it will PEND (the host's
	 * __wasm_signal enqueues it into the pending bitmask rather than
	 * dispatching). The pending bitmask is one bit per signal and so loses
	 * the count, but POSIX requires a blocked RT signal raised N times to
	 * queue N instances. Enqueue a minimal SI_USER record into the RT-signal
	 * FIFO ring — the depth source of truth the sigwait/sigtimedwait accept
	 * consumers already drain one-per-call and re-pend from while non-empty
	 * (sigwait/2-1). Mirrors sigqueue()'s SELF branch but with SI_USER (a
	 * raise carries no queued value). GATED on "will pend": an UNBLOCKED
	 * raise dispatches immediately (a single delivery, no queued depth), and
	 * pushing there would leak a ring entry the non-SA_SIGINFO dispatch path
	 * never drains. __fbx_rtsigq_push no-ops for a non-RT signal, so standard
	 * signals are unchanged; the ring is bounded (over-limit push is a silent
	 * drop == the POSIX SIGQUEUE_MAX ceiling, harmless for the depth query). */
	if (__wasm_signals_blocked || __wasm_thread_sig_blocked(sig)) {
		union sigval sv;
		memset(&sv, 0, sizeof sv);
		(void)__fbx_rtsigq_push(sig, sv, SI_USER, getpid(), getuid());
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