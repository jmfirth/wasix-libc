/* firebox#DEK: the whole-header `#error` is GONE.
 *
 * It read "WASI lacks process-associated clocks … compile with
 * -D_WASI_EMULATED_PROCESS_CLOCKS and link with -lwasi-emulated-process-clocks",
 * and it refused THE ENTIRE HEADER. Three things were wrong with that here:
 *
 *  1. ⛔ IT BROKE <sys/wait.h> ON EVERY SHELF WE SHIP. musl's features.h turns on
 *     _BSD_SOURCE by DEFAULT, and <sys/wait.h> then includes <sys/resource.h>
 *     unconditionally for wait3/wait4 — so `#include <sys/wait.h>`, the most
 *     ordinary POSIX process header there is, did not compile at all. MEASURED
 *     2026-08-26: all nine shelves, both widths, from a two-line program.
 *  2. ⛔ THE REMEDY IT PRESCRIBED DOES NOT SUPPLY getrusage. MEASURED:
 *     libwasi-emulated-process-clocks.a defines only `T __clock`; getrusage is
 *     `T getrusage` in libc.a itself. So the macro selected nothing emulated — it
 *     was a pure header unlock whose name asserted the opposite of the truth, and
 *     every caller that "fixed" the build by defining it was misled about why.
 *  3. ⛔ IT GATED FUNCTIONS THAT HAVE NOTHING TO DO WITH CLOCKS. getrlimit and
 *     setrlimit are `T` in libc.a and are not clock-related in any way.
 *
 * What remains honest: getrusage's VALUES may be wall-clock-derived rather than
 * process-associated. That is a semantics caveat to document, not grounds to make
 * the header uncompilable — an honest value with a caveat beats a build failure,
 * and a build failure is not how POSIX reports a degraded optional feature.
 */
#ifndef	_SYS_RESOURCE_H
#define	_SYS_RESOURCE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>
#include <sys/time.h>

#define __NEED_id_t

#ifdef _GNU_SOURCE
#define __NEED_pid_t
#endif

#include <bits/alltypes.h>
#include <bits/resource.h>

typedef unsigned long long rlim_t;

struct rlimit {
	rlim_t rlim_cur;
	rlim_t rlim_max;
};

#define RLIM_INFINITY (~0ULL)
#define RLIM_SAVED_CUR RLIM_INFINITY
#define RLIM_SAVED_MAX RLIM_INFINITY

int getrlimit (int, struct rlimit *);
int setrlimit (int, const struct rlimit *);

#ifdef __wasilibc_unmodified_upstream /* Use alternate WASI libc headers */
struct rusage {
	struct timeval ru_utime;
	struct timeval ru_stime;
	/* linux extentions, but useful */
	long	ru_maxrss;
	long	ru_ixrss;
	long	ru_idrss;
	long	ru_isrss;
	long	ru_minflt;
	long	ru_majflt;
	long	ru_nswap;
	long	ru_inblock;
	long	ru_oublock;
	long	ru_msgsnd;
	long	ru_msgrcv;
	long	ru_nsignals;
	long	ru_nvcsw;
	long	ru_nivcsw;
	/* room for more... */
	long    __reserved[16];
};

int getrusage (int, struct rusage *);

/* firebox#DEK: NOT defined in our libc.a (MEASURED 0 for all three), so they stay
 * hidden. Declaring what we do not define would only move the failure from compile
 * to link. getrusage/getrlimit/setrlimit above ARE defined and are now visible. */
#ifdef __wasilibc_unmodified_upstream /* firebox: no getpriority/setpriority */
int getpriority (int, id_t);
int setpriority (int, id_t, int);
#endif

#if defined(_GNU_SOURCE) && defined(__wasilibc_unmodified_upstream) /* firebox: no prlimit */
int prlimit(pid_t, int, const struct rlimit *, struct rlimit *);
#define prlimit64 prlimit
#endif

#define PRIO_MIN (-20)
#define PRIO_MAX 20

#define PRIO_PROCESS 0
#define PRIO_PGRP    1
#define PRIO_USER    2

#define RUSAGE_SELF     0
#define RUSAGE_CHILDREN (-1)
#define RUSAGE_THREAD   1

#define RLIMIT_CPU     0
#define RLIMIT_FSIZE   1
#define RLIMIT_DATA    2
#define RLIMIT_STACK   3
#define RLIMIT_CORE    4
#ifndef RLIMIT_RSS
#define RLIMIT_RSS     5
#define RLIMIT_NPROC   6
#define RLIMIT_NOFILE  7
#define RLIMIT_MEMLOCK 8
#define RLIMIT_AS      9
#endif
#define RLIMIT_LOCKS   10
#define RLIMIT_SIGPENDING 11
#define RLIMIT_MSGQUEUE 12
#define RLIMIT_NICE    13
#define RLIMIT_RTPRIO  14
#define RLIMIT_RTTIME  15
#define RLIMIT_NLIMITS 16

#define RLIM_NLIMITS RLIMIT_NLIMITS

#if defined(_LARGEFILE64_SOURCE) || defined(_GNU_SOURCE)
#define RLIM64_INFINITY RLIM_INFINITY
#define RLIM64_SAVED_CUR RLIM_SAVED_CUR
#define RLIM64_SAVED_MAX RLIM_SAVED_MAX
#define getrlimit64 getrlimit
#define setrlimit64 setrlimit
#define rlimit64 rlimit
#define rlim64_t rlim_t
#endif
#else
#include <__header_sys_resource.h>
#endif

#if _REDIR_TIME64
__REDIR(getrusage, __getrusage_time64);
#endif

#ifdef __cplusplus
}
#endif

#else
#include <__struct_rusage.h>

#endif
