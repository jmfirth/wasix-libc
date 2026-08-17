#ifndef __wasilibc___typedef_fd_set_h
#define __wasilibc___typedef_fd_set_h

#define __need_size_t
#include <stddef.h>

#include <__macro_FD_SETSIZE.h>

/*
 * fd_set is the POSIX BITMASK, indexed by file-descriptor number (#D7S).
 *
 * It used to be a {size_t __nfds; int __fds[FD_SETSIZE];} count-plus-list. That
 * layout is self-consistent with its own FD_* macros, so every program that
 * touches an fd_set ONLY through the macros worked, and the divergence survived.
 * But fd_set's bitmask layout is OBSERVABLE POSIX ABI, not an implementation
 * detail: perl's `select` builtin takes a caller-built bit vector
 * (`vec($rin, fileno($fh), 1) = 1`) and hands the raw bytes to libc, which is
 * perl's documented semantics and correct on every real POSIX system. Under the
 * list layout those bytes were reinterpreted — the leading bitmask word read as
 * __nfds (a garbage count), the words after it as fd NUMBERS.
 *
 * MEASURED before this change, one run, one fd in each set:
 *   connected TCP socket  select() -> -1/EBADF on a valid fd   (poll(): n=1)
 *   regular file          select() ->  n=0, though POSIX says a regular file
 *                                      is ALWAYS ready
 *   pipe with data ready  select() ->  n=15, from a ONE-fd set
 * n=15 is not a possible count — it is a bit pattern being read as one. The
 * whole IO::Socket / HTTP::Tiny / cpanm chain fails on this, and `curl` was
 * unaffected only because it uses poll().
 *
 * This is NOT a new layout: it is upstream musl's, which sys/select.h already
 * carries verbatim behind `#ifdef __wasilibc_unmodified_upstream`. Invariant 8 —
 * don't diverge gratuitously. Deleting the divergence also restores `fd_mask`
 * and `NFDBITS`, which the same #ifdef gates off and which currently make any
 * program using them fail to COMPILE.
 *
 * Size, since it is a visible consequence: 4100 bytes -> 128 at FD_SETSIZE 1024.
 */
typedef unsigned long __fd_mask;

#define __NFDBITS (8 * (int)sizeof(__fd_mask))

typedef struct {
    __fd_mask __fds_bits[FD_SETSIZE / __NFDBITS];
} fd_set;

#endif
