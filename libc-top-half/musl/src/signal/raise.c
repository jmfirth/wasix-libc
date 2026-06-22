#include <signal.h>
#include <stdint.h>
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
	sigset_t set;
#ifdef __wasilibc_unmodified_upstream
	__block_app_sigs(&set);
#endif
#ifdef __wasilibc_unmodified_upstream
	int ret = syscall(SYS_tkill, __pthread_self()->tid, sig);
#else
	/* SA_NODEFER synchronous re-entry: if this is a self-raise of a
	 * signal whose SA_NODEFER handler is currently executing on this
	 * thread, dispatch it synchronously in-guest (Linux delivers it on
	 * the syscall return path, nesting the handler before raise()
	 * returns). Otherwise fall through to normal host delivery. */
	if (__wasm_raise_self(sig)) {
		return 0;
	}
	int ret = __wasi_thread_signal(__pthread_self()->tid, (__wasi_signal_t)sig);
#endif
#ifdef __wasilibc_unmodified_upstream
	__restore_sigs(&set);
#endif
	return ret;
}