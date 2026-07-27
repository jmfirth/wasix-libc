#include <unistd.h>
#include <errno.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#else
#include <wasi/api.h>
#endif

/*
 * firebox#Y42 (guest half of the #HS6 pgid ABI) -- setpgid ASKS THE HOST.
 *
 * WHAT THIS REPLACES, AND WHY IT HAD TO GO. firebox#347 made this function
 * return 0 for `pid != 0 && pid != getpid()` while performing no operation at
 * all, on the stated premise that "without a real process-group table in the
 * host, we accept all structurally valid forms". That premise was FALSE: the
 * host has modelled process groups since firebox#1HE (`WasiProcess::pgid`,
 * defaulted at process creation, inherited across fork, stamped on spawn, and
 * scanned by `proc_signal` for group delivery). What was missing was only the
 * guest-facing syscall, which firebox#HS6 added as `proc_set_pgid`. This
 * wrapper is that call.
 *
 * The false premise is not the only problem with the old shape. A success
 * returned for an operation that did not happen is FAIL-OPEN, and invariant 0
 * rules on it directly: "an honest ENOSYS is faithful; a false success never
 * is -- absence is recoverable, a wrong answer is not". It was measured, not
 * inferred: `setpgid(999999, 999999)` returned `0` with `errno == 0` for a pid
 * that does not exist (firebox#E4T false success #2). node/libuv -- which,
 * unlike bash, TRUSTS the return value -- then operated on a process group
 * that was never formed, and failed every spawn with exit=71 (firebox#9B8,
 * three red REQUIRE_ALL gates). That is the signature of a false success: it
 * helps the caller that ignores the result and breaks the caller that trusts
 * it.
 *
 * There is deliberately NO local rule left here. Every POSIX decision (the
 * `pid`/`pgid` zero-forms, EINVAL for a negative pgid, ESRCH for a negative
 * pid or for a target that is neither the caller nor one of its children) is
 * the host's, because the host is the only layer that can see which pids exist
 * and who their parents are. Duplicating any of them here would recreate
 * exactly the guessing this change removes. See `proc_set_pgid.rs` in the
 * wasmer fork for those rules, and for the two POSIX refusals -- EACCES for a
 * child that has already exec'd, EPERM for a target in another session --
 * recorded there as honest gaps rather than approximated.
 */
int setpgid(pid_t pid, pid_t pgid)
{
#ifdef __wasilibc_unmodified_upstream
	return syscall(SYS_setpgid, pid, pgid);
#else
	__wasi_errno_t error = __wasi_proc_set_pgid((__wasi_pid_t) pid,
	                                            (__wasi_pid_t) pgid);
	if (error != 0) {
		errno = error;
		return -1;
	}
	return 0;
#endif
}
