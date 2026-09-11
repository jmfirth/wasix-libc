// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <assert.h>
#include <wasi/api.h>
#include <wasi/libc.h>
#include <errno.h>
#include <threads.h>


// firebox#87F: the assertions, aimed at the translation.
//
// 78 assertions used to stand above this block asserting that each E* IS its
// WASI number. That is the property the renumbering deleted on purpose, and
// they were removed in the same commit that deleted it, by the same hand, so
// that the mapping was never briefly unchecked: the rows below landed first,
// green alongside the identity block while both were true, and became the
// load-bearing set the instant the numbers moved.
//
// They assert that running a WASI errno through the translation lands on the
// E* the guest actually uses. Before the flip that was the identity check
// pointed at the thing that would still be true; after it, it is the only
// statement of the mapping that a compiler can check.
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
