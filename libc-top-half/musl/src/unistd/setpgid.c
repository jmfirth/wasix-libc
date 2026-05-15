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
 * firebox#347: WASIX doesn't model process groups in the host runtime;
 * __wasilibc_pgrp is a per-process userspace global with no real
 * scheduling/signal semantics. The upstream stub only accepted the
 * pid == 0 form ("operate on caller"), rejecting every other shape with
 * EINVAL -- including the POSIX-valid forms bash uses after fork:
 *
 *   setpgid(getpid(), getpid())   // child making itself pg-leader
 *   setpgid(child_pid, child_pid) // parent making child pg-leader
 *
 * Both produced "bash: child setpgid: Invalid argument" warnings on
 * every command in `firebox run --entrypoint /bin/bash`.
 *
 * Per POSIX (man 2 setpgid):
 *   - setpgid(0, pgid) and setpgid(getpid(), pgid) are equivalent
 *   - pid == pgid creates a new group with the caller (or child) as leader
 *   - pgid < 0 is EINVAL
 *
 * Without a real process-group table in the host, we accept all
 * structurally valid forms and update the single userspace global only
 * when the request is "operate on the caller" (pid == 0 or pid ==
 * getpid()). Operations on a child pid are accepted as no-ops at this
 * layer (the child's wasilibc_pgrp lives in the child's address space).
 * This is the cheapest POSIX-shaped behavior that silences bash without
 * pretending we have multi-process group semantics we don't have.
 */
int setpgid(pid_t pid, pid_t pgid)
{
#ifdef __wasilibc_unmodified_upstream
	return syscall(SYS_setpgid, pid, pgid);
#else
	if (pgid < 0) {
		errno = EINVAL;
		return -1;
	}
	if (pid == 0 || pid == getpid()) {
		__wasilibc_pgrp = (pgid == 0) ? getpid() : pgid;
		return 0;
	}
	/* pid != 0 and pid != getpid(): a child or unrelated process.
	 * We have no host-side process-group table to update for another
	 * process, but the form is POSIX-valid (e.g. bash setting a child's
	 * pgid from the parent). Accept as a no-op rather than EINVAL. */
	return 0;
#endif
}
