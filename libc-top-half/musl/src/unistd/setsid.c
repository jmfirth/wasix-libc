#include <unistd.h>
#include <errno.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#endif

#ifndef __wasilibc_unmodified_upstream
extern int __wasilibc_pgrp;
extern int __wasilibc_sid;
#endif

pid_t setsid(void)
{
#ifdef __wasilibc_unmodified_upstream
	return syscall(SYS_setsid);
#else
	/* firebox#7RS (class_lesson_34x): the single-process wasix sandbox has
	 * no kernel session/process-group table, but POSIX setsid() on a
	 * process that is not already a group leader ALWAYS succeeds — it makes
	 * the caller the leader of a NEW session and a NEW process group and
	 * returns the new session id (== the caller's pid). Returning EINVAL
	 * unconditionally was a wrong-errno Inv-0 gap (a wrong answer is worse
	 * than a crash). Model the session in userspace: the caller becomes its
	 * own session leader and its own process-group leader. */
	pid_t self = getpid();
	__wasilibc_sid = self;
	__wasilibc_pgrp = self;
	return self;
#endif
}
