#include <unistd.h>
#include <errno.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#else
#include <wasi/api.h>
#endif

/*
 * firebox#Y42 (guest half of the #HS6 pgid ABI) -- getpgid ASKS THE HOST.
 *
 * firebox#388 answered this from `__wasilibc_pgrp`, a per-process USERSPACE
 * global that no other process could see. That made two answers wrong in ways
 * a caller cannot work around:
 *
 *   - `getpgid(<any other pid>)` was ESRCH unconditionally, even for a live
 *     child of the caller. That is what blocks the Open POSIX killpg/1-2
 *     witness before killpg is ever reached ("Could not get pgid of child" --
 *     firebox#1HE).
 *   - `getpgid(<negative>)` was EINVAL. POSIX does not list EINVAL for getpgid
 *     at all, and Linux reaches ESRCH there via a failed pid lookup
 *     (firebox#E4T).
 *
 * The host has had the real model since firebox#1HE; `proc_get_pgid` is the
 * window onto it. No argument rule is applied here -- `pid == 0`, the negative
 * pid, and the lookup are all decided host-side, where the answer is measured
 * rather than guessed. Note the sign survives the cast: `__wasi_pid_t` is
 * unsigned on the wire and the host recovers the sign by reinterpreting as
 * i32, so a negative `pid_t` still lands on the ESRCH branch.
 */
pid_t getpgid(pid_t pid)
{
#ifdef __wasilibc_unmodified_upstream
	return syscall(SYS_getpgid, pid);
#else
	__wasi_pid_t pgid = 0;
	__wasi_errno_t error = __wasi_proc_get_pgid((__wasi_pid_t) pid, &pgid);
	if (error != 0) {
		errno = error;
		return -1;
	}
	return (pid_t) pgid;
#endif
}
