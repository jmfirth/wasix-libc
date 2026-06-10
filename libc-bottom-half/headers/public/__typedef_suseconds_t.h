#ifndef __wasilibc___typedef_suseconds_t_h
#define __wasilibc___typedef_suseconds_t_h

/* Define this to be 64-bit as its main use is in struct timeval where the
   extra space would otherwise be padding.
   firebox#824 axis-1: wasm64 is LP64 (int64_t == long), so suseconds_t is `long`
   (matching musl's `_Int64`-tied suseconds_t); wasm32 keeps `long long`. */
#if defined(__wasm64__)
typedef long suseconds_t;
#else
typedef long long suseconds_t;
#endif

#endif
