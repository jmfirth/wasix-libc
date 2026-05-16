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
 * firebox#388 (sibling of #347): WASIX has no host-side session table.
 * Upstream stub returned EINVAL unconditionally -- which trips any
 * daemonizer or interactive shell that calls setsid() to detach from
 * its controlling terminal.
 *
 * Per POSIX (man 2 setsid):
 *   - setsid() creates a new session with the caller as session leader
 *   - returns the new sid (== caller pid in practice)
 *   - returns -1 with EPERM if the caller is already a process group
 *     leader of an existing group
 *
 * Without a multi-process scheduler we adopt the #347 pattern: set the
 * userspace pgrp global to the caller's pid (the caller becomes its own
 * group leader; in the single-process model sid == pgid == pid) and
 * return getpid(). The "already group leader" check would never fire in
 * our model -- there is only one process per address space, and the
 * caller is always free to "lead" any session it wants.
 */
pid_t setsid(void)
{
#ifdef __wasilibc_unmodified_upstream
	return syscall(SYS_setsid);
#else
	pid_t self = getpid();
	__wasilibc_pgrp = self;
	return self;
#endif
}
