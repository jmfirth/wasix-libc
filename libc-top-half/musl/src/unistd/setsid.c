#include <unistd.h>
#include <errno.h>
#include <wasi/libc.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#else
#include <wasi/api_firebox.h>
#endif

/*
 * firebox#7RS -- setsid ASKS THE HOST.
 *
 * The host models sessions: `WasiProcess::sid` sits beside `pgid`, is
 * inherited across fork/spawn, and is kept across exec. `proc_setsid` applies
 * the Linux rule there, where every process's group is visible: EPERM when a
 * process group named after the caller already exists, which includes the
 * caller being a group leader. Otherwise the caller leads a new session and a
 * new group, both equal to its pid.
 *
 * This replaces firebox#Y42's half-real body. That body made the caller a group
 * leader and returned getpid() for a session it never created. No rule is
 * applied here: the host is the only layer that can see which groups exist.
 */
pid_t setsid(void)
{
#ifdef __wasilibc_unmodified_upstream
	return syscall(SYS_setsid);
#else
	uint32_t sid = 0;
	__wasi_errno_t error = __wasix_proc_setsid(&sid);
	if (error != 0) {
		errno = __wasilibc_errno_from_wasi(error);
		return -1;
	}
	return (pid_t) sid;
#endif
}
