#ifndef _FEATURES_H
#define _FEATURES_H

#if defined(_ALL_SOURCE) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif

#if defined(_DEFAULT_SOURCE) && !defined(_BSD_SOURCE)
#define _BSD_SOURCE 1
#endif

#if !defined(_POSIX_SOURCE) && !defined(_POSIX_C_SOURCE) \
 && !defined(_XOPEN_SOURCE) && !defined(_GNU_SOURCE) \
 && !defined(_BSD_SOURCE) && !defined(__STRICT_ANSI__)
#define _BSD_SOURCE 1
#define _XOPEN_SOURCE 700
#endif

#if __STDC_VERSION__ >= 199901L
#define __restrict restrict
#elif !defined(__GNUC__)
#define __restrict
#endif

#if __STDC_VERSION__ >= 199901L || defined(__cplusplus)
#define __inline inline
#elif !defined(__GNUC__)
#define __inline
#endif

#if __STDC_VERSION__ >= 201112L
#elif defined(__GNUC__)
#define _Noreturn __attribute__((__noreturn__))
#else
#define _Noreturn
#endif

#define __REDIR(x,y) __typeof__(x) x __asm__(#y)

/* firebox#5X0 — thread-local storage on a profile with exactly one thread.
 *
 * MEASURED, not assumed: a `-shared --experimental-pic` link WITHOUT
 * `--shared-memory` gives you no TLS layout at all. wasm-ld emits `__tls_base`
 * as an IMMUTABLE global with init 0, never relocates it (`__wasm_apply_global_
 * relocs` does not touch it), emits no `.tdata` segment (all our TLS is
 * zero-init, i.e. `.tbss`), and synthesizes neither `__tls_size` nor
 * `__tls_align` — those exist only under `--shared-memory`. So a loader has
 * nothing to read: there is no tdata offset for `__tls_base = memory_base +
 * offset` to be computed from, and the one `global.set __tls_base` left in the
 * module makes it fail `wasm-tools validate` outright (firebox#3EC).
 *
 * The resolution is not to reconstruct the missing layout — it is to notice
 * that on a single-threaded profile there is nothing for TLS to mean. One
 * thread has one TLS block, for the whole life of the process, which is the
 * definition of a plain global. Degenerating the storage class removes the
 * bootstrap requirement at its root rather than working around its absence.
 *
 * This is the same move `__FIREBOX_NO_TLS_ERRNO__` already makes for `errno`
 * (firebox#323) — same profile, same reasoning, and the two markers are set
 * together by the nothreads libc build.
 *
 * SCOPE: libc's own thread-locals. A CONSUMER that declares `_Thread_local`
 * still emits a TLS symbol and still has no base to resolve it against; that
 * remains open. */
#ifdef __FIREBOX_NO_THREADS__
#define __FBX_THREAD_LOCAL
#else
#define __FBX_THREAD_LOCAL _Thread_local
#endif

#endif
