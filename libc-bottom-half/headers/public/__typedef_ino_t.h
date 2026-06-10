#ifndef __wasilibc___typedef_ino_t_h
#define __wasilibc___typedef_ino_t_h

/* Define these as 64-bit integers to support billions of inodes.
 * firebox#824 axis-1: wasm64 is LP64 (uint64_t == unsigned long), so ino_t is
 * `unsigned long` (matching musl's `unsigned _Int64` ino_t); wasm32 keeps
 * `unsigned long long`. */
#if defined(__wasm64__)
typedef unsigned long ino_t;
#else
typedef unsigned long long ino_t;
#endif

#endif
