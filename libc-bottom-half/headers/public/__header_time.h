#ifndef __wasilibc___header_time_h
#define __wasilibc___header_time_h

#define __need_size_t
#define __need_NULL
#include <stddef.h>

#include <__typedef_time_t.h>
#include <__struct_timespec.h>
#include <__struct_tm.h>
#include <__typedef_clockid_t.h>

#include <wasi/api.h>

#define TIMER_ABSTIME __WASI_SUBCLOCKFLAGS_SUBSCRIPTION_CLOCK_ABSTIME

#define CLOCK_MONOTONIC (__WASI_CLOCKID_MONOTONIC)
#define CLOCK_PROCESS_CPUTIME_ID (__WASI_CLOCKID_PROCESS_CPUTIME_ID)
#define CLOCK_REALTIME (__WASI_CLOCKID_REALTIME)
#define CLOCK_THREAD_CPUTIME_ID (__WASI_CLOCKID_THREAD_CPUTIME_ID)

/*
 * TIME_UTC is the only standardized time base value.
 */
#define TIME_UTC 1

/*
 * firebox#TDX — POSIX/XSI mandate CLOCKS_PER_SEC == 1000000 (clock/2-1).
 * The historical wasix-libc value of 1000000000 made clock() report in
 * nanoseconds and broke the standardized unit contract; clock() now returns
 * microseconds to match (see libc-bottom-half/clocks/clock.c and
 * libc-top-half/musl/src/time/clock.c, which already used 1e6).
 */
#define CLOCKS_PER_SEC ((clock_t)1000000)

#endif
