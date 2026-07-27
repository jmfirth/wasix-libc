#include <unistd.h>
#include <errno.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#endif

#ifdef __wasilibc_unmodified_upstream
#else
extern int __wasilibc_pgrp;
#endif

/*
 * firebox#388 (sibling of #347): WASIX has no host-side process-group
 * table; __wasilibc_pgrp is a per-process userspace global. The upstream
 * stub accepted only getpgid(0); every other caller-self shape was
 * rejected EINVAL -- including the POSIX-valid getpgid(getpid()) form
 * that bash and other shells use after fork to query their own pgid
 * without relying on the "0 means self" alias.
 *
 * Per POSIX (man 2 getpgid):
 *   - getpgid(0) returns the caller's pgid
 *   - getpgid(getpid()) returns the caller's pgid (same as 0)
 *   - getpgid(<unknown pid>) returns -1 with ESRCH
 *   - getpgid(<negative>) returns -1 with EINVAL
 *
 * We honor all four. For the unknown-pid case we use ESRCH (POSIX-
 * correct) rather than EINVAL, because EINVAL implies the caller passed
 * a malformed pid -- which is not what's happening; they passed a valid
 * pid we don't know about. This matches the #347 setpgid fix's
 * structurally-honest-error principle.
 */
pid_t getpgid(pid_t pid)
{
#ifdef __wasilibc_unmodified_upstream
	return syscall(SYS_getpgid, pid);
#else
	if (pid < 0) {
		errno = EINVAL;
		return -1;
	}
	if (pid == 0 || pid == getpid()) {
		return __wasilibc_pgrp;
	}
	/* pid is positive but != getpid(): a process we don't know about.
	 * POSIX-correct error here is ESRCH (no such process), not EINVAL. */
	errno = ESRCH;
	return -1;
#endif
}
