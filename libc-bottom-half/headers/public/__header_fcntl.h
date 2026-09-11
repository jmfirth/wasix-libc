#ifndef __wasilibc___header_fcntl_h
#define __wasilibc___header_fcntl_h

#include <wasi/api.h>
#include <__seek.h>
#include <__mode_t.h>

/*
 * open(2) flags. Authority: Linux uapi asm-generic/fcntl.h, which is what
 * libc-top-half/musl/arch/generic/bits/fcntl.h transcribes, plus musl's own
 * include/fcntl.h for the four names uapi does not carry (O_ACCMODE's musl
 * width, O_RDONLY/O_WRONLY/O_RDWR, O_SEARCH/O_EXEC, O_TTY_INIT).
 *
 * firebox#87F — all sixteen names common to this header and that authority
 * previously carried WASI-invented values: the flag names were *defined as*
 * the wire encoding (O_APPEND == __WASI_FDFLAGS_APPEND, O_CREAT ==
 * __WASI_OFLAGS_CREAT << 12, O_CLOEXEC == __WASI_FDFLAGSEXT_CLOEXEC << 30),
 * and the access mode occupied three invented high bits so it fell outside
 * every wire field. Three encoders exploited that identity with a shift and a
 * mask (`oflag & 0xfff`, `(oflag >> 12) & 0xfff`, `(oflag >> 30) & 0x03`).
 *
 * ⛔ THE FAILURE MODE OF GETTING THIS WRONG IS FAIL-OPEN, NOT AN ERRNO.
 * Linux O_WRONLY is 1 and O_RDWR is 2, which land exactly on
 * __WASI_FDFLAGS_APPEND(1) and __WASI_FDFLAGS_DSYNC(2). A shift-and-mask
 * encoder left in place across this renumber turns every write-open into a
 * silent O_APPEND — a file that opens, writes and closes successfully with
 * the bytes in the wrong place. That is why the numbers here may only move
 * together with __wasilibc_{fdflags,oflags,fdflagsext}_to_wasi(), which are
 * now the ONLY translation from these names to the wire, and with
 * __wasilibc_fdflags_from_wasi() which is the only translation back
 * (fcntl(F_GETFL)). See wasi/libc.h and cloudlibc/src/libc/fcntl/openflags.c.
 *
 * ⛔ LOCKSTEP, and like F_* below it is not a loud failure: an object built
 * against the OLD header passing O_WRONLY(0x10000000) to a NEWLY built libc
 * lands in no access-mode arm at all and gets EINVAL, while old
 * O_APPEND(1)/O_DSYNC(2) become new O_WRONLY/O_RDWR. Reachable through the
 * shared libc provider under dynamic linking. One complete relink or none.
 */
#define O_RDONLY   (0)
#define O_WRONLY   (01)
#define O_RDWR     (02)

#define O_CREAT     (0100)
#define O_EXCL      (0200)
#define O_NOCTTY    (0400)
#define O_TRUNC     (01000)
#define O_APPEND    (02000)
#define O_NONBLOCK  (04000)
#define O_DSYNC     (010000)
/*
 * On Linux O_SYNC is __O_SYNC|O_DSYNC and O_RSYNC is *the same value* as
 * O_SYNC — the kernel gives synchronised-read no distinct encoding. The
 * encoder therefore cannot recover __WASI_FDFLAGS_RSYNC from a guest flag
 * word; it maps this bit pattern to __WASI_FDFLAGS_SYNC, which is what Linux
 * itself does with O_RSYNC. The decode direction still recognises RSYNC,
 * because the host may report it.
 */
#define O_SYNC      (04010000)
#define O_RSYNC     (04010000)
#define O_DIRECTORY (0200000)
#define O_NOFOLLOW  (0400000)
#define O_CLOEXEC   (02000000)

/*
 * O_SEARCH and O_EXEC are POSIX names with no row in Linux uapi; musl defines
 * both as O_PATH (010000000) and folds O_PATH into O_ACCMODE. We adopt musl's
 * numbers so `oflag & O_ACCMODE` discriminates the same way it does there.
 *
 * O_PATH itself is deliberately NOT defined: we would have to answer it with
 * an ordinary open carrying reduced rights, which is a false success rather
 * than the descriptor-without-access Linux hands back. A guest asking for a
 * name we cannot honour should fail to build, not silently get something else.
 */
#define O_SEARCH   (010000000)
#define O_EXEC     (010000000)
#define O_ACCMODE  (03 | O_SEARCH)

/*
 * O_TTY_INIT is defined to be zero, meaning that WASI implementations are
 * expected to always initialize a terminal the first time it's opened. musl
 * defines it as zero too, so this name did not move.
 */
#define O_TTY_INIT (0)

#define POSIX_FADV_DONTNEED __WASI_ADVICE_DONTNEED
#define POSIX_FADV_NOREUSE __WASI_ADVICE_NOREUSE
#define POSIX_FADV_NORMAL __WASI_ADVICE_NORMAL
#define POSIX_FADV_RANDOM __WASI_ADVICE_RANDOM
#define POSIX_FADV_SEQUENTIAL __WASI_ADVICE_SEQUENTIAL
#define POSIX_FADV_WILLNEED __WASI_ADVICE_WILLNEED

/*
 * fcntl(2) command numbers. Authority: Linux uapi asm-generic/fcntl.h
 * (x86-64 and aarch64 agree on all of these).
 *
 * firebox#87F: F_DUPFD, F_DUPFD_CLOEXEC, F_SETLK, F_GETLK and F_SETLKW
 * previously carried WASI-invented values (5, 6, 7, 8, 9). They are safe to
 * renumber because `cmd` NEVER crosses the host boundary: fcntl() in
 * libc-bottom-half/cloudlibc/src/libc/fcntl/fcntl.c dispatches on it in a
 * symbolic `switch` and the lock path re-encodes into its own
 * __FIREBOX_LOCK_OP_* opcodes, which are unchanged. Nothing in the runtime
 * (crates/, the wasmer fork, blink's xlat.c) reads a guest F_* number.
 *
 * ⛔ LOCKSTEP, and it is not a loud failure: the old and new sets OVERLAP.
 * Old F_DUPFD(5) == new F_GETLK(5) and old F_DUPFD_CLOEXEC(6) == new
 * F_SETLK(6), so an object compiled against the OLD header calling a NEWLY
 * built libc — reachable through the shared libc provider under dynamic
 * linking — enters the lock branch and va_arg's an int as `struct flock *`.
 * That is memory-unsafe, not an EINVAL. This header must move together with
 * a complete relink of everything that links or dlopen's this libc.
 */
#define F_DUPFD (0)
#define F_GETFD (1)
#define F_SETFD (2)
#define F_GETFL (3)
#define F_SETFL (4)
#define F_DUPFD_CLOEXEC (1030)

/*
 * POSIX advisory record locks (Firebox extension, issue #243).
 *
 * Routed at runtime to the WASIX import `__wasix_fd_lock_range`. The
 * runtime maintains a per-(inode, pid) lock table; closing any fd on
 * the inode releases this process's locks on it. F_SETLKW currently
 * fails with ENOTSUP on contention (no wait queue yet); callers should
 * use F_SETLK and retry-with-backoff if they need blocking semantics.
 */
#define F_GETLK   (5)
#define F_SETLK   (6)
#define F_SETLKW  (7)

#define F_RDLCK   (0)
#define F_WRLCK   (1)
#define F_UNLCK   (2)

#define FD_CLOEXEC (1)

#define AT_SYMLINK_NOFOLLOW (0x100)
#define AT_EACCESS          (0x200)
#define AT_SYMLINK_FOLLOW   (0x2)
#define AT_REMOVEDIR        (0x4)

#define AT_FDCWD (-2)

#endif
