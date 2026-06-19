#ifndef _WASI_EMULATED_PROCESS_CLOCKS
#define _WASI_EMULATED_PROCESS_CLOCKS
#endif
#include <time.h>
#include <wasi/api.h>
#include <common/time.h>

// firebox#TDX — POSIX/XSI fix CLOCKS_PER_SEC at 1000000, so `clock` ticks are
// microseconds. The WASI monotonic clock is in nanoseconds; the number of
// nanoseconds per clock tick is therefore NSEC_PER_SEC / CLOCKS_PER_SEC
// (== 1000). This assertion guards the assumption that the tick is an integer
// number of nanoseconds (i.e. CLOCKS_PER_SEC divides NSEC_PER_SEC evenly).
#define NSEC_PER_CLOCK_TICK (NSEC_PER_SEC / CLOCKS_PER_SEC)
_Static_assert(
    NSEC_PER_SEC % CLOCKS_PER_SEC == 0,
    "CLOCKS_PER_SEC must divide NSEC_PER_SEC so a tick is whole nanoseconds"
);

// Snapshot of the monotonic clock at the start of the program.
static __wasi_timestamp_t start;

// Use a priority of 10 to run fairly early in the implementation-reserved
// constructor priority range.
__attribute__((constructor(10)))
static void init(void) {
    (void)__wasi_clock_time_get(__WASI_CLOCKID_MONOTONIC, 0, &start);
}

// Define the libc symbol as `__clock` so that we can reliably call it
// from elsewhere in libc.
clock_t __clock(void) {
    // Use `MONOTONIC` instead of `PROCESS_CPUTIME_ID` since WASI doesn't have
    // an inherent concept of a process. Note that this means we'll incorrectly
    // include time from other processes, so this function is only declared by
    // the headers if `_WASI_EMULATED_PROCESS_CLOCKS` is defined.
    __wasi_timestamp_t now = 0;
    (void)__wasi_clock_time_get(__WASI_CLOCKID_MONOTONIC, 0, &now);
    // firebox#TDX — convert elapsed nanoseconds to CLOCKS_PER_SEC ticks
    // (microseconds) so `clock() / CLOCKS_PER_SEC` yields seconds (clock/1-1).
    return (clock_t)((now - start) / NSEC_PER_CLOCK_TICK);
}

// Define a user-visible alias as a weak symbol.
__attribute__((__weak__, __alias__("__clock")))
clock_t clock(void);
