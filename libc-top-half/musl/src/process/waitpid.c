#include <stdlib.h>
#ifdef __wasilibc_unmodified_upstream
#include <sys/wait.h>
#include "syscall.h"
#else
#include <unistd.h>
#include <wasi/api.h>
#include <errno.h>
#endif

pid_t waitpid(pid_t pid, int *status, int options)
{
#ifdef __wasilibc_unmodified_upstream
	return syscall_cp(SYS_wait4, pid, status, options, 0);
#else
	__wasi_join_flags_t flags = 0;
	if ((options & WNOHANG) != 0) {
		flags |= __WASI_JOIN_FLAGS_NON_BLOCKING;
	}
	if ((options & WUNTRACED) != 0) {
		flags |= __WASI_JOIN_FLAGS_WAKE_STOPPED;
	}

	__wasi_option_pid_t opid;
	if (pid == -1) {
		opid.tag = __WASI_OPTION_NONE;
	} else {
		opid.tag = __WASI_OPTION_SOME;
		opid.u.some = abs(pid);
	}

	__wasi_join_status_t code;
	int ret = __wasi_proc_join((__wasi_option_pid_t*)&opid, flags, &code);
	if (ret != 0) {
		errno = ret;
		return -1;
	} else {
		// Firebox (#72N): POSIX `waitpid(-1, ..., WNOHANG)` on a
		// LIVE-but-not-ready child MUST return 0 (children exist, none
		// is reapable yet) — never ECHILD. The wasmer host any-child
		// NON_BLOCKING arm (proc_join.rs, #2JV) reports this as
		// `JoinStatusType::Nothing` and leaves the pid slot at
		// `OPTION_NONE` (only a reap/exit writes a pid). But the
		// `opid.tag != SOME -> ECHILD` check just below fires FIRST on
		// that `OPTION_NONE`, SHADOWING the `Nothing + WNOHANG -> 0`
		// branch further down (which #64 wrote for the specific-pid
		// case, where opid IS Some) — so the caller got ECHILD and
		// could not distinguish "children still running, keep waiting"
		// from "no children left, stop" (the #64 cmake/kwsys papercut
		// class: a poller abandons a still-running child).
		//
		// Hoist the any-child WNOHANG->0 return ABOVE the opid.tag
		// check. It is guarded on the ORIGINAL request `pid == -1`
		// (the any-child wait) so it fires ONLY for that case — the
		// host also emits `Nothing`+`OPTION_NONE` for a specific-pid
		// wait on a NON-child/absent pid (process is None), and POSIX
		// mandates ECHILD there, WNOHANG or not; that case has
		// `pid != -1`, so it correctly falls through to the ECHILD
		// branch. The specific-pid still-running case (`Nothing` with
		// opid == Some) is unchanged: it takes the `opid.tag == SOME`
		// path and hits #64's original `Nothing + WNOHANG -> 0` below.
		if (pid == -1 && code.tag == __WASI_JOIN_STATUS_TYPE_NOTHING &&
		    (options & WNOHANG) != 0) {
			if (status) {
				*status = 0;
			}
			return 0;
		}

		// Read the PID
		if (opid.tag == __WASI_OPTION_SOME) {
			pid = opid.u.some;
		} else {
			errno = ECHILD;
			return -1;
		}

		// Build the status code depending on what happened
		if (code.tag == __WASI_JOIN_STATUS_TYPE_NOTHING) {
			// Firebox (#64): POSIX `waitpid(pid, ..., WNOHANG)`
			// with a child that has NOT yet exited must return 0
			// (not `pid`). The wasmer host signals this state by
			// returning `JoinStatusType::Nothing` from `proc_join`
			// when the non-blocking try_join sees a still-running
			// child. Without this branch, any caller that treats
			// `result > 0` as "reaped" (e.g. cmake kwsys
			// ProcessUNIX.c `kwsysProcessDestroy`) incorrectly
			// marks a still-running child as terminated with
			// `WEXITSTATUS = 0`, which is the execute_process
			// `RESULT_VARIABLE` always-0 bug.
			if ((options & WNOHANG) != 0) {
				if (status) {
					*status = 0;
				}
				return 0;
			}
			*status = 0;
		} else if (code.tag == __WASI_JOIN_STATUS_TYPE_EXIT_NORMAL) {
			*status = W_EXITCODE(code.u.exit_normal, 0);
		} else if (code.tag == __WASI_JOIN_STATUS_TYPE_EXIT_SIGNAL) {
			*status = W_EXITCODE(code.u.exit_signal.exit_code, code.u.exit_signal.signal);
		} else if (code.tag == __WASI_JOIN_STATUS_TYPE_STOPPED) {
			*status = W_STOPCODE(code.u.stopped);
		} else {
			errno = EUNKNOWN;
			return -1;
		}
		return pid;
	}
#endif
}
