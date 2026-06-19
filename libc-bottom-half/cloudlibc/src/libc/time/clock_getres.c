// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <common/clock.h>
#include <common/time.h>

#include <wasi/api.h>
#include <errno.h>
#include <time.h>

int clock_getres(clockid_t clock_id, struct timespec *res) {
  // firebox#79E — reject an unknown clock_id with EINVAL (POSIX) instead of
  // dereferencing it as a pointer-ABI clockid (the old unconditional deref
  // faulted out-of-bounds on bogus integer ids such as 50 / INT32_MIN).
  __wasi_clockid_t id;
  if (!__wasilibc_clockid_from_any_checked((uintptr_t)clock_id, &id)) {
    errno = EINVAL;
    return -1;
  }
  __wasi_timestamp_t ts;
  __wasi_errno_t error = __wasi_clock_res_get(id, &ts);
  if (error != 0) {
    errno = error;
    return -1;
  }
  *res = timestamp_to_timespec(ts);
  return 0;
}
