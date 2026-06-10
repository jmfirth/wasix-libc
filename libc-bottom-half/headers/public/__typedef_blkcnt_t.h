#ifndef __wasilibc___typedef_blkcnt_t_h
#define __wasilibc___typedef_blkcnt_t_h

/* Define these as 64-bit signed integers to support files larger than 2 GiB.
 * firebox#824 axis-1: wasm64 is LP64 (int64_t == long), so blkcnt_t is `long`
 * (matching musl's `_Int64`-tied blkcnt_t); wasm32 keeps `long long`. */
#if defined(__wasm64__)
typedef long blkcnt_t;
#else
typedef long long blkcnt_t;
#endif

#endif
