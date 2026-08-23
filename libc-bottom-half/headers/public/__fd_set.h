#ifndef __wasilibc___fd_set_h
#define __wasilibc___fd_set_h

#include <__typedef_fd_set.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Bitmask FD_* macros (#D7S) — see __typedef_fd_set.h for why the layout
 * changed. These are upstream musl's, byte-for-byte in behaviour; sys/select.h
 * already carries the same definitions behind `#ifdef
 * __wasilibc_unmodified_upstream`.
 *
 * ⚠️ An out-of-range fd is UNDEFINED in POSIX and was silently absorbed by the
 * old list layout (FD_SET simply appended). Here it would index past the array,
 * so the bounds test is explicit: callers that pass fd >= FD_SETSIZE get a
 * no-op / false rather than a stray write. That is a deliberate difference from
 * upstream musl, which does not bounds-check — an unchecked write here would be
 * a silent memory corruption, and this libc has no way to warn.
 */

static __inline void FD_CLR(int __fd, fd_set *__set) {
    if ((unsigned int)__fd < (unsigned int)FD_SETSIZE)
        __set->__fds_bits[__fd / __NFDBITS] &=
            ~((__fd_mask)1 << (__fd % __NFDBITS));
}

static __inline
#ifdef __cplusplus
bool
#else
_Bool
#endif
FD_ISSET(int __fd, const fd_set *__set)
{
    if ((unsigned int)__fd >= (unsigned int)FD_SETSIZE)
        return 0;
    return (__set->__fds_bits[__fd / __NFDBITS] &
            ((__fd_mask)1 << (__fd % __NFDBITS))) != 0;
}

static __inline void FD_SET(int __fd, fd_set *__set) {
    if ((unsigned int)__fd < (unsigned int)FD_SETSIZE)
        __set->__fds_bits[__fd / __NFDBITS] |=
            ((__fd_mask)1 << (__fd % __NFDBITS));
}

static __inline void FD_ZERO(fd_set *__set) {
    unsigned int __i;
    for (__i = 0; __i < (unsigned int)(FD_SETSIZE / __NFDBITS); ++__i)
        __set->__fds_bits[__i] = 0;
}

static __inline void FD_COPY(const fd_set *__restrict __from,
                             fd_set *__restrict __to) {
    __builtin_memcpy(__to->__fds_bits, __from->__fds_bits,
                     sizeof(__to->__fds_bits));
}

#define FD_CLR(fd, set)   (FD_CLR((fd), (set)))
#define FD_ISSET(fd, set) (FD_ISSET((fd), (set)))
#define FD_SET(fd, set)   (FD_SET((fd), (set)))
#define FD_ZERO(set)      (FD_ZERO((set)))
#define FD_COPY(from, to) (FD_COPY((from), (to)))

#ifdef __cplusplus
}
#endif

#endif
