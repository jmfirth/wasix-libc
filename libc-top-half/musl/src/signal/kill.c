#include <signal.h>
#include <errno.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#else
#include <wasi/api.h>
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
	__wasi_errno_t e = __wasi_proc_signal(pid, (__wasi_signal_t)sig);
	if (e != __WASI_ERRNO_SUCCESS) {
		errno = (int)e;
		return -1;
	}
	return 0;
#endif
}