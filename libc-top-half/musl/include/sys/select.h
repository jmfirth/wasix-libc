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

#ifdef __wasilibc_unmodified_upstream /* Use alternate WASI libc headers */
typedef unsigned long fd_mask;
#endif

#ifdef __wasilibc_unmodified_upstream /* Use alternate WASI libc headers */
typedef struct {
	unsigned long fds_bits[FD_SETSIZE / 8 / sizeof(long)];
} fd_set;

#define FD_ZERO(s) do { int __i; unsigned long *__b=(s)->fds_bits; for(__i=sizeof (fd_set)/sizeof (long); __i; __i--) *__b++=0; } while(0)
#define FD_SET(d, s)   ((s)->fds_bits[(d)/(8*sizeof(long))] |= (1UL<<((d)%(8*sizeof(long)))))
#define FD_CLR(d, s)   ((s)->fds_bits[(d)/(8*sizeof(long))] &= ~(1UL<<((d)%(8*sizeof(long)))))
#define FD_ISSET(d, s) !!((s)->fds_bits[(d)/(8*sizeof(long))] & (1UL<<((d)%(8*sizeof(long)))))
#else
#include <__fd_set.h>

/*
 * firebox#D7S — `fd_mask` must be visible in the branch we actually compile.
 *
 * Upstream musl declares it above, unconditionally. Ours sat inside
 * `#ifdef __wasilibc_unmodified_upstream`, which nothing defines (MEASURED:
 * 0 define sites across 264 referencing files), so the public name simply did
 * not exist — `fd_mask m;` failed to COMPILE against the shipped sysroot while
 * a control TU using only `fd_set`/`FD_*` compiled fine. That is the quiet
 * second symptom of the same divergence #D7S deletes, and it is invisible to
 * anyone reading the header for text rather than preprocessing it.
 *
 * Derived from `__fd_mask` rather than restated as `unsigned long`: the layout
 * of `fd_set` and the width of its word are ONE fact, and writing it twice is
 * how the two silently drift apart on a profile where `long` is not 8 bytes.
 */
typedef __fd_mask fd_mask;
#endif

int select (int, fd_set *__restrict, fd_set *__restrict, fd_set *__restrict, struct timeval *__restrict);
int pselect (int, fd_set *__restrict, fd_set *__restrict, fd_set *__restrict, const struct timespec *__restrict, const sigset_t *__restrict);

#if defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
/*
 * firebox#D7S — same story as `fd_mask` above: this was gated off entirely
 * rather than merely defined differently, so `NFDBITS` did not exist in the
 * branch we compile. The `_GNU_SOURCE || _BSD_SOURCE` feature test is
 * upstream musl's and is KEPT — only our extra `__wasilibc_unmodified_upstream`
 * gate is deleted, which is the whole shape of this change.
 */
#ifdef __wasilibc_unmodified_upstream /* Use alternate WASI libc headers */
#define NFDBITS (8*(int)sizeof(long))
#else
#define NFDBITS __NFDBITS
#endif
#endif

#if _REDIR_TIME64
__REDIR(select, __select_time64);
__REDIR(pselect, __pselect_time64);
#endif

#ifdef __cplusplus
}
#endif
#endif
