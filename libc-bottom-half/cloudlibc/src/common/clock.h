// Copyright (c) 2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#ifndef COMMON_CLOCK_H
#define COMMON_CLOCK_H

#include <stdbool.h>
#include <stdint.h>
#include <wasi/api.h>

// In this implementation we define clockid_t as a pointer type, so that
// we can implement them as full objects. Right now we only use those
// objects to store the raw ABI-level clock identifier, but in the
// future we can use this to provide support for pthread_getcpuclockid()
// and clock file descriptors.
struct __clockid {
  __wasi_clockid_t id;
};

// The two pointer-ABI clock objects. Under wasi-sdk's __header_time.h the
// macros CLOCK_REALTIME / CLOCK_MONOTONIC expand to &_CLOCK_REALTIME /
// &_CLOCK_MONOTONIC (these are the only two the pointer ABI ever names —
// see expected/wasm32-wasi-threads/defined-symbols.txt). They are declared
// here so __wasilibc_clockid_from_any_checked can recognise a legitimate
// pointer-ABI clockid by ADDRESS, without dereferencing an arbitrary value.
extern const struct __clockid _CLOCK_REALTIME;
extern const struct __clockid _CLOCK_MONOTONIC;

// Firebox: resolve a clockid_t argument to its underlying __wasi_clockid_t,
// VALIDATING it (firebox#79E). Accepts both the integer clockid ABI
// (wasix-libc's current public headers: CLOCK_MONOTONIC == 1) and the
// legacy cloudlibc pointer ABI (wasi-sdk's __header_time.h:
// CLOCK_MONOTONIC == &_CLOCK_MONOTONIC). This matters when wasi-sdk-compiled
// C++ code (libc++ chrono) is linked against wasix-libc's libc.a.
//
// Returns true and writes *out iff `raw` names a known clock; returns false
// (caller must report EINVAL) otherwise. Valid integer clockids are 0..3.
// For raw > 3 we do NOT blindly dereference: an out-of-range integer
// clockid (e.g. 50, INT32_MIN, -1 — all the POSIX clock_getres/6-2 and
// clock_settime/17-2 inputs) sign-/zero-extends to a uintptr_t > 3 and,
// under the old unconditional `((struct __clockid *)raw)->id`, faulted with
// an out-of-bounds memory access instead of EINVAL. We instead accept a
// pointer ONLY when it equals one of the two known clock objects, so a
// bogus integer can never be dereferenced.
//
// The parameter type is uintptr_t (not clockid_t) because this header
// is included before <time.h> in some TUs, so clockid_t is not yet in
// scope. Callers hold a clockid_t or a struct __clockid *; both convert
// cleanly to uintptr_t on wasm32.
static inline bool __wasilibc_clockid_from_any_checked(uintptr_t raw,
                                                       __wasi_clockid_t *out) {
  if (raw <= 3) {
    *out = (__wasi_clockid_t)raw;
    return true;
  }
  if (raw == (uintptr_t)&_CLOCK_REALTIME || raw == (uintptr_t)&_CLOCK_MONOTONIC) {
    *out = ((const struct __clockid *)raw)->id;
    return true;
  }
  return false;
}

// Backward-compatible unchecked resolver (callers that have already
// validated, or that cannot signal an error). Falls back to REALTIME for an
// unrecognised value rather than faulting on an arbitrary dereference.
static inline __wasi_clockid_t __wasilibc_clockid_from_any(uintptr_t raw) {
  __wasi_clockid_t id;
  if (__wasilibc_clockid_from_any_checked(raw, &id))
    return id;
  return __WASI_CLOCKID_REALTIME;
}

#endif
