
#include <common/clock.h>
#include <common/time.h>

#include <wasi/api.h>
#include <errno.h>
#include <time.h>

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
  return 0;
}
extern __typeof(__clock_settime) clock_settime __attribute__((__weak__, alias("__clock_settime")));
