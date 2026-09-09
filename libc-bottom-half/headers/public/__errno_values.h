#ifndef __wasilibc___errno_values_h
#define __wasilibc___errno_values_h

#include <wasi/api.h>

#define E2BIG            7
#define EACCES           13
#define EADDRINUSE       98
#define EADDRNOTAVAIL    99
#define EAFNOSUPPORT     97
#define EAGAIN           11
#define EALREADY         114
#define EBADF            9
#define EBADMSG          74
#define EBUSY            16
#define ECANCELED        125
#define ECHILD           10
#define ECONNABORTED     103
#define ECONNREFUSED     111
#define ECONNRESET       104
#define EDEADLK          35
#define EDESTADDRREQ     89
#define EDOM             33
#define EDQUOT           122
#define EEXIST           17
#define EFAULT           14
#define EFBIG            27
#define EHOSTUNREACH     113
#define EIDRM            43
#define EILSEQ           84
#define EINPROGRESS      115
#define EINTR            4
#define EINVAL           22
#define EIO              5
#define EISCONN          106
#define EISDIR           21
#define ELOOP            40
#define EMFILE           24
#define EMLINK           31
#define EMSGSIZE         90
#define EMULTIHOP        72
#define ENAMETOOLONG     36
#define ENETDOWN         100
#define ENETRESET        102
#define ENETUNREACH      101
#define ENFILE           23
#define ENOBUFS          105
#define ENODEV           19
#define ENOENT           2
#define ENOEXEC          8
#define ENOLCK           37
#define ENOLINK          67
#define ENOMEM           12
#define ENOMSG           42
#define ENOPROTOOPT      92
#define ENOSPC           28
#define ENOSYS           38
#define ENOTCONN         107
#define ESHUTDOWN        108
#define ENOTDIR          20
#define ENOTEMPTY        39
#define ENOTRECOVERABLE  131
#define ENOTSOCK         88
#define ENOTSUP          95
#define ENOTTY           25
#define ENXIO            6
#define EOVERFLOW        75
#define EOWNERDEAD       130
#define EPERM            1
#define EPIPE            32
#define EPROTO           71
#define EPROTONOSUPPORT  93
#define EPROTOTYPE       91
#define ERANGE           34
#define EROFS            30
#define ESPIPE           29
#define ESRCH            3
#define ESTALE           116
#define ETIMEDOUT        110
#define ETXTBSY          26
#define EXDEV            18
#define ENOTCAPABLE      134
#define EMEMVIOLATION    135
#define EUNKNOWN         136

#define EPENDING         137
#define EOPNOTSUPP ENOTSUP
#define EWOULDBLOCK EAGAIN
#define EDEADLOCK EDEADLK

// The defines below are unused and only provided for compatibility with the error codes defined in common libc implementations.
// If we start using an errorcode, it will be changed to an actual error code.
#define ENOTBLK          15
#define ECHRNG           44
#define EL2NSYNC         45
#define EL3HLT           46
#define EL3RST           47
#define ELNRNG           48
#define EUNATCH          49
#define ENOCSI           50
#define EL2HLT           51
#define EBADE            52
#define EBADR            53
#define EXFULL           54
#define ENOANO           55
#define EBADRQC          56
#define EBADSLT          57
#define EBFONT           59
#define ENOSTR           60
#define ENODATA          61
#define ETIME            62
#define ENOSR            63
#define ENONET           64
#define ENOPKG           65
#define EREMOTE          66
#define EADV             68
#define ESRMNT           69
#define ECOMM            70
#define EDOTDOT          73
#define ENOTUNIQ         76
#define EBADFD           77
#define EREMCHG          78
#define ELIBACC          79
#define ELIBBAD          80
#define ELIBSCN          81
#define ELIBMAX          82
#define ELIBEXEC         83
#define ERESTART         85
#define ESTRPIPE         86
#define EUSERS           87
#define ESOCKTNOSUPPORT  94
#define EPFNOSUPPORT     96
#define ETOOMANYREFS     109
#define EHOSTDOWN        112
#define EUCLEAN          117
#define ENOTNAM          118
#define ENAVAIL          119
#define EISNAM           120
#define EREMOTEIO        121
#define ENOMEDIUM        123
#define EMEDIUMTYPE      124
#define ENOKEY           126
#define EKEYEXPIRED      127
#define EKEYREVOKED      128
#define EKEYREJECTED     129
#define ERFKILL          132
#define EHWPOISON        133

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
// The map was landed while every E* above was still #defined to its
// __WASI_ERRNO_* counterpart, so it was the identity and could not change any
// program's behaviour; the call sites were converted under that identity. The
// renumbering below is the step that made the map real, and it moved no line
// of the machinery -- only the numbers the right-hand column resolves to.
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
