#include <signal.h>
#include <errno.h>
#include <string.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#endif

#ifndef __wasilibc_unmodified_upstream
/* Process-wide pending bitmask (defined in sigaction.c). An int array
 * of (_NSIG+31)/32 words; bit (sig-1) set = sig pending. */
#define __WASM_PENDING_WORDS_SIGP ((_NSIG + 31) / 32)
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
	for (int sig = 1; sig < _NSIG; sig++) {
		int word = (sig - 1) / 32;
		int bit = (sig - 1) % 32;
		if (__wasm_pending_sigs[word] & (1 << bit)) {
			sigaddset(set, sig);
		}
	}
	return 0;
#endif
}