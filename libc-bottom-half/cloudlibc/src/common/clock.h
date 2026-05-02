// Copyright (c) 2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#ifndef COMMON_CLOCK_H
#define COMMON_CLOCK_H

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

// Firebox: resolve a clockid_t argument to its underlying __wasi_clockid_t.
// Accepts both the integer clockid ABI (wasix-libc's current public
// headers: CLOCK_MONOTONIC == 1) and the legacy cloudlibc pointer ABI
// (wasi-sdk's __header_time.h: CLOCK_MONOTONIC == &_CLOCK_MONOTONIC).
// Valid wasi clockids are 0..3; anything larger is treated as a pointer
// to a struct __clockid. This matters when wasi-sdk-compiled C++ code
// (libc++ chrono) is linked against wasix-libc's libc.a.
//
// The parameter type is uintptr_t (not clockid_t) because this header
// is included before <time.h> in some TUs, so clockid_t is not yet in
// scope. Callers hold a clockid_t or a struct __clockid *; both convert
// cleanly to uintptr_t on wasm32.
static inline __wasi_clockid_t __wasilibc_clockid_from_any(uintptr_t raw) {
  if (raw <= 3) {
    return (__wasi_clockid_t)raw;
  }
  return ((const struct __clockid *)raw)->id;
}

#endif
