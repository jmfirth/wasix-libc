#ifndef _SYS_SELECT_H
#define _SYS_SELECT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#define __NEED_size_t
#define __NEED_time_t
#define __NEED_suseconds_t
#define __NEED_struct_timeval
#define __NEED_struct_timespec
#define __NEED_sigset_t

#include <bits/alltypes.h>

#define FD_SETSIZE 1024

/* firebox#D7S: fd_set is the POSIX/Linux bitmask -- bit (fd % NFDBITS) of word
 * (fd / NFDBITS) -- exactly as upstream musl defines it. The layout is
 * observable ABI: perl's select builtin, memcpy'd sets and direct fds_bits
 * access all hand libc raw bits. wasi-libc replaced it with cloudlibc's
 * {size_t __nfds; int __fds[FD_SETSIZE]} list, which misread every such caller
 * (a one-fd perl set came back n=15/n=128, and a set with bit 9 or 10 in its
 * first word read past the object). The list layout survives only as the
 * legacy ABI behind the unversioned `select`/`pselect` link names; see the
 * redirect below and cloudlibc/src/libc/sys/select/select_legacy.c. */
typedef unsigned long fd_mask;

typedef struct {
	unsigned long fds_bits[FD_SETSIZE / 8 / sizeof(long)];
} fd_set;

#define FD_ZERO(s) do { int __i; unsigned long *__b=(s)->fds_bits; for(__i=sizeof (fd_set)/sizeof (long); __i; __i--) *__b++=0; } while(0)
#define FD_SET(d, s)   ((s)->fds_bits[(d)/(8*sizeof(long))] |= (1UL<<((d)%(8*sizeof(long)))))
#define FD_CLR(d, s)   ((s)->fds_bits[(d)/(8*sizeof(long))] &= ~(1UL<<((d)%(8*sizeof(long)))))
#define FD_ISSET(d, s) !!((s)->fds_bits[(d)/(8*sizeof(long))] & (1UL<<((d)%(8*sizeof(long)))))

int select (int, fd_set *__restrict, fd_set *__restrict, fd_set *__restrict, struct timeval *__restrict);
int pselect (int, fd_set *__restrict, fd_set *__restrict, fd_set *__restrict, const struct timespec *__restrict, const sigset_t *__restrict);

#if defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
#define NFDBITS (8*(int)sizeof(long))
#endif

#ifndef __wasilibc_unmodified_upstream
/* firebox#D7S: bind the bitmask ABI to NEW link names. The unversioned
 * `select`/`pselect` symbols stay the legacy {count,list} ABI that every
 * pre-D7S object, every pre-D7S dynamic consumer (which imports env.select /
 * env.pselect from its libc provider) and the Rust libc crate's wasi fd_set
 * were built against. So each caller reaches the decoder for the layout it was
 * compiled with: a mixed link or a stale consumer against a new provider is
 * CORRECT, never a silent reinterpretation. Same mechanism as musl's time64
 * transition (__select_time64 below). */
__REDIR(select, __select_fdmask);
__REDIR(pselect, __pselect_fdmask);
#endif

#if _REDIR_TIME64
__REDIR(select, __select_time64);
__REDIR(pselect, __pselect_time64);
#endif

#ifdef __cplusplus
}
#endif
#endif
