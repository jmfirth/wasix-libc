#include <unistd.h>
#include <errno.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#endif

/*
 * firebox#388 (sibling of #347): WASIX has no host-side session table.
 * Upstream stub returned EINVAL unconditionally -- which trips programs
 * that query their own session id (Python's os.getsid, bash's job-control
 * paths).
 *
 * Per POSIX (man 2 getsid):
 *   - getsid(0) returns the caller's session id
 *   - getsid(pid) returns the session id of process pid
 *   - returns -1 with ESRCH if pid is not a valid process
 *   - returns -1 with EPERM if the caller can't query that process
 *
 * In our single-process model sid == pid for the caller. For unknown
 * pids we return ESRCH (POSIX-correct), not EINVAL.
 */
pid_t getsid(pid_t pid)
{
#ifdef __wasilibc_unmodified_upstream
	return syscall(SYS_getsid, pid);
#else
	if (pid < 0) {
		errno = EINVAL;
		return -1;
	}
	pid_t self = getpid();
	if (pid == 0 || pid == self) {
		return self;
	}
	/* Other pids: we have no host-side session table to query. ESRCH
	 * (POSIX-correct for "no such process") rather than EINVAL. */
	errno = ESRCH;
	return -1;
#endif
}
