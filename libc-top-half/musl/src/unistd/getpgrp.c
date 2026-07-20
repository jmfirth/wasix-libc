#include <unistd.h>
#include <errno.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#endif

#ifdef __wasilibc_unmodified_upstream
#else
int __wasilibc_pgrp = 0;
/* firebox#7RS (class_lesson_34x): userspace session id backing setsid()/
 * getsid(). 0 = "setsid() never called"; getsid() then reports the caller's
 * own pid, since every process has a session per POSIX. Lives beside
 * __wasilibc_pgrp so the sandbox's session + process-group state share a home. */
int __wasilibc_sid = 0;
#endif

pid_t getpgrp(void)
{
#ifdef __wasilibc_unmodified_upstream
	return __syscall(SYS_getpgid, 0);
#else
	return __wasilibc_pgrp;
#endif
}
