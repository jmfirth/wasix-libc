#include <unistd.h>
#include <errno.h>

/*
 * firebox#Y42 (guest half of the #HS6 pgid ABI) -- setpgrp IS setpgid(0, 0).
 *
 * The previous body was `errno = ENOSYS; return __wasilibc_pgrp;`, which is the
 * worst of both shapes at once. It set errno like a failure but RETURNED THE
 * GLOBAL (0 in practice) -- which is what success looks like to every caller
 * that checks the return value the way POSIX describes. And `errno` is only
 * meaningful once a call has already reported failure, so a conforming caller
 * never reads it. The ENOSYS was therefore invisible and the operation silently
 * did not happen: a false success wearing an honest-gap comment.
 *
 * With the host pgid model reachable there is nothing left to stub. The
 * operation is real, and the definition below is upstream musl's, unchanged.
 * Any error is whatever setpgid reports, which is now the host's measured
 * verdict rather than a guess.
 */
pid_t setpgrp(void)
{
	return setpgid(0, 0);
}
