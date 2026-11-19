// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <common/clock.h>
#include <common/time.h>

#include <wasi/api.h>
#include <errno.h>
#include <time.h>
#include <wasi/libc.h>
#include <wasi/api_firebox.h>

int __clock_gettime(clockid_t clock_id, struct timespec *tp) {
  uintptr_t raw = (uintptr_t)clock_id;
  __wasi_timestamp_t ts;
  __wasi_errno_t error;
  if (clock_id == CLOCK_BOOTTIME) {
    error = __wasix_clock_boottime_get(0, &ts);
    if (error) {
      errno = __wasilibc_errno_from_wasi(error);
      return -1;
    }
    *tp = timestamp_to_timespec(ts);
    return 0;
  }
  // firebox#79E — reject an unknown clock_id with EINVAL (POSIX) instead of
  // dereferencing it as a pointer-ABI clockid (avoids the out-of-bounds
  // fault on bogus integer ids).
  __wasi_clockid_t id;
  if (__wasilibc_clockid_from_any_checked(raw, &id)) {
    error = __wasi_clock_time_get(id, 1, &ts);
  } else if (__wasilibc_is_named_cpu_clock(raw)) {
    error = __wasilibc_cpu_clock_get((int32_t)raw, 0, &ts);
  } else {
    error = __WASI_ERRNO_INVAL;
  }
  if (error != 0) {
    errno = error == __WASI_ERRNO_SRCH ? EINVAL : __wasilibc_errno_from_wasi(error);
    return -1;
  }
  *tp = timestamp_to_timespec(ts);
  return 0;
}
extern __typeof(__clock_gettime) clock_gettime __attribute__((__weak__, alias("__clock_gettime")));
