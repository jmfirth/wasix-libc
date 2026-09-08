// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <assert.h>
#include <wasi/api.h>
#include <wasi/libc.h>
#include <errno.h>
#include <threads.h>

static_assert(E2BIG == __WASI_ERRNO_2BIG, "Value mismatch");
static_assert(EACCES == __WASI_ERRNO_ACCES, "Value mismatch");
static_assert(EADDRINUSE == __WASI_ERRNO_ADDRINUSE, "Value mismatch");
static_assert(EADDRNOTAVAIL == __WASI_ERRNO_ADDRNOTAVAIL, "Value mismatch");
static_assert(EAFNOSUPPORT == __WASI_ERRNO_AFNOSUPPORT, "Value mismatch");
static_assert(EAGAIN == __WASI_ERRNO_AGAIN, "Value mismatch");
static_assert(EALREADY == __WASI_ERRNO_ALREADY, "Value mismatch");
static_assert(EBADF == __WASI_ERRNO_BADF, "Value mismatch");
static_assert(EBADMSG == __WASI_ERRNO_BADMSG, "Value mismatch");
static_assert(EBUSY == __WASI_ERRNO_BUSY, "Value mismatch");
static_assert(ECANCELED == __WASI_ERRNO_CANCELED, "Value mismatch");
static_assert(ECHILD == __WASI_ERRNO_CHILD, "Value mismatch");
static_assert(ECONNABORTED == __WASI_ERRNO_CONNABORTED, "Value mismatch");
static_assert(ECONNREFUSED == __WASI_ERRNO_CONNREFUSED, "Value mismatch");
static_assert(ECONNRESET == __WASI_ERRNO_CONNRESET, "Value mismatch");
static_assert(EDEADLK == __WASI_ERRNO_DEADLK, "Value mismatch");
static_assert(EDESTADDRREQ == __WASI_ERRNO_DESTADDRREQ, "Value mismatch");
static_assert(EDOM == __WASI_ERRNO_DOM, "Value mismatch");
static_assert(EDQUOT == __WASI_ERRNO_DQUOT, "Value mismatch");
static_assert(EEXIST == __WASI_ERRNO_EXIST, "Value mismatch");
static_assert(EFAULT == __WASI_ERRNO_FAULT, "Value mismatch");
static_assert(EFBIG == __WASI_ERRNO_FBIG, "Value mismatch");
static_assert(EHOSTUNREACH == __WASI_ERRNO_HOSTUNREACH, "Value mismatch");
static_assert(EIDRM == __WASI_ERRNO_IDRM, "Value mismatch");
static_assert(EILSEQ == __WASI_ERRNO_ILSEQ, "Value mismatch");
static_assert(EINPROGRESS == __WASI_ERRNO_INPROGRESS, "Value mismatch");
static_assert(EINTR == __WASI_ERRNO_INTR, "Value mismatch");
static_assert(EINVAL == __WASI_ERRNO_INVAL, "Value mismatch");
static_assert(EIO == __WASI_ERRNO_IO, "Value mismatch");
static_assert(EISCONN == __WASI_ERRNO_ISCONN, "Value mismatch");
static_assert(EISDIR == __WASI_ERRNO_ISDIR, "Value mismatch");
static_assert(ELOOP == __WASI_ERRNO_LOOP, "Value mismatch");
static_assert(EMFILE == __WASI_ERRNO_MFILE, "Value mismatch");
static_assert(EMLINK == __WASI_ERRNO_MLINK, "Value mismatch");
static_assert(EMSGSIZE == __WASI_ERRNO_MSGSIZE, "Value mismatch");
static_assert(EMULTIHOP == __WASI_ERRNO_MULTIHOP, "Value mismatch");
static_assert(ENAMETOOLONG == __WASI_ERRNO_NAMETOOLONG, "Value mismatch");
static_assert(ENETDOWN == __WASI_ERRNO_NETDOWN, "Value mismatch");
static_assert(ENETRESET == __WASI_ERRNO_NETRESET, "Value mismatch");
static_assert(ENETUNREACH == __WASI_ERRNO_NETUNREACH, "Value mismatch");
static_assert(ENFILE == __WASI_ERRNO_NFILE, "Value mismatch");
static_assert(ENOBUFS == __WASI_ERRNO_NOBUFS, "Value mismatch");
static_assert(ENODEV == __WASI_ERRNO_NODEV, "Value mismatch");
static_assert(ENOENT == __WASI_ERRNO_NOENT, "Value mismatch");
static_assert(ENOEXEC == __WASI_ERRNO_NOEXEC, "Value mismatch");
static_assert(ENOLCK == __WASI_ERRNO_NOLCK, "Value mismatch");
static_assert(ENOLINK == __WASI_ERRNO_NOLINK, "Value mismatch");
static_assert(ENOMEM == __WASI_ERRNO_NOMEM, "Value mismatch");
static_assert(ENOMSG == __WASI_ERRNO_NOMSG, "Value mismatch");
static_assert(ENOPROTOOPT == __WASI_ERRNO_NOPROTOOPT, "Value mismatch");
static_assert(ENOSPC == __WASI_ERRNO_NOSPC, "Value mismatch");
static_assert(ENOSYS == __WASI_ERRNO_NOSYS, "Value mismatch");
static_assert(ENOTCAPABLE == __WASI_ERRNO_NOTCAPABLE, "Value mismatch");
static_assert(ENOTCONN == __WASI_ERRNO_NOTCONN, "Value mismatch");
static_assert(ENOTDIR == __WASI_ERRNO_NOTDIR, "Value mismatch");
static_assert(ENOTEMPTY == __WASI_ERRNO_NOTEMPTY, "Value mismatch");
static_assert(ENOTRECOVERABLE == __WASI_ERRNO_NOTRECOVERABLE, "Value mismatch");
static_assert(ENOTSOCK == __WASI_ERRNO_NOTSOCK, "Value mismatch");
static_assert(ENOTSUP == __WASI_ERRNO_NOTSUP, "Value mismatch");
static_assert(ENOTTY == __WASI_ERRNO_NOTTY, "Value mismatch");
static_assert(ENXIO == __WASI_ERRNO_NXIO, "Value mismatch");
static_assert(EOVERFLOW == __WASI_ERRNO_OVERFLOW, "Value mismatch");
static_assert(EOWNERDEAD == __WASI_ERRNO_OWNERDEAD, "Value mismatch");
static_assert(EPERM == __WASI_ERRNO_PERM, "Value mismatch");
static_assert(EPIPE == __WASI_ERRNO_PIPE, "Value mismatch");
static_assert(EPROTO == __WASI_ERRNO_PROTO, "Value mismatch");
static_assert(EPROTONOSUPPORT == __WASI_ERRNO_PROTONOSUPPORT, "Value mismatch");
static_assert(EPROTOTYPE == __WASI_ERRNO_PROTOTYPE, "Value mismatch");
static_assert(ERANGE == __WASI_ERRNO_RANGE, "Value mismatch");
static_assert(EROFS == __WASI_ERRNO_ROFS, "Value mismatch");
static_assert(ESPIPE == __WASI_ERRNO_SPIPE, "Value mismatch");
static_assert(ESRCH == __WASI_ERRNO_SRCH, "Value mismatch");
static_assert(ESTALE == __WASI_ERRNO_STALE, "Value mismatch");
static_assert(ETIMEDOUT == __WASI_ERRNO_TIMEDOUT, "Value mismatch");
static_assert(ETXTBSY == __WASI_ERRNO_TXTBSY, "Value mismatch");
static_assert(EXDEV == __WASI_ERRNO_XDEV, "Value mismatch");
static_assert(EMEMVIOLATION == __WASI_ERRNO_MEMVIOLATION, "Value mismatch");
static_assert(EUNKNOWN == __WASI_ERRNO_UNKNOWN, "Value mismatch");

// firebox#87F: the same assertions, re-aimed at the translation.
//
// The 78 above assert that each E* IS its WASI number. Those are the property
// that is about to be deleted on purpose: once E* is renumbered onto Linux
// uapi they all fail, and they will be removed in that same step, by the same
// hand, so that nothing is briefly unchecked.
//
// The ones below assert what has to hold afterwards -- that running a WASI
// errno through the translation lands on the E* the guest actually uses. They
// are not a second copy of the identity check; they are the identity check
// pointed at the thing that will still be true. Today, with every E* still
// #defined to its WASI counterpart, both sets are green simultaneously, which
// is exactly what makes this step safe to land on its own: the map cannot be
// wrong in a way that nothing catches, and nothing observable changes.
//
// Written out rather than generated from __FBX_ERRNO_MAP, which is what you
// would reach for first and what does not work: __FBX_E_FROM_WASI expands the
// map, so driving these rows through the map too would be a recursive use and
// the preprocessor leaves the inner __FBX_ERRNO_MAP unexpanded (blue paint).
// Measured, not assumed -- the generated form fails to compile with "use of
// undeclared identifier __FBX_ERRNO_TERNARY_ROW". The runtime table below has
// no such problem and IS generated from the map. Drift here is not silent
// either way: a row that stops matching stops compiling.
//
// This covers all 80 rows, including ESHUTDOWN and EPENDING, which the
// hand-written identity block above happens to omit.
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_2BIG)           == E2BIG, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_ACCES)          == EACCES, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_ADDRINUSE)      == EADDRINUSE, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_ADDRNOTAVAIL)   == EADDRNOTAVAIL, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_AFNOSUPPORT)    == EAFNOSUPPORT, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_AGAIN)          == EAGAIN, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_ALREADY)        == EALREADY, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_BADF)           == EBADF, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_BADMSG)         == EBADMSG, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_BUSY)           == EBUSY, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_CANCELED)       == ECANCELED, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_CHILD)          == ECHILD, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_CONNABORTED)    == ECONNABORTED, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_CONNREFUSED)    == ECONNREFUSED, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_CONNRESET)      == ECONNRESET, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_DEADLK)         == EDEADLK, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_DESTADDRREQ)    == EDESTADDRREQ, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_DOM)            == EDOM, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_DQUOT)          == EDQUOT, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_EXIST)          == EEXIST, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_FAULT)          == EFAULT, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_FBIG)           == EFBIG, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_HOSTUNREACH)    == EHOSTUNREACH, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_IDRM)           == EIDRM, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_ILSEQ)          == EILSEQ, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_INPROGRESS)     == EINPROGRESS, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_INTR)           == EINTR, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_INVAL)          == EINVAL, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_IO)             == EIO, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_ISCONN)         == EISCONN, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_ISDIR)          == EISDIR, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_LOOP)           == ELOOP, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_MFILE)          == EMFILE, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_MLINK)          == EMLINK, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_MSGSIZE)        == EMSGSIZE, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_MULTIHOP)       == EMULTIHOP, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_NAMETOOLONG)    == ENAMETOOLONG, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_NETDOWN)        == ENETDOWN, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_NETRESET)       == ENETRESET, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_NETUNREACH)     == ENETUNREACH, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_NFILE)          == ENFILE, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_NOBUFS)         == ENOBUFS, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_NODEV)          == ENODEV, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_NOENT)          == ENOENT, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_NOEXEC)         == ENOEXEC, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_NOLCK)          == ENOLCK, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_NOLINK)         == ENOLINK, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_NOMEM)          == ENOMEM, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_NOMSG)          == ENOMSG, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_NOPROTOOPT)     == ENOPROTOOPT, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_NOSPC)          == ENOSPC, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_NOSYS)          == ENOSYS, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_NOTCONN)        == ENOTCONN, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_NOTDIR)         == ENOTDIR, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_NOTEMPTY)       == ENOTEMPTY, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_NOTRECOVERABLE) == ENOTRECOVERABLE, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_NOTSOCK)        == ENOTSOCK, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_NOTSUP)         == ENOTSUP, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_NOTTY)          == ENOTTY, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_NXIO)           == ENXIO, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_OVERFLOW)       == EOVERFLOW, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_OWNERDEAD)      == EOWNERDEAD, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_PERM)           == EPERM, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_PIPE)           == EPIPE, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_PROTO)          == EPROTO, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_PROTONOSUPPORT) == EPROTONOSUPPORT, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_PROTOTYPE)      == EPROTOTYPE, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_RANGE)          == ERANGE, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_ROFS)           == EROFS, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_SPIPE)          == ESPIPE, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_SRCH)           == ESRCH, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_STALE)          == ESTALE, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_TIMEDOUT)       == ETIMEDOUT, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_TXTBSY)         == ETXTBSY, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_XDEV)           == EXDEV, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_NOTCAPABLE)     == ENOTCAPABLE, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_SHUTDOWN)       == ESHUTDOWN, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_MEMVIOLATION)   == EMEMVIOLATION, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_UNKNOWN)        == EUNKNOWN, "Map mismatch");
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_PENDING)        == EPENDING, "Map mismatch");

// Success is not an error, and anything the map does not cover is not
// guessable. Asserted rather than commented so the boundary behaviour is a
// checked property of the macro and not just of the table below.
static_assert(__FBX_E_FROM_WASI(__WASI_ERRNO_SUCCESS) == 0, "Success is not an errno");
static_assert(__FBX_E_FROM_WASI(__FBX_ERRNO_WASI_MAX + 1) == EUNKNOWN, "Out of range must be EUNKNOWN");
static_assert(__FBX_E_FROM_WASI(0xFFFF) == EUNKNOWN, "Out of range must be EUNKNOWN");

// firebox#323: gate errno's TLS lowering. The static wasix-libc variant
// (target wasm32-wasi) is built into a sysroot whose consumers (zeroperl,
// Ruby cross-compile, coreutils) compile WITHOUT `-pthread`. Without
// `-pthread`, an `extern _Thread_local int errno;` declaration in those
// consumers lowers to a non-TLS R_WASM_MEMORY_ADDR_LEB relocation at
// absolute address 0, while libc's own errno.o (built WITH `-pthread` and
// `-ftls-model=local-exec`) places the symbol in a .tbss segment at the
// TLS-relative virtual address (typically 65536). Split-brain: libc writes
// errno into the TLS slot, consumer reads zero from absolute 0. Concrete
// failure: Ruby's `Dir.glob("/missing/*")` gets ENOENT from the syscall,
// libc stores it via TLS errno, but Ruby's `rb_sys_fail` reads errno=0 and
// raises `Errno::NOERROR`. Gating on `__FIREBOX_NO_TLS_ERRNO__` (defined by
// scripts/wasix-libc/build.sh for the static variant only) denatures the
// TLS lowering at the libc side so both sides agree on a plain global.
// The threaded variant (wasm32-wasi-threads) leaves this off — its
// consumers DO compile with `-pthread` and correctly expect TLS errno.
#ifdef __FIREBOX_NO_TLS_ERRNO__
int errno = 0;
#else
thread_local int errno = 0;
#endif

// These values are used by reference-sysroot's dlmalloc.
const int __EINVAL = EINVAL;
const int __ENOMEM = ENOMEM;

// firebox#87F: the runtime half of the map.
//
// A table, not a switch. The WASI errno space is dense over
// [0, __FBX_ERRNO_WASI_MAX], so this is 81 bytes of passive data plus one
// bounds compare and one i32.load8_u per translation. A switch would lower to
// br_table: 81 four-byte entries plus scaffolding living in the *code*
// section, on the instruction path of every syscall wrapper that touches
// errno, reached through an indirect branch. The table is also the form that
// cannot drift from the assertions above, because it is not a second
// transcription of the mapping -- it is the same __FBX_ERRNO_MAP rows in
// initializer position, and a row whose index left the covered range would be
// an out-of-bounds designator, which is a hard compile error rather than a
// silently wrong entry.
//
// unsigned char rather than int: every value the map can produce fits in a
// byte both now (WASI numbering, 1..80) and after the renumbering (Linux
// uapi, 1..133, with the four host-only codes placed above 133 but still well
// under 256), and the whole table is one wasm data segment either way.
#define __FBX_ERRNO_TABLE_ROW(_unused, __w, __e) [__w] = (unsigned char)(__e),
static const unsigned char __fbx_errno_from_wasi_table[__FBX_ERRNO_WASI_MAX + 1] = {
    // Not a map row: SUCCESS has no E* name. Translating it yields 0, which
    // is what __FBX_E_FROM_WASI does too -- see the note on that macro.
    [__WASI_ERRNO_SUCCESS] = 0,
    __FBX_ERRNO_MAP(__FBX_ERRNO_TABLE_ROW, _)
};
#undef __FBX_ERRNO_TABLE_ROW

int __wasilibc_errno_from_wasi(__wasi_errno_t code) {
  // __wasi_errno_t is unsigned, so the upper bound is the only one there is.
  // A code past the end of the map means the host knows an error this libc
  // does not; EUNKNOWN says so honestly rather than handing the call site a
  // number that will read as some unrelated errno.
  if ((unsigned)code > (unsigned)__FBX_ERRNO_WASI_MAX)
    return EUNKNOWN;
  return (int)__fbx_errno_from_wasi_table[code];
}
