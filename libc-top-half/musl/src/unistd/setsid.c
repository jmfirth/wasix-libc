#include <unistd.h>
#include <errno.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#endif

/*
 * firebox#Y42 -- setsid performs the half that IS backed, and no longer writes
 * the terminal global.
 *
 * SCOPE, stated plainly because the shape here is deliberate. There is NO
 * session model host-side -- `WasiProcess` carries a `pgid` and no `sid` at all
 * -- so setsid/getsid/tcgetsid remain unmodelled, and #HS6's pgid ABI
 * deliberately does not cover them. This file is NOT the fix for that; it is
 * the minimum needed to keep setsid coherent now that `__wasilibc_pgrp` means
 * the terminal foreground group rather than the caller's pgid (firebox#Y42).
 * Left alone, setsid() would have silently rewritten the TERMINAL's foreground
 * group -- a different and newly wrong thing.
 *
 * What setsid() must do splits cleanly in two:
 *   1. Make the caller a process-group leader. REAL as of #HS6: setpgid(0, 0)
 *      is host-backed and takes genuine effect, including for proc_signal's
 *      group delivery. It cannot fail for the self form.
 *   2. Make the caller a SESSION leader and detach its controlling terminal.
 *      UNBACKED. Nothing here does it.
 *
 * The `getpid()` return is therefore still a fiction -- it reports a session id
 * for a session that was not created (firebox#E4T, #9B8's measured
 * `setsid() = 1, errno = 0`). That fiction is NOT retired here: retiring it
 * honestly means either a host session model or an ENOSYS, and both are
 * decisions above this file's scope. It is recorded, not laundered -- do not
 * read the half-real implementation below as evidence that setsid works.
 */
pid_t setsid(void)
{
#ifdef __wasilibc_unmodified_upstream
	return syscall(SYS_setsid);
#else
	/* The backed half: become a process-group leader for real. */
	if (setpgid(0, 0) != 0)
		return -1;
	/* The unbacked half: no session is created. See the comment above. */
	return getpid();
#endif
}
