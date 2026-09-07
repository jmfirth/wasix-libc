#ifndef __wasilibc___header_fcntl_h
#define __wasilibc___header_fcntl_h

#include <wasi/api.h>
#include <__seek.h>
#include <__mode_t.h>

#define O_APPEND __WASI_FDFLAGS_APPEND
#define O_DSYNC __WASI_FDFLAGS_DSYNC
#define O_NONBLOCK __WASI_FDFLAGS_NONBLOCK
#define O_RSYNC __WASI_FDFLAGS_RSYNC
#define O_SYNC __WASI_FDFLAGS_SYNC
#define O_CREAT (__WASI_OFLAGS_CREAT << 12)
#define O_DIRECTORY (__WASI_OFLAGS_DIRECTORY << 12)
#define O_EXCL (__WASI_OFLAGS_EXCL << 12)
#define O_TRUNC (__WASI_OFLAGS_TRUNC << 12)

#define O_NOFOLLOW (0x01000000)
#define O_EXEC     (0x02000000)
#define O_RDONLY   (0x04000000)
#define O_SEARCH   (0x08000000)
#define O_WRONLY   (0x10000000)

#define O_CLOEXEC (__WASI_FDFLAGSEXT_CLOEXEC << 30)

/*
 * O_TTY_INIT is defined to be zero, meaning that WASI implementations are
 * expected to always initialize a terminal the first time it's opened.
 */
#define O_TTY_INIT (0)

#define O_NOCTTY   (0)

#define O_RDWR (O_RDONLY | O_WRONLY)
#define O_ACCMODE (O_EXEC | O_RDWR | O_SEARCH)

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
