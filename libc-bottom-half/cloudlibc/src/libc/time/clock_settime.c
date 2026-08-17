
#include <common/clock.h>
#include <common/time.h>

#include <wasi/api.h>
#include <errno.h>
#include <stddef.h>
#include <time.h>

// firebox#QQN — defined in libc-top-half/musl/src/time/timer_create.c, which
// owns the POSIX per-process timer manager and its static g_lock/g_cond. WEAK
// so this resolves to NULL in a link that does not pull in the timer machinery;
// the call site tests for NULL before calling.
extern void __fbx_timers_clock_changed(void) __attribute__((__weak__));

int __clock_settime(clockid_t clock_id, const struct timespec *tp) {
  // firebox#79E — resolve + validate the clock_id. An unknown id (bogus
  // integer or stray pointer) is EINVAL, not an out-of-bounds dereference.
  __wasi_clockid_t id;
  if (!__wasilibc_clockid_from_any_checked((uintptr_t)clock_id, &id)) {
    errno = EINVAL;
    return -1;
  }
  // firebox#79E — only CLOCK_REALTIME is settable. POSIX requires EINVAL for
  // a clock that cannot be set; CLOCK_MONOTONIC and the CPU-time clocks are
  // read-only (the runtime accepted CLOCK_MONOTONIC silently, so reject it
  // here — clock_settime/6-1, /20-1).
  if (id != __WASI_CLOCKID_REALTIME) {
    errno = EINVAL;
    return -1;
  }
  __wasi_timestamp_t ts;
  if (!timespec_to_timestamp_exact(tp, &ts)) {
    errno = EINVAL;
    return -1;
  }
  __wasi_errno_t error = __wasi_clock_time_set(id, ts);
  if (error != 0) {
    errno = error;
    return -1;
  }
  // firebox#QQN — CLOCK_REALTIME just moved DISCONTINUOUSLY, so any armed
  // absolute CLOCK_REALTIME timer may now be overdue and POSIX requires it to
  // fire immediately (Linux re-arms REALTIME timers on a clock set). The POSIX
  // timer manager (timer_create.c) parks on a CLOCK_MONOTONIC condvar until its
  // nearest PRE-JUMP expiry and has no other reason to wake, so it must be told.
  //
  // WEAK: a program that never links the timer machinery leaves this NULL and
  // pays nothing -- calling it unconditionally would drag the manager thread and
  // its pthread dependencies into every binary that merely sets the clock.
  //
  // Ordering is deliberate: the wake happens only AFTER the host has accepted
  // the new time, so the manager cannot recompute against the old clock and go
  // back to sleep on a stale deadline.
  if (__fbx_timers_clock_changed != NULL) {
    __fbx_timers_clock_changed();
  }
  return 0;
}
extern __typeof(__clock_settime) clock_settime __attribute__((__weak__, alias("__clock_settime")));
