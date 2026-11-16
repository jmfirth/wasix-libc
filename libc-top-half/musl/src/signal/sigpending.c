#include <signal.h>
#include <errno.h>
#include <string.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#endif

#ifndef __wasilibc_unmodified_upstream
#include "pthread_impl.h"
/* Process-wide pending bitmask (defined in sigaction.c). An int array
 * of (_NSIG+31)/32 words; bit (sig-1) set = sig pending.
 *
 * firebox#C3E — POSIX: the signals "pending on the calling thread or the
 * process". The thread's own set is self->pending_sigs[] (thread-directed);
 * __wasm_pending_sigs[] carries the process-directed set. This used to read
 * the process-wide word alone, into which every thread's own pend was also
 * mirrored, so one thread's raise() was pending on all of them. */
extern volatile int __wasm_pending_sigs[];
#endif

int sigpending(sigset_t *set)
{
#ifdef __wasilibc_unmodified_upstream
	return syscall(SYS_rt_sigpending, set, _NSIG/8);
#else
	if (!set) {
		errno = EFAULT;
		return -1;
	}
	/* Copy the per-bit pending state into the caller's sigset_t.
	 * sigset_t is a long-array with bit (sig-1). */
	sigemptyset(set);
	struct pthread *self = __pthread_self();
	for (int sig = 1; sig < _NSIG; sig++) {
		int word = (sig - 1) / 32;
		int bit = (sig - 1) % 32;
		int own = self ? self->pending_sigs[word] : 0;
		if ((own | __wasm_pending_sigs[word]) & (1 << bit)) {
			sigaddset(set, sig);
		}
	}
	return 0;
#endif
}