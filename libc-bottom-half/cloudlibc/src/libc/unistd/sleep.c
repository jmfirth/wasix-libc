// Copyright (c) 2015 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <time.h>
#include <unistd.h>

// firebox#SLR — POSIX: on interruption by a signal, sleep() returns "the
// number of seconds remaining in the requested sleep time"; only an
// uninterrupted sleep returns 0.
//
// This used to pass rmtp = NULL and `return seconds` on any failure, which
// reports the FULL requested duration as unslept no matter how long we
// actually slept. MEASURED 2026-08-26 on the differential-corpus member
// `alarm-interrupts-sleep`: alarm(1) against sleep(5) produced
// `alarm_fired=1 remaining=5 elapsed=1s` — the signal DID interrupt the sleep
// (elapsed=1s, handler ran), and the only wrong field was the return value.
//
// ⛔ That row was allowlisted in tests/differential-corpus/expected-divergences.txt
// as "alarm() cannot EINTR a blocking sleep (WASI has no async signal
// delivery)" — an ARCHITECTURAL IMPOSSIBILITY claim. It is false: the
// interrupt half has worked since firebox#NSL wired the EINTR + remainder arm
// in clock_nanosleep(). Passing rmtp is all that was ever needed to reach it;
// with rmtp == NULL that whole arm is dead code from this caller.
//
// Truncating toward zero matches musl's sleep() (`nanosleep(&tv, &tv);
// return tv.tv_sec;`), which is the behaviour our native reference exhibits.
// musl's own unistd/sleep.c is NOT compiled into this sysroot (it is absent
// from LIBC_TOP_HALF_MUSL_SOURCES), so this file is the live implementation.
unsigned int sleep(unsigned int seconds) {
  struct timespec ts = {.tv_sec = seconds, .tv_nsec = 0};
  struct timespec rem = {.tv_sec = 0, .tv_nsec = 0};
  if (clock_nanosleep(CLOCK_REALTIME, 0, &ts, &rem) != 0) {
    // Clamp: a remainder can never exceed what was requested, and a negative
    // or over-large value would be a worse answer than the old one.
    if (rem.tv_sec < 0)
      return 0;
    if ((unsigned long long)rem.tv_sec > (unsigned long long)seconds)
      return seconds;
    return (unsigned int)rem.tv_sec;
  }
  return 0;
}
