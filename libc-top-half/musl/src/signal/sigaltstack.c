#include <signal.h>
#include <errno.h>
#include <string.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#endif

/*
 * firebox#388 (sibling of #347): Two bugs in one line on the wasix
 * branch (`return EINVAL`):
 *
 *   1. Wrong return form. POSIX-compliant libc functions report errors
 *      by setting errno and returning -1. `return EINVAL` returns the
 *      integer 22 to the caller, who sees "success with value 22" via
 *      the standard `if (result == 0)` check -- silently miscategorizing
 *      an error as success.
 *
 *   2. Wrong semantics. POSIX `sigaltstack(NULL, &old)` is the read-back
 *      form (query the current alt stack -- no error). `sigaltstack(NULL,
 *      NULL)` is a no-op. Failing them with EINVAL is hostile to programs
 *      (Python's signal init, glibc's defensive probes) that call them
 *      during startup.
 *
 * Per POSIX (man 2 sigaltstack):
 *   - ss==NULL just queries (writes *old, if non-NULL); no error
 *   - ss with SS_DISABLE disables; no error
 *   - ss with unknown flags returns -1 with EINVAL
 *   - ss with ss_size < MINSIGSTKSZ and !SS_DISABLE returns -1 with ENOMEM
 *
 * In a single-thread wasm model with no observable alt-stack effects
 * (the runtime serves one thread at a time and signal handlers run on
 * the host's own stack, not a user-defined alt-stack), we accept all
 * POSIX-valid forms as no-op success. We continue to reject genuinely
 * invalid flag combinations with the correct POSIX errno.
 *
 * SS_DISABLE / SS_ONSTACK constants are defined inside
 * #ifdef __wasilibc_unmodified_upstream in <signal.h> on this branch
 * (the wasm32 arch header doesn't define them either), so we use the
 * POSIX-standard numeric values inline. SS_ONSTACK == 1, SS_DISABLE == 2.
 */
#ifndef __WASILIBC_SS_ONSTACK
#define __WASILIBC_SS_ONSTACK 1
#endif
#ifndef __WASILIBC_SS_DISABLE
#define __WASILIBC_SS_DISABLE 2
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
	if (ss) {
		/* Reject unknown flag bits per POSIX. The only valid flag
		 * the caller can set is SS_DISABLE (value 2). SS_ONSTACK is
		 * a query-only bit and not legal to set. */
		if (ss->ss_flags & ~__WASILIBC_SS_DISABLE) {
			errno = EINVAL;
			return -1;
		}
		if (!(ss->ss_flags & __WASILIBC_SS_DISABLE)
		    && ss->ss_size < MINSIGSTKSZ) {
			errno = ENOMEM;
			return -1;
		}
	}
	if (old) {
		/* Truthful: we have no alt-stack model, so report SS_DISABLE
		 * with zero size and a null base. Programs that read this
		 * back will see "no alt stack configured" and proceed. */
		memset(old, 0, sizeof(*old));
		old->ss_flags = __WASILIBC_SS_DISABLE;
	}
	return 0;
#endif
}
