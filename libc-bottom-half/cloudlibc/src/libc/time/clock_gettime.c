// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <common/clock.h>
#include <common/time.h>

#include <wasi/api.h>
#include <errno.h>
#include <time.h>

int __clock_gettime(clockid_t clock_id, struct timespec *tp) {
  // firebox#79E — reject an unknown clock_id with EINVAL (POSIX) instead of
  // dereferencing it as a pointer-ABI clockid (avoids the out-of-bounds
  // fault on bogus integer ids).
  __wasi_clockid_t id;
  if (!__wasilibc_clockid_from_any_checked((uintptr_t)clock_id, &id)) {
    errno = EINVAL;
    return -1;
  }
  __wasi_timestamp_t ts;
  __wasi_errno_t error = __wasi_clock_time_get(id, 1, &ts);
  if (error != 0) {
    errno = error;
    return -1;
  }
  *tp = timestamp_to_timespec(ts);
  return 0;
}
extern __typeof(__clock_gettime) clock_gettime __attribute__((__weak__, alias("__clock_gettime")));
