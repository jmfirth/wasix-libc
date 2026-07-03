#include <sys/times.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#else
#include <wasi/api.h>
#include <stdint.h>
#endif
#include <errno.h>

#define NSEC_PER_SEC 1000000000ULL

/* firebox#3MB — times() reports CPU in clock ticks of 1/sysconf(_SC_CLK_TCK)
 * second. wasix-libc's sysconf returns _SC_CLK_TCK == 100 (see
 * libc-top-half/musl/src/conf/sysconf.c), so one tick is 1e9/100 = 1e7 ns.
 * The tms fields and the times() return value are all in these ticks. */
#define NSEC_PER_TICK (NSEC_PER_SEC / 100ULL)

#ifndef __wasilibc_unmodified_upstream
/* firebox#3MB — the WASIX import module the runtime registers proc_times under
 * (mirrors sigaction.c / sigqueue.c). */
#if defined(__wasm64__)
#define __FBX_WASIX_MODULE "wasix_64v1"
#else
#define __FBX_WASIX_MODULE "wasix_32v1"
#endif

/* firebox#3MB — additive proc_times import backing times(2).
 *
 * There is NO WASI syscall for times(2), and no standard clock id can express
 * a process's REAPED-CHILDREN CPU (tms_cutime/tms_cstime) — so a single
 * additive import that fills all four fields is the lowest-surface faithful
 * mapping (it matches Linux's single times() syscall). `out` points to four
 * consecutive little-endian uint64 NANOSECOND counters
 * { utime_ns, stime_ns, cutime_ns, cstime_ns } the runtime maintains from its
 * own cooperative-scheduler CPU accounting (identical on native + browser).
 *
 * Inv 8: ADDITIVE — a NEW import (same shape as tranche-B's signal_code_get),
 * NOT a widening of any existing one. A guest linked against a libc that never
 * emits it is unaffected, so prebuilt upstream wasmer.io artifacts keep running.
 * Arity: 1 param (ptr out) -> i32 errno. */
struct __fbx_proc_times {
	uint64_t utime_ns;
	uint64_t stime_ns;
	uint64_t cutime_ns;
	uint64_t cstime_ns;
};

extern int32_t __imported_fbx_proc_times(void *out)
	__attribute__((__import_module__(__FBX_WASIX_MODULE),
	               __import_name__("proc_times")));
#endif

clock_t times(struct tms *tms)
{
#ifdef __wasilibc_unmodified_upstream
	return __syscall(SYS_times, tms);
#else
	/* firebox#3MB — CPU accounting fields come from the runtime's per-process
	 * counters via the additive proc_times import: tms_utime/stime is this
	 * process's own CPU, tms_cutime/cstime is the CPU of children it has
	 * already wait()ed (0 until the first reap). This is the faithful Linux
	 * model — a fresh process starts at 0, the counter grows as the process
	 * runs, and a reaped child's CPU rolls into the parent's tms_cutime.
	 * (Replaces the old REALTIME-derived approximation, which mis-assigned the
	 * child fields from the wall clock — Open POSIX fork/8-1's INIT and
	 * post-waitpid assertions.) */
	struct __fbx_proc_times pt = {0, 0, 0, 0};
	if (__imported_fbx_proc_times(&pt) == 0) {
		tms->tms_utime  = pt.utime_ns  / NSEC_PER_TICK;
		tms->tms_stime  = pt.stime_ns  / NSEC_PER_TICK;
		tms->tms_cutime = pt.cutime_ns / NSEC_PER_TICK;
		tms->tms_cstime = pt.cstime_ns / NSEC_PER_TICK;
	} else {
		tms->tms_utime = tms->tms_stime = 0;
		tms->tms_cutime = tms->tms_cstime = 0;
	}

	/* times() returns elapsed real time in clock ticks. Monotonic is the
	 * faithful "real time" source here (browser-portable, never runs backward),
	 * matching Linux's "ticks since an arbitrary point in the past" contract. */
	__wasi_timestamp_t mono;
	__wasi_errno_t error =
	    __wasi_clock_time_get(__WASI_CLOCKID_MONOTONIC, 1, &mono);
	if (error != 0) {
		errno = error;
		return (clock_t)-1;
	}
	return (clock_t)(mono / NSEC_PER_TICK);
#endif
}
