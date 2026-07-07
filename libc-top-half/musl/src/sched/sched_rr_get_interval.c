#include <sched.h>
#include <errno.h>
#include <time.h>
#include "sched_impl.h"

/* firebox#QAF — sched_rr_get_interval reports the round-robin quantum. The
 * substrate never preempts (one scheduling class), but the query itself is
 * faithful: Linux answers with a plausible, non-negative interval (its default
 * SCHED_RR timeslice is ~100 ms), and so do we. pid is validated as the kernel
 * does (EINVAL negative, ESRCH reaped). Note the Open POSIX rr_get_interval
 * tests first call sched_setscheduler(0, SCHED_RR, ...), which the unprivileged
 * substrate refuses with EPERM, so those tests self-report UNRESOLVED before
 * reaching this function — this stays faithful for the paths that do reach it. */

int sched_rr_get_interval(pid_t pid, struct timespec *ts)
{
	if (!ts) {
		errno = EINVAL;
		return -1;
	}
	if (__sched_pid_check(pid) != 0)
		return -1;
	ts->tv_sec = 0;
	ts->tv_nsec = 100000000L; /* 100 ms — Linux's default RR timeslice */
	return 0;
}
