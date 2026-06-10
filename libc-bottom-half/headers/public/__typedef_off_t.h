#ifndef __wasilibc___typedef_off_t_h
#define __wasilibc___typedef_off_t_h

/* Define these as 64-bit signed integers to support files larger than 2 GiB.
 * firebox#824 axis-1: on wasm64 (LP64) `long` IS 64-bit and int64_t == long, so
 * off_t is `long` — matching real Linux x86_64 LP64 and musl's `_Int64`-tied
 * off_t in <bits/alltypes.h> (else a typedef redefinition long vs long long).
 * wasm32 (ILP32) keeps `long long` (its `long` is 32-bit). */
#if defined(__wasm64__)
typedef long off_t;
#else
typedef long long off_t;
#endif

#endif
