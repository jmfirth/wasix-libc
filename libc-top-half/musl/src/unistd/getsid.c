#include <unistd.h>
#include <errno.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#endif

#ifndef __wasilibc_unmodified_upstream
extern int __wasilibc_sid;
#endif

pid_t getsid(pid_t pid)
{
#ifdef __wasilibc_unmodified_upstream
	return syscall(SYS_getsid, pid);
#else
	/* firebox#7RS (class_lesson_34x): getsid() is a pure query with no
	 * multi-process semantics in the single-process sandbox. Every process
	 * has a session per POSIX, so return the userspace session id set by
	 * setsid(), defaulting to the caller's own pid when setsid() was never
	 * called. Returning EINVAL unconditionally was a wrong-errno Inv-0 gap. */
	return __wasilibc_sid != 0 ? __wasilibc_sid : getpid();
#endif
}
