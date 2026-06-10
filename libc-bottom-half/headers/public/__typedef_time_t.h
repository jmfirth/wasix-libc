#ifndef __wasilibc___typedef_time_t_h
#define __wasilibc___typedef_time_t_h

/* Define this as a 64-bit signed integer to avoid the 2038 bug.
 * firebox#824 axis-1: wasm64 is LP64 (int64_t == long), so time_t is `long`
 * (matching x86_64 Linux + musl's `_Int64`-tied time_t); wasm32 keeps `long long`. */
#if defined(__wasm64__)
typedef long time_t;
#else
typedef long long time_t;
#endif

#endif
