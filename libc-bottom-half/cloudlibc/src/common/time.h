// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#ifndef COMMON_TIME_H
#define COMMON_TIME_H

#include <common/limits.h>

#include <sys/time.h>

#include <wasi/api.h>
#include <stdbool.h>
#include <time.h>

#define NSEC_PER_SEC 1000000000

static inline bool timespec_to_timestamp_exact(
    const struct timespec *timespec, __wasi_timestamp_t *timestamp) {
  // Invalid nanoseconds field.
  if (timespec->tv_nsec < 0 || timespec->tv_nsec >= NSEC_PER_SEC)
    return false;

  // Timestamps before the Epoch are not supported.
  if (timespec->tv_sec < 0)
    return false;

  // Make sure our timestamp does not overflow.
  return !__builtin_mul_overflow(timespec->tv_sec, NSEC_PER_SEC, timestamp) &&
         !__builtin_add_overflow(*timestamp, timespec->tv_nsec, timestamp);
}

static inline bool timespec_to_timestamp_clamp(
    const struct timespec *timespec, __wasi_timestamp_t *timestamp) {
  // Invalid nanoseconds field.
  if (timespec->tv_nsec < 0 || timespec->tv_nsec >= NSEC_PER_SEC)
    return false;

  // firebox#B28 — this converts a RELATIVE duration (the only caller is
  // pselect.c's timeout subscription), and the previous form collapsed EVERY
  // sub-second duration to "immediate": `tv_sec <= 0` fired for {0, 500000000}
  // just as it did for {0, 0}. MEASURED 2026-08-19 against the shipping
  // sysroot-patched-pic, deterministic 3/3 with two live negative controls:
  // `select(0,NULL,NULL,NULL,&{0,500000})` returned in 0 ms where Linux waits
  // 500 ms, while `select(…,&{1,0})` correctly took 1003 ms and
  // `poll(NULL,0,900)` correctly took 904 ms. So every `select()` with a
  // sub-second timeout — the ordinary event-loop idiom — was a 100%-CPU
  // busy-spin, and the loop still made forward progress, which is why it never
  // surfaced as a hang. Invariant 0: a false success is never faithful.
  //
  // Encode the duration honestly and reserve the two special ABI values:
  //   0 => INFINITE in `poll_oneoff` (host maps it to `Duration::MAX`), so a
  //        genuine zero-length wait must NOT be spelled 0 here.
  //   1 => "immediate" (host maps it to `Duration::ZERO`).
  // A negative `tv_sec` is not a representable duration; POSIX select() calls
  // that EINVAL, but the caller-visible validation lives in select.c, so keep
  // the prior lenient "treat as immediate" rather than widen this helper's
  // contract.
  if (timespec->tv_sec < 0) {
    *timestamp = 1;  // 1 == immediate; 0 would mean INFINITE (firebox#B28)
    return true;
  }
  if (__builtin_mul_overflow(timespec->tv_sec, NSEC_PER_SEC, timestamp) ||
      __builtin_add_overflow(*timestamp, timespec->tv_nsec, timestamp)) {
    // Make sure our timestamp does not overflow.
    *timestamp = NUMERIC_MAX(__wasi_timestamp_t);
  } else if (*timestamp == 0) {
    // A true zero-length wait. Spell it 1 ("immediate"), never 0 ("infinite").
    *timestamp = 1;
  }
  return true;
}

static inline struct timespec timestamp_to_timespec(
    __wasi_timestamp_t timestamp) {
  // Decompose timestamp into seconds and nanoseconds.
  return (struct timespec){.tv_sec = timestamp / NSEC_PER_SEC,
                           .tv_nsec = timestamp % NSEC_PER_SEC};
}

static inline struct timeval timestamp_to_timeval(
    __wasi_timestamp_t timestamp) {
  struct timespec ts = timestamp_to_timespec(timestamp);
  return (struct timeval){.tv_sec = ts.tv_sec, ts.tv_nsec / 1000};
}

#endif
