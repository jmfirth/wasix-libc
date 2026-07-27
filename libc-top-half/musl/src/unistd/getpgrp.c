#include <unistd.h>
#include <errno.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#endif

/*
 * firebox#Y42 (guest half of the #HS6 pgid ABI) -- getpgrp ASKS THE HOST.
 *
 * This file used to DEFINE `__wasilibc_pgrp`, the userspace global that stood
 * in for a process-group model the guest could not see. That model has existed
 * host-side since firebox#1HE and is now reachable (`proc_get_pgid`), so
 * getpgrp() is simply `getpgid(0)` -- the definition upstream musl uses, and
 * the relationship POSIX specifies.
 *
 * `__wasilibc_pgrp` survives, but ONLY as the terminal foreground-process-group
 * stand-in for tcgetpgrp/tcsetpgrp -- a different concept, with no host model
 * of its own yet. It is now defined by its writer, tcsetpgrp.c; see that file
 * for what it does and does not mean.
 *
 * POSIX gives getpgrp() no error return, and the host's `proc_get_pgid` cannot
 * fail for the `pid == 0` (caller) form -- the caller trivially exists -- so
 * getpgid(0) here is total. If it ever were not, it would report -1/errno
 * rather than invent a pgid.
 */
pid_t getpgrp(void)
{
#ifdef __wasilibc_unmodified_upstream
	return __syscall(SYS_getpgid, 0);
#else
	return getpgid(0);
#endif
}
