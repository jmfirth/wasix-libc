// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <common/clock.h>
#include <common/time.h>

#include <assert.h>
#include <wasi/api.h>
#include <errno.h>
#include <stdint.h>
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

/* firebox#BYB — the largest duration a single `__wasi_subscription_clock_t`
 * can carry. `__wasi_timestamp_t` is nanoseconds in a u64, so one
 * subscription tops out at UINT64_MAX ns == 18446744073.709551615 s (~584
 * years). A longer sleep is not an error, it is more than one subscription:
 * see the chunking loop below. */
#define MAX_CHUNK_SEC (UINT64_MAX / (uint64_t)NSEC_PER_SEC)   /* 18446744073 */
#define MAX_CHUNK_NSEC ((uint32_t)(UINT64_MAX % (uint64_t)NSEC_PER_SEC)) /* 709551615 */

/* Subtract `elapsed_ns` from the decomposed remainder `(*sec, *nsec)`,
 * saturating at zero. Kept decomposed because the remainder routinely exceeds
 * what a single u64 of nanoseconds can hold. */
static void sub_elapsed(uint64_t *sec, uint32_t *nsec, uint64_t elapsed_ns) {
  uint64_t e_sec = elapsed_ns / (uint64_t)NSEC_PER_SEC;
  uint32_t e_nsec = (uint32_t)(elapsed_ns % (uint64_t)NSEC_PER_SEC);
  if (*sec < e_sec || (*sec == e_sec && *nsec <= e_nsec)) {
    *sec = 0;
    *nsec = 0;
    return;
  }
  *sec -= e_sec;
  if (*nsec < e_nsec) {
    *nsec += (uint32_t)NSEC_PER_SEC;
    *sec -= 1;
  }
  *nsec -= e_nsec;
}

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

  // firebox#BYB — validate the timespec HERE rather than by asking
  // timespec_to_timestamp_exact() to convert it. That helper folds two
  // different things into one `false`: "not a legal timespec" and "legal, but
  // longer than u64 nanoseconds can hold". Only the first is an error.
  // nanosleep(2)/clock_nanosleep(2) fail with EINVAL exactly when tv_nsec is
  // outside [0, 999999999] or tv_sec is negative; a LARGE POSITIVE tv_sec is
  // legal and sleeps.
  //
  // MEASURED 2026-09-07 against the shipped coreutils.webc, before this fix:
  //   sleep 18446744073  -> still sleeping at 6s
  //   sleep 18446744074  -> EINVAL, and Rust std's Thread::sleep asserts
  //                         errno == EINTR, so the guest panicked into
  //                         abort() (rc 127). `sleep infinity` — the
  //                         canonical container keep-alive — kept nothing
  //                         alive.
  // The boundary is exactly u64::MAX NANOSECONDS, i.e. this conversion, not
  // the wire and not the host: a probe issuing `poll_oneoff` DIRECTLY with
  // `clock.timeout = UINT64_MAX` slept normally, which exonerates both.
  if (rqtp->tv_nsec < 0 || rqtp->tv_nsec >= NSEC_PER_SEC)
    return EINVAL;
  if (rqtp->tv_sec < 0)
    return EINVAL;

  // The duration still to be slept, kept DECOMPOSED as (seconds, nanoseconds)
  // for the whole of this function. `sleep infinity` reaches libc as
  // tv_sec == time_t::MAX (Rust std clamps to time_t::MAX per iteration;
  // GNU/uutils coreutils passes the same shape), which is ~5e8 times larger
  // than a u64 of nanoseconds can express — so the running remainder can
  // never live in one __wasi_timestamp_t.
  uint64_t rem_sec = (uint64_t)rqtp->tv_sec;
  uint32_t rem_nsec = (uint32_t)rqtp->tv_nsec;

  // firebox#MW0 — TIMER_ABSTIME is an *absolute* deadline. Rather than relying
  // on the host's absolute-clock subscription (which treated the deadline as
  // relative — clock_nanosleep/2-1, /3-1 hung), resolve the abstime path in
  // libc: read the current time on the same clock and compute the remaining
  // RELATIVE duration. A deadline already in the past (or now) means no
  // suspension occurs and we return success immediately (clock_nanosleep/3-1).
  // The subscriptions we then issue are always relative, so the host only ever
  // sees the well-behaved relative path (which clock_nanosleep/1-1 exercises).
  if ((flags & TIMER_ABSTIME) != 0) {
    __wasi_timestamp_t now;
    if (__wasi_clock_time_get(id, 1, &now) != 0)
      return EINVAL;
    uint64_t now_sec = now / (uint64_t)NSEC_PER_SEC;
    uint32_t now_nsec = (uint32_t)(now % (uint64_t)NSEC_PER_SEC);
    if (rem_sec < now_sec || (rem_sec == now_sec && rem_nsec <= now_nsec)) {
      // Deadline is in the past: return immediately, no suspension.
      // Still honour a pending cancel (this is a cancellation point).
      if (&__testcancel != 0)
        __testcancel();
      return 0;
    }
    sub_elapsed(&rem_sec, &rem_nsec, now);
  }

  // firebox#NSL — rmtp reports the time remaining when an EINTR cuts the sleep
  // short (POSIX: "if the TIMER_ABSTIME flag is not set ... the timespec
  // referenced by rmtp is updated to contain the amount of time remaining").
  // Elapsed is measured on CLOCK_MONOTONIC — NOT on `id` — so a concurrent
  // clock_settime(CLOCK_REALTIME) can never skew a relative sleep's remainder.
  // rmtp is left untouched for TIMER_ABSTIME and when rmtp is NULL.
  bool want_remaining = rmtp != NULL && (flags & TIMER_ABSTIME) == 0;

  // firebox#BYB — CLAMP AND RE-ARM. A duration longer than one subscription
  // can carry is slept as a sequence of full-width chunks followed by the
  // remainder. Clamping ALONE would be its own faithfulness bug — the guest
  // would wake early and could not tell that from a completed sleep — so the
  // re-arm is the part that makes it honest, and the loop only exits on the
  // FINAL chunk, on EINTR, or on a host error.
  //
  // We deliberately do NOT spell an over-long sleep as the wire's INFINITE
  // encoding (`timeout == 0`, which the host maps to `Duration::MAX`): a
  // finite sleep must still return, even if the guest will not be around to
  // see it. `sleep infinity` converges on the same observable behaviour by
  // asking for ~2.9e11 chunks of 584 years each, which is what "does not
  // return" means for a program.
  while (rem_sec > 0 || rem_nsec > 0) {
    bool final_chunk;
    __wasi_timestamp_t timeout;
    if (rem_sec > MAX_CHUNK_SEC ||
        (rem_sec == MAX_CHUNK_SEC && rem_nsec > MAX_CHUNK_NSEC)) {
      timeout = UINT64_MAX;
      final_chunk = false;
    } else {
      timeout = rem_sec * (uint64_t)NSEC_PER_SEC + rem_nsec;
      final_chunk = true;
    }

    // `timeout` is never 0 here — the loop guard excludes a zero remainder —
    // so we never collide with the wire's `0 == INFINITE` encoding. (1 is the
    // wire's "immediate", and a genuine 1 ns request lands on it correctly.)
    __wasi_subscription_t sub = {
        .u.tag = __WASI_EVENTTYPE_CLOCK,
        .u.u.clock.id = id,
        .u.u.clock.timeout = timeout,
        .u.u.clock.flags = 0,
    };

    __wasi_timestamp_t mono_start = 0;
    bool measured_start =
        want_remaining &&
        __wasi_clock_time_get(__WASI_CLOCKID_MONOTONIC, 1, &mono_start) == 0;

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
    // non-success into — and, for a relative sleep with rmtp != NULL, to store
    // the unslept remainder in rmtp. nanosleep() (our caller) turns this EINTR
    // into errno=EINTR + a -1 return; clock_nanosleep() returns it directly.
    // Without this, every signal-interrupted sleep reported ENOTSUP and left
    // rmtp untouched (Open POSIX nanosleep/7-1,7-2 + clock_nanosleep/9-1,10-1).
    if (error == __WASI_ERRNO_INTR) {
      if (want_remaining) {
        __wasi_timestamp_t elapsed = 0;
        if (measured_start) {
          __wasi_timestamp_t mono_now = mono_start;
          (void)__wasi_clock_time_get(__WASI_CLOCKID_MONOTONIC, 1, &mono_now);
          elapsed = mono_now > mono_start ? mono_now - mono_start : 0;
        }
        // `rem_*` is the remainder as of the START of this chunk, so the
        // report stays correct across a re-arm.
        sub_elapsed(&rem_sec, &rem_nsec, elapsed);
        rmtp->tv_sec = (time_t)rem_sec;
        rmtp->tv_nsec = (long)rem_nsec;
      }
      return EINTR;
    }

    if (error != 0 || ev.error != 0)
      return ENOTSUP;

    if (final_chunk)
      break;

    // Re-arm: retire exactly one full-width chunk from the remainder.
    rem_sec -= MAX_CHUNK_SEC;
    if (rem_nsec < MAX_CHUNK_NSEC) {
      rem_nsec += (uint32_t)NSEC_PER_SEC;
      rem_sec -= 1;
    }
    rem_nsec -= MAX_CHUNK_NSEC;
  }

  // A zero-length request suspends for zero time and falls straight through
  // the loop; it is still a cancellation point.
  if (&__testcancel != 0)
    __testcancel();
  return 0;
}

weak_alias(clock_nanosleep, __clock_nanosleep);
