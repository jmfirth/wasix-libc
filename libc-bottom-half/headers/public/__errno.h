#ifndef __wasilibc___errno_h
#define __wasilibc___errno_h

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__PIC__) && !defined(__PIE__) && !defined(__WASILIBC_BUILDING_LIBC)
/* Shared-library consumer: import errno via GOT.mem (non-TLS extern).
 * See patches/0020-errno-non-tls-when-pic-shared.patch for full rationale. */
extern int errno;
#elif defined(__FIREBOX_NO_TLS_ERRNO__)
/* firebox#323: static wasix-libc variant — errno is built non-TLS to
 * match consumers (zeroperl, Ruby, coreutils) that compile without
 * -pthread. See errno.c for the full rationale. */
extern int errno;
#else
#ifdef __cplusplus
extern thread_local int errno;
#else
extern _Thread_local int errno;
#endif
#endif

#define errno errno

#ifdef __cplusplus
}
#endif

#endif
