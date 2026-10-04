#include <sys/resource.h>
#include <string.h>
#include <errno.h>
#include <wasi/api.h>
#include <wasi/libc.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#else
#include <stdint.h>
#include <__struct_rusage.h>

/* firebox#S37 -- the runtime's CPU accounting, the import times(2) already
 * reads (see ../time/times.c, which owns the description). Same symbol, same
 * import: this adds nothing to the import surface. */
#if defined(__wasm64__)
#define __FBX_WASIX_MODULE "wasix_64v1"
#else
#define __FBX_WASIX_MODULE "wasix_32v1"
#endif
struct __fbx_proc_times {
	uint64_t utime_ns;
	uint64_t stime_ns;
	uint64_t cutime_ns;
	uint64_t cstime_ns;
};
extern int32_t __imported_fbx_proc_times(void *out)
	__attribute__((__import_module__(__FBX_WASIX_MODULE),
	               __import_name__("proc_times")));

static struct timeval ns_to_timeval(uint64_t ns)
{
	return (struct timeval){ .tv_sec = ns / 1000000000ULL,
	                         .tv_usec = (ns % 1000000000ULL) / 1000ULL };
}
#endif

int getrusage(int who, struct rusage *ru)
{
#ifdef __wasilibc_unmodified_upstream
	int r;
#ifdef SYS_getrusage_time64
	long long kru64[18];
	r = __syscall(SYS_getrusage_time64, who, kru64);
	if (!r) {
		ru->ru_utime = (struct timeval)
			{ .tv_sec = kru64[0], .tv_usec = kru64[1] };
		ru->ru_stime = (struct timeval)
			{ .tv_sec = kru64[2], .tv_usec = kru64[3] };
		char *slots = (char *)&ru->ru_maxrss;
		for (int i=0; i<14; i++)
			*(long *)(slots + i*sizeof(long)) = kru64[4+i];
	}
	if (SYS_getrusage_time64 == SYS_getrusage || r != -ENOSYS)
		return __syscall_ret(r);
#endif
	char *dest = (char *)&ru->ru_maxrss - 4*sizeof(long);
	r = __syscall(SYS_getrusage, who, dest);
	if (!r && sizeof(time_t) > sizeof(long)) {
		long kru[4];
		memcpy(kru, dest, 4*sizeof(long));
		ru->ru_utime = (struct timeval)
			{ .tv_sec = kru[0], .tv_usec = kru[1] };
		ru->ru_stime = (struct timeval)
			{ .tv_sec = kru[2], .tv_usec = kru[3] };
	}
	return __syscall_ret(r);
#else
	/* firebox#S37 -- this used to report the MONOTONIC clock as both ru_utime
	 * and ru_stime (so a process that had run for a millisecond claimed the
	 * host's uptime in CPU, twice, with tv_usec a thousand times too large) and
	 * left the other fourteen fields as whatever was on the caller's stack.
	 *
	 * Every field is now either measured or zero. CPU time is the runtime's
	 * own accounting, the numbers times(2) reports. ru_maxrss is the linear
	 * memory, which only grows, so its size now is its peak; Linux reports
	 * kilobytes. Nothing else is accounted, and zero is what Linux reports for
	 * a field it does not maintain.
	 *
	 * RUSAGE_THREAD is refused: the accounting is per process, and a thread
	 * handed the whole process's CPU would be a wrong number, not a missing
	 * one. EINVAL is what getrusage() answers for a `who` it does not have. */
	if (who != RUSAGE_SELF && who != RUSAGE_CHILDREN) {
		errno = EINVAL;
		return -1;
	}
	struct __fbx_proc_times pt = {0, 0, 0, 0};
	int32_t error = __imported_fbx_proc_times(&pt);
	if (error != 0) {
		errno = __wasilibc_errno_from_wasi(error);
		return -1;
	}
	memset(ru, 0, sizeof *ru);
	if (who == RUSAGE_SELF) {
		ru->ru_utime = ns_to_timeval(pt.utime_ns);
		ru->ru_stime = ns_to_timeval(pt.stime_ns);
		ru->ru_maxrss = (long)(__builtin_wasm_memory_size(0) * 64);
	} else {
		ru->ru_utime = ns_to_timeval(pt.cutime_ns);
		ru->ru_stime = ns_to_timeval(pt.cstime_ns);
	}
	return 0;
#endif
}
