#include <unistd.h>
#include <errno.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#endif

#ifdef __wasilibc_unmodified_upstream
#else
extern int __wasilibc_pgrp;
#endif

int setpgid(pid_t pid, pid_t pgid)
{
#ifdef __wasilibc_unmodified_upstream
	return syscall(SYS_setpgid, pid, pgid);
#else
	/* firebox#7RS (class_lesson_34x): both `pid == 0` and `pid == getpid()`
	 * name THIS process — the self-form `setpgid(getpid(), getpid())` is a
	 * perfectly valid POSIX call ("make this process its own group leader"),
	 * so it must succeed. Rejecting it with EINVAL was a wrong-errno Inv-0
	 * gap (the arguments are well-formed; the sandbox simply lacks a
	 * multi-process group table, which is not the caller's error). A
	 * genuinely-foreign pid still errors — EINVAL is retained here for
	 * scope; ESRCH/EPERM would be more faithful (out of scope for #7RS). */
	if (pid == 0 || pid == getpid()) {
		__wasilibc_pgrp = pgid;
		return 0;
	} else {
		errno = EINVAL;
		return -1;
	}
#endif
}
