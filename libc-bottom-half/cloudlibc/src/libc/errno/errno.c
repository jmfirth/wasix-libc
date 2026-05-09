// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <assert.h>
#include <wasi/api.h>
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
