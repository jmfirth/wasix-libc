// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <common/clock.h>
#include <common/time.h>

#include <assert.h>
#include <wasi/api.h>
#include <errno.h>
#include <time.h>

static_assert(TIMER_ABSTIME == __WASI_SUBCLOCKFLAGS_SUBSCRIPTION_CLOCK_ABSTIME,
              "Value mismatch");

/* firebox#5RE — sleep()/nanosleep()/clock_nanosleep() are POSIX cancellation
 * points. On WASIX a pthread_cancel wakes a target parked in this
 * poll_oneoff (the runtime signal enqueue interrupts the wait — firebox#5RE
 * wasmer arm); on return we must act on a pending cancel. `__testcancel`
 * (defined in the posix-threads build, libc-top-half pthread_cancel.c) is a
 * no-op unless this thread has a pending cancel with cancellation enabled,
 * in which case it never returns (it unwinds via pthread_exit, running
 * cleanup handlers). Declared as a WEAK reference so the single-threaded
 * libc build (no pthread cancellation machinery) links with it absent: the
 * call then resolves to NULL and is guarded out. */
__attribute__((__weak__)) void __testcancel(void);

int clock_nanosleep(clockid_t clock_id, int flags, const struct timespec *rqtp,
                    struct timespec *rmtp) {
  if ((flags & ~TIMER_ABSTIME) != 0)
    return EINVAL;

  // firebox#79E — reject an unknown clock_id with EINVAL (POSIX). Resolving
  // here also avoids the out-of-bounds dereference the old
  // __wasilibc_clockid_from_any took on a bogus integer id
  // (clock_nanosleep/13-1: clock_nanosleep(99999, ...) must return EINVAL).
  __wasi_clockid_t id;
  if (!__wasilibc_clockid_from_any_checked((uintptr_t)clock_id, &id))
    return EINVAL;

  // Convert the requested deadline to an absolute WASI timestamp.
  __wasi_timestamp_t deadline;
  if (!timespec_to_timestamp_exact(rqtp, &deadline))
    return EINVAL;

  // firebox#MW0 — TIMER_ABSTIME is an *absolute* deadline. Rather than relying
  // on the host's absolute-clock subscription (which treated the deadline as
  // relative — clock_nanosleep/2-1, /3-1 hung), resolve the abstime path in
  // libc: read the current time on the same clock and compute the remaining
  // RELATIVE duration. A deadline already in the past (or now) means no
  // suspension occurs and we return success immediately (clock_nanosleep/3-1).
  // The subscription we then issue is always relative, so the host only ever
  // sees the well-behaved relative path (which clock_nanosleep/1-1 exercises).
  __wasi_timestamp_t timeout;
  if ((flags & TIMER_ABSTIME) != 0) {
    __wasi_timestamp_t now;
    if (__wasi_clock_time_get(id, 1, &now) != 0)
      return EINVAL;
    if (deadline <= now) {
      // Deadline is in the past: return immediately, no suspension.
      // Still honour a pending cancel (this is a cancellation point).
      if (&__testcancel != 0)
        __testcancel();
      return 0;
    }
    timeout = deadline - now;
  } else {
    timeout = deadline;
  }

  // Prepare a relative polling subscription. (We dropped TIMER_ABSTIME above:
  // `timeout` is always a relative duration now.)
  __wasi_subscription_t sub = {
      .u.tag = __WASI_EVENTTYPE_CLOCK,
      .u.u.clock.id = id,
      .u.u.clock.timeout = timeout,
      .u.u.clock.flags = 0,
  };

  // a zero timeout is an infinite wait, while 1 is used to
  // wait 0 seconds
  if (sub.u.u.clock.timeout == 0)
    sub.u.u.clock.timeout = 1;

  // firebox#NSL — capture a CLOCK_MONOTONIC baseline so an EINTR-interrupted
  // RELATIVE sleep can report the time remaining in rmtp (POSIX: "if the
  // TIMER_ABSTIME flag is not set ... the timespec referenced by rmtp is updated
  // to contain the amount of time remaining"). We measure elapsed on the
  // monotonic clock — NOT `id` — so a concurrent clock_settime(CLOCK_REALTIME)
  // can never skew a relative sleep's remainder. rmtp is left untouched for
  // TIMER_ABSTIME and when rmtp is NULL.
  __wasi_timestamp_t mono_start = 0;
  bool want_remaining = rmtp != NULL && (flags & TIMER_ABSTIME) == 0;
  if (want_remaining &&
      __wasi_clock_time_get(__WASI_CLOCKID_MONOTONIC, 1, &mono_start) != 0)
    want_remaining = false;

  // Block until polling event is triggered.
  __wasi_size_t nevents;
  __wasi_event_t ev;
  __wasi_errno_t error = __wasi_poll_oneoff(&sub, &ev, 1, &nevents);

  // firebox#5RE — cancellation point: if a pthread_cancel woke us, act on it
  // now. __testcancel never returns when a cancel is pending+enabled (it
  // unwinds via pthread_exit(PTHREAD_CANCELED), running cleanup handlers).
  // Weak: absent in the single-threaded build, where it resolves to NULL.
  if (&__testcancel != 0)
    __testcancel();

  // firebox#NSL — a delivered signal whose handler RAN interrupts the host
  // poll_oneoff with __WASI_ERRNO_INTR (the never-restart blocking-wait arm,
  // host firebox#G13). POSIX requires (clock_)nanosleep to fail with EINTR in
  // that case — not the ENOTSUP that the catch-all below used to fold every
  // non-success into — and, for a relative sleep with rmtp != NULL, to store the
  // unslept remainder in rmtp. nanosleep() (our caller) turns this EINTR into
  // errno=EINTR + a -1 return; clock_nanosleep() returns it directly. Without
  // this, every signal-interrupted sleep reported ENOTSUP and left rmtp
  // untouched (Open POSIX nanosleep/7-1,7-2 + clock_nanosleep/9-1,10-1).
  if (error == __WASI_ERRNO_INTR) {
    if (want_remaining) {
      __wasi_timestamp_t mono_now = mono_start;
      (void)__wasi_clock_time_get(__WASI_CLOCKID_MONOTONIC, 1, &mono_now);
      __wasi_timestamp_t elapsed =
          mono_now > mono_start ? mono_now - mono_start : 0;
      __wasi_timestamp_t remaining = elapsed < timeout ? timeout - elapsed : 0;
      rmtp->tv_sec = (time_t)(remaining / 1000000000ULL);
      rmtp->tv_nsec = (long)(remaining % 1000000000ULL);
    }
    return EINTR;
  }

  return error == 0 && ev.error == 0 ? 0 : ENOTSUP;
}

weak_alias(clock_nanosleep, __clock_nanosleep);
