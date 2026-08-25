// Copyright (c) 2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

/* firebox#YWV: RESTORED from upstream b0555fc ("Delete several blocks of
 * unused code.", 2022-07-20), which dropped _CLOCK_PROCESS_CPUTIME_ID and its
 * sibling as dead weight. They are only "unused" in a STATIC, non-PIC link:
 * --gc-sections drops the address-taken-but-never-read constants, so the
 * absence is invisible. Under -C relocation-model=pic the SAME address-take
 * becomes a GOT.mem DATA import, and GOT.mem data imports resolve EAGERLY at
 * instantiate -- so a PIC guest (every Rust guest, whose libc crate takes the
 * address of all four clock ids to build its CLOCK_* constants) dies BEFORE
 * _start with "Unresolved global GOT.mem._CLOCK_PROCESS_CPUTIME_ID / Missing
 * export". A missing symbol is a load-time death, not a call-time errno
 * (firebox invariant 0), and POSIX has this clock (invariant 2).
 *
 * This is NOT a fabricated clock: __WASI_CLOCKID_PROCESS_CPUTIME_ID == 2 is
 * a real WASI clock id that clock_gettime() already accepts on the INTEGER ABI and
 * forwards to the host. Restoring the object only lets the POINTER ABI
 * name the clock the integer ABI could already name.
 *
 * RETIRES when upstream wasi-libc restores these two objects (or when the
 * pointer-ABI clockid compat surface is removed outright and no consumer
 * takes these addresses).
 */

#include <common/clock.h>

#include <wasi/api.h>
#include <time.h>

const struct __clockid _CLOCK_PROCESS_CPUTIME_ID = {
    .id = __WASI_CLOCKID_PROCESS_CPUTIME_ID,
};
