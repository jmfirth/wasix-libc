/* firebox#5X0: __FBX_THREAD_LOCAL */
#include <features.h>
#include <signal.h>
#include <errno.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#endif
#ifndef __wasilibc_unmodified_upstream
#include <string.h>
#include "firebox_altstack.h"

/* firebox#9PX — per-thread alt-stack state. See firebox_altstack.h for why
 * this lives in TLS rather than in struct pthread. Zero-init => no alt stack,
 * not on alt stack. Read by __wasm_signal's SA_ONSTACK dispatch (sigaction.c). */
__FBX_THREAD_LOCAL void  *__fbx_altstack_sp    = 0;
__FBX_THREAD_LOCAL size_t __fbx_altstack_size  = 0;
__FBX_THREAD_LOCAL int    __fbx_altstack_flags = 0;
__FBX_THREAD_LOCAL int    __fbx_altstack_depth = 0;
#endif

int sigaltstack(const stack_t *restrict ss, stack_t *restrict old)
{
#ifdef __wasilibc_unmodified_upstream
	if (ss) {
		if (!(ss->ss_flags & SS_DISABLE) && ss->ss_size < MINSIGSTKSZ) {
			errno = ENOMEM;
			return -1;
		}
		if (ss->ss_flags & SS_ONSTACK) {
			errno = EINVAL;
			return -1;
		}
	}
	return syscall(SYS_sigaltstack, ss, old);
#else
	/* firebox#9PX — faithful per-thread sigaltstack(2).
	 *
	 * Before this, sigaltstack() unconditionally returned EINVAL and stored
	 * nothing, so a SA_ONSTACK handler had no alt stack to run on (the 26
	 * Open POSIX sigaction/12-* witnesses). The state now lives in the
	 * calling thread's TLS and is consulted by __wasm_signal's SA_ONSTACK
	 * stack switch.
	 *
	 * Semantics mirror musl's do_sigaltstack / the Linux kernel:
	 *   - old (if non-NULL): report the current alt stack; ss_flags carries
	 *     SS_ONSTACK when a handler is currently executing on it, else 0
	 *     (or SS_DISABLE when no alt stack is registered).
	 *   - ss (if non-NULL): install a new alt stack. SS_DISABLE clears it;
	 *     otherwise ss_size must be >= MINSIGSTKSZ. Changing the alt stack
	 *     while running on it is rejected with EPERM. Only SS_DISABLE and
	 *     SS_AUTODISARM are accepted in ss_flags. */
	stack_t cur;
	cur.ss_sp = __fbx_altstack_sp;
	cur.ss_size = __fbx_altstack_size;
	if (__fbx_altstack_depth > 0) {
		cur.ss_flags = SS_ONSTACK | (__fbx_altstack_flags & SS_AUTODISARM);
	} else if (__fbx_altstack_sp == 0) {
		cur.ss_flags = SS_DISABLE;
	} else {
		cur.ss_flags = __fbx_altstack_flags & SS_AUTODISARM;
	}

	if (ss) {
		/* The alt stack cannot be changed while a handler is executing
		 * on it (POSIX EPERM). SS_DISABLE while on-stack is likewise
		 * rejected — matches the Linux kernel. */
		if (__fbx_altstack_depth > 0) {
			errno = EPERM;
			return -1;
		}
		/* Only SS_DISABLE and SS_AUTODISARM are valid input flags.
		 * SS_ONSTACK as an input flag is invalid (it is output-only). */
		if (ss->ss_flags & ~(int)SS_FLAG_BITS & ~SS_DISABLE) {
			errno = EINVAL;
			return -1;
		}
		if (ss->ss_flags & SS_DISABLE) {
			__fbx_altstack_sp = 0;
			__fbx_altstack_size = 0;
			__fbx_altstack_flags = 0;
		} else {
			if (ss->ss_size < MINSIGSTKSZ) {
				errno = ENOMEM;
				return -1;
			}
			__fbx_altstack_sp = ss->ss_sp;
			__fbx_altstack_size = ss->ss_size;
			__fbx_altstack_flags = ss->ss_flags & SS_FLAG_BITS;
		}
	}

	if (old) {
		memcpy(old, &cur, sizeof cur);
	}

	return 0;
#endif
}
