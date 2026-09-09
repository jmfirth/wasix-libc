#ifndef __wasilibc___errno_values_h
#define __wasilibc___errno_values_h

#include <wasi/api.h>

#define E2BIG __WASI_ERRNO_2BIG
#define EACCES __WASI_ERRNO_ACCES
#define EADDRINUSE __WASI_ERRNO_ADDRINUSE
#define EADDRNOTAVAIL __WASI_ERRNO_ADDRNOTAVAIL
#define EAFNOSUPPORT __WASI_ERRNO_AFNOSUPPORT
#define EAGAIN __WASI_ERRNO_AGAIN
#define EALREADY __WASI_ERRNO_ALREADY
#define EBADF __WASI_ERRNO_BADF
#define EBADMSG __WASI_ERRNO_BADMSG
#define EBUSY __WASI_ERRNO_BUSY
#define ECANCELED __WASI_ERRNO_CANCELED
#define ECHILD __WASI_ERRNO_CHILD
#define ECONNABORTED __WASI_ERRNO_CONNABORTED
#define ECONNREFUSED __WASI_ERRNO_CONNREFUSED
#define ECONNRESET __WASI_ERRNO_CONNRESET
#define EDEADLK __WASI_ERRNO_DEADLK
#define EDESTADDRREQ __WASI_ERRNO_DESTADDRREQ
#define EDOM __WASI_ERRNO_DOM
#define EDQUOT __WASI_ERRNO_DQUOT
#define EEXIST __WASI_ERRNO_EXIST
#define EFAULT __WASI_ERRNO_FAULT
#define EFBIG __WASI_ERRNO_FBIG
#define EHOSTUNREACH __WASI_ERRNO_HOSTUNREACH
#define EIDRM __WASI_ERRNO_IDRM
#define EILSEQ __WASI_ERRNO_ILSEQ
#define EINPROGRESS __WASI_ERRNO_INPROGRESS
#define EINTR __WASI_ERRNO_INTR
#define EINVAL __WASI_ERRNO_INVAL
#define EIO __WASI_ERRNO_IO
#define EISCONN __WASI_ERRNO_ISCONN
#define EISDIR __WASI_ERRNO_ISDIR
#define ELOOP __WASI_ERRNO_LOOP
#define EMFILE __WASI_ERRNO_MFILE
#define EMLINK __WASI_ERRNO_MLINK
#define EMSGSIZE __WASI_ERRNO_MSGSIZE
#define EMULTIHOP __WASI_ERRNO_MULTIHOP
#define ENAMETOOLONG __WASI_ERRNO_NAMETOOLONG
#define ENETDOWN __WASI_ERRNO_NETDOWN
#define ENETRESET __WASI_ERRNO_NETRESET
#define ENETUNREACH __WASI_ERRNO_NETUNREACH
#define ENFILE __WASI_ERRNO_NFILE
#define ENOBUFS __WASI_ERRNO_NOBUFS
#define ENODEV __WASI_ERRNO_NODEV
#define ENOENT __WASI_ERRNO_NOENT
#define ENOEXEC __WASI_ERRNO_NOEXEC
#define ENOLCK __WASI_ERRNO_NOLCK
#define ENOLINK __WASI_ERRNO_NOLINK
#define ENOMEM __WASI_ERRNO_NOMEM
#define ENOMSG __WASI_ERRNO_NOMSG
#define ENOPROTOOPT __WASI_ERRNO_NOPROTOOPT
#define ENOSPC __WASI_ERRNO_NOSPC
#define ENOSYS __WASI_ERRNO_NOSYS
#define ENOTCONN __WASI_ERRNO_NOTCONN
#define ESHUTDOWN __WASI_ERRNO_SHUTDOWN
#define ENOTDIR __WASI_ERRNO_NOTDIR
#define ENOTEMPTY __WASI_ERRNO_NOTEMPTY
#define ENOTRECOVERABLE __WASI_ERRNO_NOTRECOVERABLE
#define ENOTSOCK __WASI_ERRNO_NOTSOCK
#define ENOTSUP __WASI_ERRNO_NOTSUP
#define ENOTTY __WASI_ERRNO_NOTTY
#define ENXIO __WASI_ERRNO_NXIO
#define EOVERFLOW __WASI_ERRNO_OVERFLOW
#define EOWNERDEAD __WASI_ERRNO_OWNERDEAD
#define EPERM __WASI_ERRNO_PERM
#define EPIPE __WASI_ERRNO_PIPE
#define EPROTO __WASI_ERRNO_PROTO
#define EPROTONOSUPPORT __WASI_ERRNO_PROTONOSUPPORT
#define EPROTOTYPE __WASI_ERRNO_PROTOTYPE
#define ERANGE __WASI_ERRNO_RANGE
#define EROFS __WASI_ERRNO_ROFS
#define ESPIPE __WASI_ERRNO_SPIPE
#define ESRCH __WASI_ERRNO_SRCH
#define ESTALE __WASI_ERRNO_STALE
#define ETIMEDOUT __WASI_ERRNO_TIMEDOUT
#define ETXTBSY __WASI_ERRNO_TXTBSY
#define EXDEV __WASI_ERRNO_XDEV
#define ENOTCAPABLE __WASI_ERRNO_NOTCAPABLE
#define EMEMVIOLATION __WASI_ERRNO_MEMVIOLATION
#define EUNKNOWN __WASI_ERRNO_UNKNOWN

#define EPENDING __WASI_ERRNO_PENDING
#define EOPNOTSUPP ENOTSUP
#define EWOULDBLOCK EAGAIN

// The defines below are unused and only provided for compatibility with the error codes defined in common libc implementations.
// If we start using an errorcode, it will be changed to an actual error code.
#define ENOTBLK 200
#define ECHRNG 201
#define EL2NSYNC 202
#define EL3HLT 203
#define EL3RST 204
#define ELNRNG 205
#define EUNATCH 206
#define ENOCSI 207
#define EL2HLT 208
#define EBADE 209
#define EBADR 210
#define EXFULL 211
#define ENOANO 212
#define EBADRQC 213
#define EBADSLT 214
#define EBFONT 215
#define ENOSTR 216
#define ENODATA 217
#define ETIME 218
#define ENOSR 219
#define ENONET 220
#define ENOPKG 221
#define EREMOTE 222
#define EADV 223
#define ESRMNT 224
#define ECOMM 225
#define EDOTDOT 226
#define ENOTUNIQ 227
#define EBADFD 228
#define EREMCHG 229
#define ELIBACC 230
#define ELIBBAD 231
#define ELIBSCN 232
#define ELIBMAX 233
#define ELIBEXEC 234
#define ERESTART 235
#define ESTRPIPE 236
#define EUSERS 237
#define ESOCKTNOSUPPORT 238
#define EPFNOSUPPORT 239
#define ETOOMANYREFS 240
#define EHOSTDOWN 241
#define EUCLEAN 242
#define ENOTNAM 243
#define ENAVAIL 244
#define EISNAM 245
#define EREMOTEIO 246
#define ENOMEDIUM 247
#define EMEDIUMTYPE 248
#define ENOKEY 249
#define EKEYEXPIRED 250
#define EKEYREVOKED 251
#define EKEYREJECTED 252
#define ERFKILL 253
#define EHWPOISON 254

// firebox#87F: the single source of truth for WASI errno -> guest errno.
//
// Every consumer of this relationship is generated from __FBX_ERRNO_MAP and
// nothing else: the constant-expression macro below, the lookup table in
// libc-bottom-half/cloudlibc/src/libc/errno/errno.c, and (later) the
// regenerated constants in ../firebox-forks/libc-rs. That is why the map
// lives in this header rather than next to the function -- this file is the
// artifact the Rust side is regenerated FROM, so it has to be able to
// describe the mapping without reference to any .c file.
//
// Right now every E* above is #defined to its __WASI_ERRNO_* counterpart, so
// the map is the identity and __FBX_E_FROM_WASI(w) == w for every WASI errno.
// That is deliberate: it lets the machinery land, be asserted, and be
// reviewed while it provably cannot change any program's behaviour. When the
// E* values are renumbered onto Linux uapi the map stops being the identity
// and every one of those assertions starts carrying real weight, without a
// single line of the machinery moving.
//
// A: an opaque pass-through argument. C macros cannot be partially applied,
// so a consumer that needs per-row access to something outside the list (the
// ternary chain needs the input expression) threads it through here. A
// consumer that does not need it passes a placeholder and ignores it.
#define __FBX_ERRNO_MAP(X, A) \
  X(A, __WASI_ERRNO_2BIG,           E2BIG) \
  X(A, __WASI_ERRNO_ACCES,          EACCES) \
  X(A, __WASI_ERRNO_ADDRINUSE,      EADDRINUSE) \
  X(A, __WASI_ERRNO_ADDRNOTAVAIL,   EADDRNOTAVAIL) \
  X(A, __WASI_ERRNO_AFNOSUPPORT,    EAFNOSUPPORT) \
  X(A, __WASI_ERRNO_AGAIN,          EAGAIN) \
  X(A, __WASI_ERRNO_ALREADY,        EALREADY) \
  X(A, __WASI_ERRNO_BADF,           EBADF) \
  X(A, __WASI_ERRNO_BADMSG,         EBADMSG) \
  X(A, __WASI_ERRNO_BUSY,           EBUSY) \
  X(A, __WASI_ERRNO_CANCELED,       ECANCELED) \
  X(A, __WASI_ERRNO_CHILD,          ECHILD) \
  X(A, __WASI_ERRNO_CONNABORTED,    ECONNABORTED) \
  X(A, __WASI_ERRNO_CONNREFUSED,    ECONNREFUSED) \
  X(A, __WASI_ERRNO_CONNRESET,      ECONNRESET) \
  X(A, __WASI_ERRNO_DEADLK,         EDEADLK) \
  X(A, __WASI_ERRNO_DESTADDRREQ,    EDESTADDRREQ) \
  X(A, __WASI_ERRNO_DOM,            EDOM) \
  X(A, __WASI_ERRNO_DQUOT,          EDQUOT) \
  X(A, __WASI_ERRNO_EXIST,          EEXIST) \
  X(A, __WASI_ERRNO_FAULT,          EFAULT) \
  X(A, __WASI_ERRNO_FBIG,           EFBIG) \
  X(A, __WASI_ERRNO_HOSTUNREACH,    EHOSTUNREACH) \
  X(A, __WASI_ERRNO_IDRM,           EIDRM) \
  X(A, __WASI_ERRNO_ILSEQ,          EILSEQ) \
  X(A, __WASI_ERRNO_INPROGRESS,     EINPROGRESS) \
  X(A, __WASI_ERRNO_INTR,           EINTR) \
  X(A, __WASI_ERRNO_INVAL,          EINVAL) \
  X(A, __WASI_ERRNO_IO,             EIO) \
  X(A, __WASI_ERRNO_ISCONN,         EISCONN) \
  X(A, __WASI_ERRNO_ISDIR,          EISDIR) \
  X(A, __WASI_ERRNO_LOOP,           ELOOP) \
  X(A, __WASI_ERRNO_MFILE,          EMFILE) \
  X(A, __WASI_ERRNO_MLINK,          EMLINK) \
  X(A, __WASI_ERRNO_MSGSIZE,        EMSGSIZE) \
  X(A, __WASI_ERRNO_MULTIHOP,       EMULTIHOP) \
  X(A, __WASI_ERRNO_NAMETOOLONG,    ENAMETOOLONG) \
  X(A, __WASI_ERRNO_NETDOWN,        ENETDOWN) \
  X(A, __WASI_ERRNO_NETRESET,       ENETRESET) \
  X(A, __WASI_ERRNO_NETUNREACH,     ENETUNREACH) \
  X(A, __WASI_ERRNO_NFILE,          ENFILE) \
  X(A, __WASI_ERRNO_NOBUFS,         ENOBUFS) \
  X(A, __WASI_ERRNO_NODEV,          ENODEV) \
  X(A, __WASI_ERRNO_NOENT,          ENOENT) \
  X(A, __WASI_ERRNO_NOEXEC,         ENOEXEC) \
  X(A, __WASI_ERRNO_NOLCK,          ENOLCK) \
  X(A, __WASI_ERRNO_NOLINK,         ENOLINK) \
  X(A, __WASI_ERRNO_NOMEM,          ENOMEM) \
  X(A, __WASI_ERRNO_NOMSG,          ENOMSG) \
  X(A, __WASI_ERRNO_NOPROTOOPT,     ENOPROTOOPT) \
  X(A, __WASI_ERRNO_NOSPC,          ENOSPC) \
  X(A, __WASI_ERRNO_NOSYS,          ENOSYS) \
  X(A, __WASI_ERRNO_NOTCONN,        ENOTCONN) \
  X(A, __WASI_ERRNO_NOTDIR,         ENOTDIR) \
  X(A, __WASI_ERRNO_NOTEMPTY,       ENOTEMPTY) \
  X(A, __WASI_ERRNO_NOTRECOVERABLE, ENOTRECOVERABLE) \
  X(A, __WASI_ERRNO_NOTSOCK,        ENOTSOCK) \
  X(A, __WASI_ERRNO_NOTSUP,         ENOTSUP) \
  X(A, __WASI_ERRNO_NOTTY,          ENOTTY) \
  X(A, __WASI_ERRNO_NXIO,           ENXIO) \
  X(A, __WASI_ERRNO_OVERFLOW,       EOVERFLOW) \
  X(A, __WASI_ERRNO_OWNERDEAD,      EOWNERDEAD) \
  X(A, __WASI_ERRNO_PERM,           EPERM) \
  X(A, __WASI_ERRNO_PIPE,           EPIPE) \
  X(A, __WASI_ERRNO_PROTO,          EPROTO) \
  X(A, __WASI_ERRNO_PROTONOSUPPORT, EPROTONOSUPPORT) \
  X(A, __WASI_ERRNO_PROTOTYPE,      EPROTOTYPE) \
  X(A, __WASI_ERRNO_RANGE,          ERANGE) \
  X(A, __WASI_ERRNO_ROFS,           EROFS) \
  X(A, __WASI_ERRNO_SPIPE,          ESPIPE) \
  X(A, __WASI_ERRNO_SRCH,           ESRCH) \
  X(A, __WASI_ERRNO_STALE,          ESTALE) \
  X(A, __WASI_ERRNO_TIMEDOUT,       ETIMEDOUT) \
  X(A, __WASI_ERRNO_TXTBSY,         ETXTBSY) \
  X(A, __WASI_ERRNO_XDEV,           EXDEV) \
  X(A, __WASI_ERRNO_NOTCAPABLE,     ENOTCAPABLE) \
  X(A, __WASI_ERRNO_SHUTDOWN,       ESHUTDOWN) \
  X(A, __WASI_ERRNO_MEMVIOLATION,   EMEMVIOLATION) \
  X(A, __WASI_ERRNO_UNKNOWN,        EUNKNOWN) \
  X(A, __WASI_ERRNO_PENDING,        EPENDING)

// The largest WASI errno the map covers. The WASI errno space is dense over
// [0, __FBX_ERRNO_WASI_MAX]; __WASI_ERRNO_SUCCESS (0) is not an error and has
// no E* name, so it is not a row of the map.
#define __FBX_ERRNO_WASI_MAX __WASI_ERRNO_PENDING

// __FBX_E_FROM_WASI(w): the map as a constant expression.
//
// A ternary chain over integer constants is itself an integer constant
// expression whenever its argument is one, which is the whole point: this is
// the form that can appear inside static_assert, so the assertions in errno.c
// can be re-aimed from "E* equals the WASI number" onto "E* equals what the
// translation produces for the WASI number" and stay compile-time checked
// across the renumbering.
//
// `w` is substituted once per row, so pass a side-effect-free expression.
// Runtime callers should use __wasilibc_errno_from_wasi() instead, which
// evaluates its argument once and indexes a table.
//
// A WASI value outside the map yields EUNKNOWN rather than passing the number
// through: an unmapped guest number is indistinguishable from a real errno at
// the call site, and a wrong-but-plausible errno is worse than an honest
// "something failed and libc does not know what".
//
// __WASI_ERRNO_SUCCESS is handled ahead of the chain instead of being left to
// fall through to EUNKNOWN. It is a real WASI value rather than an
// out-of-range one, "no error" is its faithful translation, and the runtime
// table has to store something at index 0 regardless -- letting the macro and
// the table disagree there would be a trap with no upside.
#define __FBX_ERRNO_TERNARY_ROW(__in, __w, __e) (__in) == (int)(__w) ? (int)(__e) :
#define __FBX_E_FROM_WASI(w)                                    \
  ((int)(w) == (int)(__WASI_ERRNO_SUCCESS) ? 0                  \
   : (__FBX_ERRNO_MAP(__FBX_ERRNO_TERNARY_ROW, (w)) (int)(EUNKNOWN)))

#endif
