#ifndef _LIMITS_H
#define _LIMITS_H

#include <features.h>

#include <bits/alltypes.h> /* __LONG_MAX */

/* Support signed or unsigned plain-char */

#if '\xff' > 0
#define CHAR_MIN 0
#define CHAR_MAX 255
#else
#define CHAR_MIN (-128)
#define CHAR_MAX 127
#endif

#define CHAR_BIT 8
#define SCHAR_MIN (-128)
#define SCHAR_MAX 127
#define UCHAR_MAX 255
#define SHRT_MIN  (-1-0x7fff)
#define SHRT_MAX  0x7fff
#define USHRT_MAX 0xffff
#define INT_MIN  (-1-0x7fffffff)
#define INT_MAX  0x7fffffff
#define UINT_MAX 0xffffffffU
#define LONG_MIN (-LONG_MAX-1)
#define LONG_MAX __LONG_MAX
#define ULONG_MAX (2UL*LONG_MAX+1)
#define LLONG_MIN (-LLONG_MAX-1)
#define LLONG_MAX  0x7fffffffffffffffLL
#define ULLONG_MAX (2ULL*LLONG_MAX+1)

#define MB_LEN_MAX 4

#if defined(_POSIX_SOURCE) || defined(_POSIX_C_SOURCE) \
 || defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) || defined(_BSD_SOURCE)

#include <bits/limits.h>

/* firebox#E1F: Firebox has pipes -- `pipe`/`pipe2`/`popen` are defined in
 * libc.a on every shipped shelf (llvm-nm, measured) and route to the host
 * `fd_pipe` import. PIPE_BUF is a GUARANTEE, not a capacity readout: POSIX
 * requires only that a write of <= PIPE_BUF bytes be atomic, and permits any
 * value >= _POSIX_PIPE_BUF (512). The guarantee holds here with room to spare
 * -- `fd_pipe_internal` builds the pipe with `Pipe::new()`, i.e. capacity
 * `None`/unbounded, and `PipeTx::write` hands the WHOLE buffer to the channel
 * as one message with no backpressure split (wasmer fork,
 * lib/virtual-fs/src/pipe.rs), so a single write of ANY length is indivisible.
 * Keeping upstream musl's 4096 therefore understates a stronger property,
 * which is the safe direction for a floor guarantee, and keeps the value
 * byte-identical to musl-on-Linux (invariant 2).
 *   ⚠ `fpathconf(fd, _PC_PIPE_BUF)` still returns -1 via its own
 *   __wasilibc_unmodified_upstream site in src/conf/fpathconf.c. That is now a
 *   divergence between two standard discovery mechanisms, it is a .c change
 *   rather than a header one, and it is deliberately NOT made here -- a source
 *   edit invalidates every shelf, so it belongs with a rebuild. */
#define PIPE_BUF 4096
#define FILESIZEBITS 64
#ifndef NAME_MAX
#define NAME_MAX 255
#endif
#define PATH_MAX 4096
#define NGROUPS_MAX 32
#define ARG_MAX 131072
#define IOV_MAX 1024
#define SYMLOOP_MAX 40
#define WORD_BIT 32
#define SSIZE_MAX LONG_MAX
#define TZNAME_MAX 6
#define TTY_NAME_MAX 32
#define HOST_NAME_MAX 255

#if LONG_MAX == 0x7fffffffL
#define LONG_BIT 32
#else
#define LONG_BIT 64
#endif

/* Implementation choices... */

#if defined(__wasilibc_unmodified_upstream) || defined(_REENTRANT)
#define PTHREAD_KEYS_MAX 128
#define PTHREAD_STACK_MIN 2048
#define PTHREAD_DESTRUCTOR_ITERATIONS 4
#endif
#if defined(__wasilibc_unmodified_upstream) || defined(_REENTRANT)
#define SEM_VALUE_MAX 0x7fffffff
#define SEM_NSEMS_MAX 256
#endif
#define DELAYTIMER_MAX 0x7fffffff
/* firebox#KS9: wasix-libc now ships a guest POSIX message-queue implementation
 * (src/mq/mq_impl.c), so MQ_PRIO_MAX is a real limit here, not a WASI absence.
 * Value matches Linux (the mq_send/mq_timedsend priority-range tests read it). */
#define MQ_PRIO_MAX 32768
#define LOGIN_NAME_MAX 256

/* Arbitrary numbers... */

#ifdef __wasilibc_unmodified_upstream /* WASI has no shell commands */
#define BC_BASE_MAX 99
#define BC_DIM_MAX 2048
#define BC_SCALE_MAX 99
#define BC_STRING_MAX 1000
#endif
#define CHARCLASS_NAME_MAX 14
#define COLL_WEIGHTS_MAX 2
/* firebox#E1F: "WASI has no shell commands" is false -- `system` and `popen`
 * are defined in libc.a on every shipped shelf (llvm-nm, measured). LINE_MAX is
 * the POSIX utility input-line limit that text tools (awk, sed, getconf) read;
 * it is a plain limit constant, not a capability claim, and 4096 is upstream
 * musl's value verbatim, so a guest sees exactly what musl-on-Linux gives it
 * (invariant 2). Note this introduces NO new inconsistency:
 * `sysconf(_SC_LINE_MAX)` returns -1, but that -1 is upstream musl's own
 * answer (it predates the WASI port -- present in commit 320054e, the initial
 * import), so header-4096-plus-sysconf-indeterminate IS the Linux behaviour we
 * are matching, not an amputation artefact.
 *
 * EXPR_NEST_MAX stays amputated by SCOPE, not by verdict -- it is the same
 * class and very likely the same answer, but this lane was scoped to LINE_MAX
 * and did not measure it. */
#ifdef __wasilibc_unmodified_upstream /* WASI has no shell commands */
#define EXPR_NEST_MAX 32
#endif
#define LINE_MAX 4096
#define RE_DUP_MAX 255

#define NL_ARGMAX 9
#define NL_MSGMAX 32767
#define NL_SETMAX 255
#define NL_TEXTMAX 2048

#endif

#if defined(_GNU_SOURCE) || defined(_BSD_SOURCE) || defined(_XOPEN_SOURCE)

#ifdef PAGESIZE
#define PAGE_SIZE PAGESIZE
#endif
#define NZERO 20
#define NL_LANGMAX 32

#endif

#if defined(_GNU_SOURCE) || defined(_BSD_SOURCE) \
 || (defined(_XOPEN_SOURCE) && _XOPEN_SOURCE+0 < 700)

#define NL_NMAX 16

#endif

/* POSIX/SUS requirements follow. These numbers come directly
 * from SUS and have nothing to do with the host system. */

#define _POSIX_AIO_LISTIO_MAX   2
#define _POSIX_AIO_MAX          1
#define _POSIX_ARG_MAX          4096
#define _POSIX_CHILD_MAX        25
#define _POSIX_CLOCKRES_MIN     20000000
#define _POSIX_DELAYTIMER_MAX   32
#define _POSIX_HOST_NAME_MAX    255
#define _POSIX_LINK_MAX         8
#define _POSIX_LOGIN_NAME_MAX   9
#define _POSIX_MAX_CANON        255
#define _POSIX_MAX_INPUT        255
#define _POSIX_MQ_OPEN_MAX      8
#define _POSIX_MQ_PRIO_MAX      32
#define _POSIX_NAME_MAX         14
#define _POSIX_NGROUPS_MAX      8
#define _POSIX_OPEN_MAX         20
#define _POSIX_PATH_MAX         256
#define _POSIX_PIPE_BUF         512
#define _POSIX_RE_DUP_MAX       255
#define _POSIX_RTSIG_MAX        8
#define _POSIX_SEM_NSEMS_MAX    256
#define _POSIX_SEM_VALUE_MAX    32767
#define _POSIX_SIGQUEUE_MAX     32
#define _POSIX_SSIZE_MAX        32767
#define _POSIX_STREAM_MAX       8
#define _POSIX_SS_REPL_MAX      4
#define _POSIX_SYMLINK_MAX      255
#define _POSIX_SYMLOOP_MAX      8
#define _POSIX_THREAD_DESTRUCTOR_ITERATIONS 4
#define _POSIX_THREAD_KEYS_MAX  128
#define _POSIX_THREAD_THREADS_MAX 64
#define _POSIX_TIMER_MAX        32
#define _POSIX_TRACE_EVENT_NAME_MAX 30
#define _POSIX_TRACE_NAME_MAX   8
#define _POSIX_TRACE_SYS_MAX    8
#define _POSIX_TRACE_USER_EVENT_MAX 32
#define _POSIX_TTY_NAME_MAX     9
#define _POSIX_TZNAME_MAX       6
#define _POSIX2_BC_BASE_MAX     99
#define _POSIX2_BC_DIM_MAX      2048
#define _POSIX2_BC_SCALE_MAX    99
#define _POSIX2_BC_STRING_MAX   1000
#define _POSIX2_CHARCLASS_NAME_MAX 14
#define _POSIX2_COLL_WEIGHTS_MAX 2
#define _POSIX2_EXPR_NEST_MAX   32
#define _POSIX2_LINE_MAX        2048
#define _POSIX2_RE_DUP_MAX      255

#define _XOPEN_IOV_MAX          16
#define _XOPEN_NAME_MAX         255
#define _XOPEN_PATH_MAX         1024

#endif
