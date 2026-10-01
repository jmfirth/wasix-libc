#ifdef __wasilibc_unmodified_upstream
#include <sys/timex.h>
#include <time.h>
#include <errno.h>
#include "syscall.h"

#define IS32BIT(x) !((x)+0x80000000ULL>>32)

struct ktimex64 {
	unsigned modes;
	int :32;
	long long offset, freq, maxerror, esterror;
	int status;
	int :32;
	long long constant, precision, tolerance;
	long long time_sec, time_usec;
	long long tick, ppsfreq, jitter;
	int shift;
	int :32;
	long long stabil, jitcnt, calcnt, errcnt, stbcnt;
	int tai;
	int __padding[11];
};

struct ktimex {
	unsigned modes;
	long offset, freq, maxerror, esterror;
	int status;
	long constant, precision, tolerance;
	long time_sec, time_usec;
	long tick, ppsfreq, jitter;
	int shift;
	long stabil, jitcnt, calcnt, errcnt, stbcnt;
	int tai;
	int __padding[11];
};

int clock_adjtime (clockid_t clock_id, struct timex *utx)
{
	int r = -ENOSYS;
#ifdef SYS_clock_adjtime64
	struct ktimex64 ktx = {
		.modes = utx->modes,
		.offset = utx->offset,
		.freq = utx->freq,
		.maxerror = utx->maxerror,
		.esterror = utx->esterror,
		.status = utx->status,
		.constant = utx->constant,
		.precision = utx->precision,
		.tolerance = utx->tolerance,
		.time_sec = utx->time.tv_sec,
		.time_usec = utx->time.tv_usec,
		.tick = utx->tick,
		.ppsfreq = utx->ppsfreq,
		.jitter = utx->jitter,
		.shift = utx->shift,
		.stabil = utx->stabil,
		.jitcnt = utx->jitcnt,
		.calcnt = utx->calcnt,
		.errcnt = utx->errcnt,
		.stbcnt = utx->stbcnt,
		.tai = utx->tai,
	};
	r = __syscall(SYS_clock_adjtime64, clock_id, &ktx);
	if (r>=0) {
		utx->modes = ktx.modes;
		utx->offset = ktx.offset;
		utx->freq = ktx.freq;
		utx->maxerror = ktx.maxerror;
		utx->esterror = ktx.esterror;
		utx->status = ktx.status;
		utx->constant = ktx.constant;
		utx->precision = ktx.precision;
		utx->tolerance = ktx.tolerance;
		utx->time.tv_sec = ktx.time_sec;
		utx->time.tv_usec = ktx.time_usec;
		utx->tick = ktx.tick;
		utx->ppsfreq = ktx.ppsfreq;
		utx->jitter = ktx.jitter;
		utx->shift = ktx.shift;
		utx->stabil = ktx.stabil;
		utx->jitcnt = ktx.jitcnt;
		utx->calcnt = ktx.calcnt;
		utx->errcnt = ktx.errcnt;
		utx->stbcnt = ktx.stbcnt;
		utx->tai = ktx.tai;
	}
	if (SYS_clock_adjtime == SYS_clock_adjtime64 || r!=-ENOSYS)
		return __syscall_ret(r);
	if ((utx->modes & ADJ_SETOFFSET) && !IS32BIT(utx->time.tv_sec))
		return __syscall_ret(-ENOTSUP);
#endif
	if (sizeof(time_t) > sizeof(long)) {
		struct ktimex ktx = {
			.modes = utx->modes,
			.offset = utx->offset,
			.freq = utx->freq,
			.maxerror = utx->maxerror,
			.esterror = utx->esterror,
			.status = utx->status,
			.constant = utx->constant,
			.precision = utx->precision,
			.tolerance = utx->tolerance,
			.time_sec = utx->time.tv_sec,
			.time_usec = utx->time.tv_usec,
			.tick = utx->tick,
			.ppsfreq = utx->ppsfreq,
			.jitter = utx->jitter,
			.shift = utx->shift,
			.stabil = utx->stabil,
			.jitcnt = utx->jitcnt,
			.calcnt = utx->calcnt,
			.errcnt = utx->errcnt,
			.stbcnt = utx->stbcnt,
			.tai = utx->tai,
		};
#ifdef SYS_adjtimex
		if (clock_id==CLOCK_REALTIME) r = __syscall(SYS_adjtimex, &ktx);
		else
#endif
		r = __syscall(SYS_clock_adjtime, clock_id, &ktx);
		if (r>=0) {
			utx->modes = ktx.modes;
			utx->offset = ktx.offset;
			utx->freq = ktx.freq;
			utx->maxerror = ktx.maxerror;
			utx->esterror = ktx.esterror;
			utx->status = ktx.status;
			utx->constant = ktx.constant;
			utx->precision = ktx.precision;
			utx->tolerance = ktx.tolerance;
			utx->time.tv_sec = ktx.time_sec;
			utx->time.tv_usec = ktx.time_usec;
			utx->tick = ktx.tick;
			utx->ppsfreq = ktx.ppsfreq;
			utx->jitter = ktx.jitter;
			utx->shift = ktx.shift;
			utx->stabil = ktx.stabil;
			utx->jitcnt = ktx.jitcnt;
			utx->calcnt = ktx.calcnt;
			utx->errcnt = ktx.errcnt;
			utx->stbcnt = ktx.stbcnt;
			utx->tai = ktx.tai;
		}
		return __syscall_ret(r);
	}
#ifdef SYS_adjtimex
	if (clock_id==CLOCK_REALTIME) return syscall(SYS_adjtimex, utx);
#endif
	return syscall(SYS_clock_adjtime, clock_id, utx);
}
#else
/* firebox#25D — adjtimex(2) / clock_adjtime(2) as an unprivileged Linux
 * container sees them: reads answer from the host kernel's clock discipline,
 * and every write is refused.
 *
 * Reads. NTP state is not namespaced, and a native guest's CLOCK_REALTIME IS
 * the host clock, so the host's discipline (via __wasix_clock_discipline_get)
 * is the true answer. A "never disciplined" answer (TIME_ERROR, STA_UNSYNC)
 * would assert a fact about a host clock that is usually synced. With no host
 * source (the browser) a read is ENOSYS.
 *
 * Writes. Setting the clock or its discipline needs CAP_SYS_TIME, which an
 * unprivileged container — Docker's default set included — lacks even as
 * euid 0. Firebox's "root holds every capability" model covers sandbox-owned
 * state; the host clock is shared with the host. So a write is EPERM in every
 * profile, decided here before any host call.
 *
 * The checks run in the kernel's order (SYSCALL clock_adjtime → copy_from_user,
 * do_clock_adjtime, timekeeping_validate_timex):
 *   NULL buf                                   EFAULT
 *   unknown clock                              EINVAL
 *   a valid clock with no clock_adj (MONOTONIC,
 *   the CPU-time clocks)                       EOPNOTSUPP
 *   ADJ_ADJTIME without ADJ_OFFSET_SINGLESHOT  EINVAL
 *   ADJ_ADJTIME without ADJ_OFFSET_READONLY    EPERM   (adjtime() itself)
 *   ADJ_SETOFFSET                              EPERM
 *   ADJ_FREQUENCY out of the 64-bit scale range EINVAL
 *   any other nonzero modes outside ADJ_ADJTIME EPERM
 * The kernel-internal bit names are spelled out below: <sys/timex.h> only
 * carries their userspace combinations (ADJ_OFFSET_SINGLESHOT = 0x8001,
 * ADJ_OFFSET_SS_READ = 0xa001).
 *
 * Field sources on a successful read: everything the kernel reports comes from
 * the host record, verbatim, except
 *   time  — this guest's clock_gettime(CLOCK_REALTIME), in the unit STA_NANO
 *           selects (tv_usec holds nanoseconds when STA_NANO is set);
 *   tick  — 1000000 / sysconf(_SC_CLK_TCK), Firebox's own tick;
 *   offset under ADJ_OFFSET_SS_READ — the pending adjtime() slew, as Linux
 *           reports it. A host that cannot supply that (macOS refuses
 *           unprivileged adjtime reads) answers ENOSYS for this mode rather
 *           than substitute the NTP offset. */
/* common/clock.h first: <time.h> pulls __typedef_clockid_t.h, which shares
 * its COMMON_CLOCK_H guard and would hide the checked resolver. */
#include <common/clock.h>
#include <sys/timex.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <unistd.h>
#include <wasi/api.h>
#include <wasi/libc.h>

#define KADJ_OFFSET_SINGLESHOT 0x0001 /* kernel-internal value */
#define KADJ_OFFSET_READONLY   0x2000
#define KADJ_ADJTIME           0x8000
#define PPM_SCALE              (1000LL << 16)

int clock_adjtime(clockid_t clock_id, struct timex *utx)
{
	__wasi_clockid_t id;
	if (!utx) {
		errno = EFAULT;
		return -1;
	}
	if (!__wasilibc_clockid_from_any_checked((uintptr_t)clock_id, &id)) {
		errno = EINVAL;
		return -1;
	}
	if (id != __WASI_CLOCKID_REALTIME) {
		errno = EOPNOTSUPP;
		return -1;
	}

	unsigned modes = utx->modes;
	int adjtime_read = 0;
	if (modes & KADJ_ADJTIME) {
		if (!(modes & KADJ_OFFSET_SINGLESHOT)) {
			errno = EINVAL;
			return -1;
		}
		if (!(modes & KADJ_OFFSET_READONLY)) {
			errno = EPERM;
			return -1;
		}
		adjtime_read = 1;
	} else if (modes) {
		errno = EPERM;
		return -1;
	}
	if (modes & ADJ_SETOFFSET) {
		errno = EPERM;
		return -1;
	}
	if ((modes & ADJ_FREQUENCY) &&
	    (LLONG_MIN / PPM_SCALE > (long long)utx->freq ||
	     LLONG_MAX / PPM_SCALE < (long long)utx->freq)) {
		errno = EINVAL;
		return -1;
	}

	int64_t d[__WASIX_CLOCK_DISCIPLINE_WORDS];
	__wasi_errno_t err = __wasix_clock_discipline_get(d);
	if (err != __WASI_ERRNO_SUCCESS) {
		errno = __wasilibc_errno_from_wasi(err);
		return -1;
	}
	if (adjtime_read && !(d[19] & __WASIX_CLOCK_DISCIPLINE_ADJTIME_VALID)) {
		errno = ENOSYS;
		return -1;
	}
	struct timespec now;
	if (clock_gettime(CLOCK_REALTIME, &now))
		return -1;
	long tck = sysconf(_SC_CLK_TCK);

	utx->status = (int)d[1];
	utx->offset = (long)(adjtime_read ? d[18] : d[2]);
	utx->freq = (long)d[3];
	utx->maxerror = (long)d[4];
	utx->esterror = (long)d[5];
	utx->constant = (long)d[6];
	utx->precision = (long)d[7];
	utx->tolerance = (long)d[8];
	utx->tai = (int)d[9];
	utx->ppsfreq = (long)d[10];
	utx->jitter = (long)d[11];
	utx->shift = (int)d[12];
	utx->stabil = (long)d[13];
	utx->jitcnt = (long)d[14];
	utx->calcnt = (long)d[15];
	utx->errcnt = (long)d[16];
	utx->stbcnt = (long)d[17];
	utx->time.tv_sec = now.tv_sec;
	utx->time.tv_usec = (d[1] & STA_NANO) ? now.tv_nsec : now.tv_nsec / 1000;
	utx->tick = tck > 0 ? 1000000 / tck : 0;
	return (int)d[0];
}
#endif
