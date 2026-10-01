#include <unistd.h>
#include <errno.h>
#include <wasi/libc.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#else
#include <wasi/api_firebox.h>
#endif

/*
 * firebox#7RS -- getsid ASKS THE HOST.
 *
 * firebox#388 answered from nothing. It gave getpid() for the caller, ESRCH for
 * every other pid (live or not), and EINVAL for a negative pid, which POSIX
 * does not list. `proc_get_sid` reads the host's per-process session id and
 * decides the arguments with getpgid's rules: `pid == 0` is the caller, and a
 * negative or unknown pid is ESRCH. The sign survives the cast because the host
 * recovers it from the u32.
 */
pid_t getsid(pid_t pid)
{
#ifdef __wasilibc_unmodified_upstream
	return syscall(SYS_getsid, pid);
#else
	uint32_t sid = 0;
	__wasi_errno_t error = __wasix_proc_get_sid((uint32_t) pid, &sid);
	if (error != 0) {
		errno = __wasilibc_errno_from_wasi(error);
		return -1;
	}
	return (pid_t) sid;
#endif
}
