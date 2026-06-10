#ifndef __wasilibc___typedef_clock_t_h
#define __wasilibc___typedef_clock_t_h

/* Define this as a 64-bit signed integer to avoid wraparounds.
 * firebox#824 axis-1: wasm64 is LP64, so clock_t is `long` (matching musl's
 * plain-`long` clock_t in <bits/alltypes.h>); wasm32 keeps `long long`. */
#if defined(__wasm64__)
typedef long clock_t;
#else
typedef long long clock_t;
#endif

#endif
