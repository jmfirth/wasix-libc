// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <common/clock.h>
#include <common/time.h>

#include <wasi/api.h>
#include <errno.h>
#include <time.h>
#include <wasi/libc.h>

int clock_getres(clockid_t clock_id, struct timespec *res) {
  uintptr_t raw = (uintptr_t)clock_id;
  __wasi_timestamp_t ts;
  __wasi_errno_t error;
  // firebox#79E — reject an unknown clock_id with EINVAL (POSIX) instead of
  // dereferencing it as a pointer-ABI clockid (the old unconditional deref
  // faulted out-of-bounds on bogus integer ids such as 50 / INT32_MIN).
  __wasi_clockid_t id;
  if (__wasilibc_clockid_from_any_checked(raw, &id)) {
    error = __wasi_clock_res_get(id, &ts);
  } else if (__wasilibc_is_named_cpu_clock(raw)) {
    error = __wasilibc_cpu_clock_get((int32_t)raw, 1, &ts);
  } else {
    error = __WASI_ERRNO_INVAL;
  }
  if (error != 0) {
    errno = error == __WASI_ERRNO_SRCH ? EINVAL : __wasilibc_errno_from_wasi(error);
    return -1;
  }
  if (res)
    *res = timestamp_to_timespec(ts);
  return 0;
}
