//! POSIX-like functions supporting absolute path arguments, implemented in
//! terms of `__wasilibc_find_relpath` and `*at`-style functions.

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>
#include <wasi/api.h>
#include <wasi/libc.h>
#include <wasi/libc-find-relpath.h>
#include <wasi/libc-nocwd.h>

static int find_relpath2(
    const char *path,
    char **relative,
    size_t *relative_len
) {
    const char *abs;
    return __wasilibc_find_relpath_alloc(path, &abs, relative, relative_len, 1);
}

// Helper to call `__wasilibc_find_relpath` and return an already-managed
// pointer for the `relative` path. This function is not reentrant since the
// `relative` pointer will point to static data that cannot be reused until
// `relative` is no longer used.
static int find_relpath(const char *path, char **relative) {
    static __thread char *relative_buf = NULL;
    static __thread size_t relative_buf_len = 0;
    int fd = find_relpath2(path, &relative_buf, &relative_buf_len);
    // find_relpath2 can update relative_buf, so assign it after the call
    *relative = relative_buf;
    return fd;
}

// same as `find_relpath`, but uses another set of static variables to cache
static int find_relpath_alt(const char *path, char **relative) {
    static __thread char *relative_buf = NULL;
    static __thread size_t relative_buf_len = 0;
    int fd = find_relpath2(path, &relative_buf, &relative_buf_len);
    // find_relpath2 can update relative_buf, so assign it after the call
    *relative = relative_buf;
    return fd;
}

// firebox#HXN: process-wide file-mode creation mask (umask). WASI/WASIX has no
// kernel umask, so libc maintains it in guest memory. It rides proc_fork's
// private-memory copy, so a child inherits the parent's umask exactly as on
// Linux. Accessed with __atomic builtins so a threaded program's concurrent
// umask()/open() are well-defined (matching the sibling accounting in mman.c).
// Only the low 0777 permission bits are significant (POSIX: "only the file
// permission bits of cmask are used"). The default 022 is the conventional
// Linux login default; it also reproduces the 0644 the host historically
// stamped for the common open(..., 0666) create, so honoring the mode is a
// no-op for that case while newly respecting explicit modes.
static int __wasilibc_umask_value = 0022;

static mode_t __wasilibc_umask_get(void) {
    return (mode_t)(__atomic_load_n(&__wasilibc_umask_value, __ATOMIC_SEQ_CST) & 0777);
}

// firebox#HXN: apply a caller-supplied creation mode to a freshly-created
// object. WASI path_open carries no mode, so the host stamps DEFAULT_FILE_MODE
// (0644); recover the requested mode by chmod-ing the descriptor we just
// created, exactly as Linux applies (mode & ~umask) at create time. Best-
// effort: we own the just-created object, so __wasix_fd_chmod cannot fail for
// lack of privilege; a pathological failure leaves the host default rather than
// failing an otherwise-successful open (which would leak the created fd). The
// import does not touch the global errno, so a successful open's errno is left
// undisturbed.
static void __wasilibc_apply_create_mode(int fd, mode_t mode) {
    mode_t eff = (mode & 07777) & ~__wasilibc_umask_get();
    (void)__wasix_fd_chmod(fd, (uint32_t)eff);
}

// firebox#HXN: create-mode-aware open on an already-resolved (dirfd, relative)
// pair. The WASI ABI has no mode field, so honor the caller's create mode by
// chmod-ing the descriptor after a REAL create. The "did this open actually
// create the object?" test must be exact — chmod-ing a pre-existing file the
// caller merely opened with O_CREAT would corrupt its mode (a real bug), and
// Linux ignores the mode argument entirely unless the file is created. Two
// cases:
//   * O_CREAT|O_EXCL: a successful open ALWAYS created the object (O_EXCL fails
//     with EEXIST otherwise) — race-free, no probe needed.
//   * O_CREAT without O_EXCL: the object may or may not have pre-existed. Probe
//     with fstatat BEFORE the open (matching the open's O_NOFOLLOW symlink
//     semantics); only chmod if it did not exist. The residual TOCTOU is
//     benign: we only ever chmod the fd THIS call opened, so a concurrent
//     creator in the tiny window at worst leaves the host default mode — never
//     another file's mode.
int __wasilibc_nocwd_openat_mode(int dirfd, const char *relative_path,
                                 int oflag, mode_t mode) {
    // firebox#HXN: preserve the caller's errno across our INTERNAL probe + chmod.
    // The fstatat probe below fails ENOENT for the common create case (a file
    // that does NOT yet exist), and __wasix_fd_chmod may perturb errno too —
    // neither must leak into an otherwise-SUCCESSFUL open. rustc/LLVM read errno
    // after opening their output stream and mis-report a stale ENOENT as "IO
    // failure on output stream: No such file or directory" (this broke the whole
    // rust toolchain in the #M4F warm-up gate — a successful create left
    // errno=ENOENT). The old nomode open never touched errno on success; restore
    // that invariant. On a REAL open failure we leave openat_nomode's errno.
    int saved_errno = errno;
    int probe_existed = 0;
    if ((oflag & O_CREAT) && !(oflag & O_EXCL)) {
        struct stat st;
        int atflag = (oflag & O_NOFOLLOW) ? AT_SYMLINK_NOFOLLOW : 0;
        if (__wasilibc_nocwd_fstatat(dirfd, relative_path, &st, atflag) == 0)
            probe_existed = 1;
    }

    int fd = __wasilibc_nocwd_openat_nomode(dirfd, relative_path, oflag);
    if (fd < 0)
        return fd;   // real failure: openat_nomode set errno — leave it for the caller
    if ((oflag & O_CREAT) && ((oflag & O_EXCL) || !probe_existed))
        __wasilibc_apply_create_mode(fd, mode);
    errno = saved_errno;   // success: hide the internal probe/chmod errno churn
    return fd;
}

// firebox#HXN: create-mode-aware mkdir on an already-resolved (dirfd, relative)
// pair. WASI's mkdirat carries no mode, so the host stamps DEFAULT_DIR_MODE
// (0755). A successful mkdir ALWAYS created the directory (it fails EEXIST
// otherwise), so honor the caller's mode unconditionally on success via
// path_chmod (mode & ~umask), exactly as Linux does. Best-effort: the directory
// exists regardless; a chmod hiccup must not fail an otherwise-successful mkdir,
// and __wasix_path_chmod does not touch the global errno.
int __wasilibc_nocwd_mkdirat_mode(int dirfd, const char *relative_path,
                                  mode_t mode) {
    int r = __wasilibc_nocwd_mkdirat_nomode(dirfd, relative_path);
    if (r == 0) {
        // firebox#HXN: as in openat_mode, don't let the internal chmod's errno
        // churn leak into a SUCCESSFUL mkdir (the errno-transparency invariant
        // the rust-toolchain break taught). Restore the post-mkdir errno.
        int saved_errno = errno;
        mode_t eff = (mode & 07777) & ~__wasilibc_umask_get();
        (void)__wasix_path_chmod(dirfd, relative_path, strlen(relative_path),
                                 (uint32_t)eff);
        errno = saved_errno;
    }
    return r;
}

int open(const char *path, int oflag, ...) {
    // WASI path_open carries no mode; capture the varargs mode (meaningful only
    // when the open may create) and thread it to the create-mode-aware core,
    // which applies it after a real create (firebox#HXN).
    mode_t mode = 0;
    if (oflag & O_CREAT) {
        va_list ap;
        va_start(ap, oflag);
        mode = va_arg(ap, mode_t);
        va_end(ap);
    }

    char *relative_path;
    int dirfd = find_relpath(path, &relative_path);

    // If we can't find a preopen for it, fail as if we can't find the path.
    if (dirfd == -1) {
        errno = ENOENT;
        return -1;
    }

    return __wasilibc_nocwd_openat_mode(dirfd, relative_path, oflag, mode);
}

// See the documentation in libc.h
int __wasilibc_open_nomode(const char *path, int oflag) {
    char *relative_path;
    int dirfd = find_relpath(path, &relative_path);

    // If we can't find a preopen for it, fail as if we can't find the path.
    if (dirfd == -1) {
        errno = ENOENT;
        return -1;
    }

    return __wasilibc_nocwd_openat_nomode(dirfd, relative_path, oflag);
}

int access(const char *path, int amode) {
    char *relative_path;
    int dirfd = find_relpath(path, &relative_path);

    // If we can't find a preopen for it, fail as if we can't find the path.
    if (dirfd == -1) {
        errno = ENOENT;
        return -1;
    }

    return __wasilibc_nocwd_faccessat(dirfd, relative_path, amode, 0);
}

ssize_t readlink(
    const char *restrict path,
    char *restrict buf,
    size_t bufsize)
{
    char *relative_path;
    int dirfd = find_relpath(path, &relative_path);

    // If we can't find a preopen for it, fail as if we can't find the path.
    if (dirfd == -1) {
        errno = ENOENT;
        return -1;
    }

    return __wasilibc_nocwd_readlinkat(dirfd, relative_path, buf, bufsize);
}

int stat(const char *restrict path, struct stat *restrict buf) {
    char *relative_path;
    int dirfd = find_relpath(path, &relative_path);

    // If we can't find a preopen for it, fail as if we can't find the path.
    if (dirfd == -1) {
        errno = ENOENT;
        return -1;
    }

    return __wasilibc_nocwd_fstatat(dirfd, relative_path, buf, 0);
}

int lstat(const char *restrict path, struct stat *restrict buf) {
    char *relative_path;
    int dirfd = find_relpath(path, &relative_path);

    // If we can't find a preopen for it, fail as if we can't find the path.
    if (dirfd == -1) {
        errno = ENOENT;
        return -1;
    }

    return __wasilibc_nocwd_fstatat(dirfd, relative_path, buf, AT_SYMLINK_NOFOLLOW);
}

int utime(const char *path, const struct utimbuf *times) {
    char *relative_path;
    int dirfd = find_relpath(path, &relative_path);

    // If we can't find a preopen for it, fail as if we can't find the path.
    if (dirfd == -1) {
        errno = ENOENT;
        return -1;
    }

    return __wasilibc_nocwd_utimensat(
             dirfd, relative_path,
                     times ? ((struct timespec [2]) {
                                 { .tv_sec = times->actime },
                                 { .tv_sec = times->modtime }
                             })
                           : NULL,
                     0);
}

int utimes(const char *path, const struct timeval times[2]) {
    char *relative_path;
    int dirfd = find_relpath(path, &relative_path);

    // If we can't find a preopen for it, fail as if we can't find the path.
    if (dirfd == -1) {
        errno = ENOENT;
        return -1;
    }

    return __wasilibc_nocwd_utimensat(
             dirfd, relative_path,
                     times ? ((struct timespec [2]) {
                                 { .tv_sec = times[0].tv_sec,
				   .tv_nsec = times[0].tv_usec * 1000 },
                                 { .tv_sec = times[1].tv_sec,
				   .tv_nsec = times[1].tv_usec * 1000 },
                             })
                           : NULL,
                     0);
}

int unlink(const char *path) {
    char *relative_path;
    int dirfd = find_relpath(path, &relative_path);

    // If we can't find a preopen for it, fail as if we can't find the path.
    if (dirfd == -1) {
        errno = ENOENT;
        return -1;
    }

    // `unlinkat` imports `__wasi_path_remove_directory` even when
    // `AT_REMOVEDIR` isn't passed. Instead, use a specialized function which
    // just imports `__wasi_path_unlink_file`.
    return __wasilibc_nocwd___wasilibc_unlinkat(dirfd, relative_path);
}

int rmdir(const char *path) {
    char *relative_path;
    int dirfd = find_relpath(path, &relative_path);

    // If we can't find a preopen for it, fail as if we can't find the path.
    if (dirfd == -1) {
        errno = ENOENT;
        return -1;
    }

    return __wasilibc_nocwd___wasilibc_rmdirat(dirfd, relative_path);
}

int remove(const char *path) {
    char *relative_path;
    int dirfd = find_relpath(path, &relative_path);

    // If we can't find a preopen for it, fail as if we can't find the path.
    if (dirfd == -1) {
        errno = ENOENT;
        return -1;
    }

    // First try to remove it as a file.
    int r = __wasilibc_nocwd___wasilibc_unlinkat(dirfd, relative_path);
    if (r != 0 && (errno == EISDIR || errno == ENOENT)) {
        // That failed, but it might be a directory.
        r = __wasilibc_nocwd___wasilibc_rmdirat(dirfd, relative_path);

        // If it isn't a directory, we lack capabilities to remove it as a file.
        if (errno == ENOTDIR)
            errno = ENOENT;
    }
    return r;
}

int mkdir(const char *path, mode_t mode) {
    char *relative_path;
    int dirfd = find_relpath(path, &relative_path);

    // If we can't find a preopen for it, fail as if we can't find the path.
    if (dirfd == -1) {
        errno = ENOENT;
        return -1;
    }

    return __wasilibc_nocwd_mkdirat_mode(dirfd, relative_path, mode);
}

mode_t umask(mode_t mode) {
    // firebox#HXN: a real per-process umask. WASI/WASIX has no kernel umask, so
    // libc maintains it (see __wasilibc_umask_value). Per POSIX, umask() sets
    // the file-mode creation mask to the low 0777 bits of `mode` and returns
    // the PREVIOUS mask; open()/openat()/mkdir()/mkdirat() then create objects
    // with (requested_mode & ~umask). Atomic swap so a concurrent umask()/open()
    // pair is well-defined.
    int prev = __atomic_exchange_n(&__wasilibc_umask_value,
                                   (int)(mode & 0777), __ATOMIC_SEQ_CST);
    return (mode_t)(prev & 0777);
}

int chmod(const char *path, mode_t mode) {
    char *relative_path;
    int dirfd = find_relpath(path, &relative_path);

    // If we can't find a preopen for it, fail as if we can't find the path.
    if (dirfd == -1) {
        errno = ENOENT;
        return -1;
    }

    return fchmodat(dirfd, relative_path, mode, 0);
}

int fchmod(int fd, mode_t mode) {
    __wasi_errno_t error = __wasix_fd_chmod(fd, (uint32_t)mode);
    if (error != 0) {
        errno = error;
        return -1;
    }
    return 0;
}

int fchmodat(int fd, const char *path, mode_t mode, int flag) {
    // Reject unknown flags per POSIX.
    if (flag & ~AT_SYMLINK_NOFOLLOW) {
        errno = EINVAL;
        return -1;
    }
    // For AT_FDCWD and absolute paths, resolve through the preopen map
    // to obtain a real dirfd + relative path, mirroring how `chmod()` and
    // other *at()-family wrappers in this file work. WASIX has no kernel
    // CWD; every path must land on a preopen, so forwarding AT_FDCWD or
    // a bare absolute path directly to the WASIX import would return
    // EBADF (AT_FDCWD is -100, not a valid WASIX fd).
    int effective_fd = fd;
    const char *effective_path = path;
    if (fd == AT_FDCWD || (path != NULL && path[0] == '/')) {
        char *relative_path;
        int dirfd = find_relpath(path, &relative_path);
        if (dirfd == -1) {
            errno = ENOENT;
            return -1;
        }
        effective_fd = dirfd;
        effective_path = relative_path;
    }
    size_t path_len = strlen(effective_path);
    __wasi_errno_t error = (flag & AT_SYMLINK_NOFOLLOW)
        ? __wasix_path_lchmod(effective_fd, effective_path, path_len, (uint32_t)mode)
        : __wasix_path_chmod(effective_fd, effective_path, path_len, (uint32_t)mode);
    if (error != 0) {
        errno = error;
        return -1;
    }
    return 0;
}

int lchmod(const char *path, mode_t mode) {
    char *relative_path;
    int dirfd = find_relpath(path, &relative_path);

    // If we can't find a preopen for it, fail as if we can't find the path.
    if (dirfd == -1) {
        errno = ENOENT;
        return -1;
    }

    return fchmodat(dirfd, relative_path, mode, AT_SYMLINK_NOFOLLOW);
}

DIR *opendir(const char *dirname) {
    char *relative_path;
    int dirfd = find_relpath(dirname, &relative_path);

    // If we can't find a preopen for it, fail as if we can't find the path.
    if (dirfd == -1) {
        errno = ENOENT;
        return NULL;
    }

    return __wasilibc_nocwd_opendirat(dirfd, relative_path);
}

int scandir(
    const char *restrict dir,
    struct dirent ***restrict namelist,
    int (*filter)(const struct dirent *),
    int (*compar)(const struct dirent **, const struct dirent **)
) {
    char *relative_path;
    int dirfd = find_relpath(dir, &relative_path);

    // If we can't find a preopen for it, fail as if we can't find the path.
    if (dirfd == -1) {
        errno = ENOENT;
        return -1;
    }

    return __wasilibc_nocwd_scandirat(dirfd, relative_path, namelist, filter, compar);
}

int symlink(const char *target, const char *linkpath) {
    char *relative_path;
    int dirfd = find_relpath(linkpath, &relative_path);

    // If we can't find a preopen for it, fail as if we can't find the path.
    if (dirfd == -1) {
        errno = ENOENT;
        return -1;
    }

    return __wasilibc_nocwd_symlinkat(target, dirfd, relative_path);
}

int link(const char *old, const char *new) {
    char *old_relative_path;
    int old_dirfd = find_relpath_alt(old, &old_relative_path);

    if (old_dirfd != -1) {
        char *new_relative_path;
        int new_dirfd = find_relpath(new, &new_relative_path);

        if (new_dirfd != -1)
            return __wasilibc_nocwd_linkat(old_dirfd, old_relative_path,
                                           new_dirfd, new_relative_path, 0);
    }

    // We couldn't find a preopen for it; fail as if we can't find the path.
    errno = ENOENT;
    return -1;
}

int rename(const char *old, const char *new) {
    char *old_relative_path;
    int old_dirfd = find_relpath_alt(old, &old_relative_path);

    if (old_dirfd != -1) {
        char *new_relative_path;
        int new_dirfd = find_relpath(new, &new_relative_path);

        if (new_dirfd != -1)
            return __wasilibc_nocwd_renameat(old_dirfd, old_relative_path,
                                             new_dirfd, new_relative_path);
    }

    // We couldn't find a preopen for it; fail as if we can't find the path.
    errno = ENOENT;
    return -1;
}

// Like `access`, but with `faccessat`'s flags argument.
int
__wasilibc_access(const char *path, int mode, int flags)
{
    char *relative_path;
    int dirfd = find_relpath(path, &relative_path);

    // If we can't find a preopen for it, fail as if we can't find the path.
    if (dirfd == -1) {
        errno = ENOENT;
        return -1;
    }

    return __wasilibc_nocwd_faccessat(dirfd, relative_path,
                                      mode, flags);
}

// Like `utimensat`, but without the `at` part.
int
__wasilibc_utimens(const char *path, const struct timespec times[2], int flags)
{
    char *relative_path;
    int dirfd = find_relpath(path, &relative_path);

    // If we can't find a preopen for it, fail as if we can't find the path.
    if (dirfd == -1) {
        errno = ENOENT;
        return -1;
    }

    return __wasilibc_nocwd_utimensat(dirfd, relative_path,
                                      times, flags);
}

// Like `stat`, but with `fstatat`'s flags argument.
int
__wasilibc_stat(const char *__restrict path, struct stat *__restrict st, int flags)
{
    char *relative_path;
    int dirfd = find_relpath(path, &relative_path);

    // If we can't find a preopen for it, fail as if we can't find the path.
    if (dirfd == -1) {
        errno = ENOENT;
        return -1;
    }

    return __wasilibc_nocwd_fstatat(dirfd, relative_path, st, flags);
}

// Like `link`, but with `linkat`'s flags argument.
int
__wasilibc_link(const char *oldpath, const char *newpath, int flags)
{
    char *old_relative_path;
    char *new_relative_path;
    int old_dirfd = find_relpath(oldpath, &old_relative_path);
    int new_dirfd = find_relpath(newpath, &new_relative_path);

    // If we can't find a preopen for it, fail as if we can't find the path.
    if (old_dirfd == -1 || new_dirfd == -1) {
        errno = ENOENT;
        return -1;
    }

    return __wasilibc_nocwd_linkat(old_dirfd, old_relative_path,
                                   new_dirfd, new_relative_path,
                                   flags);
}

// Like `__wasilibc_link`, but oldpath is relative to olddirfd.
int
__wasilibc_link_oldat(int olddirfd, const char *oldpath, const char *newpath, int flags)
{
    char *new_relative_path;
    int new_dirfd = find_relpath(newpath, &new_relative_path);

    // If we can't find a preopen for it, fail as if we can't find the path.
    if (new_dirfd == -1) {
        errno = ENOENT;
        return -1;
    }

    return __wasilibc_nocwd_linkat(olddirfd, oldpath,
                                   new_dirfd, new_relative_path,
                                   flags);
}

// Like `__wasilibc_link`, but newpath is relative to newdirfd.
int
__wasilibc_link_newat(const char *oldpath, int newdirfd, const char *newpath, int flags)
{
    char *old_relative_path;
    int old_dirfd = find_relpath(oldpath, &old_relative_path);

    // If we can't find a preopen for it, fail as if we can't find the path.
    if (old_dirfd == -1) {
        errno = ENOENT;
        return -1;
    }

    return __wasilibc_nocwd_linkat(old_dirfd, old_relative_path,
                                   newdirfd, newpath,
                                   flags);
}

// Like `rename`, but from is relative to fromdirfd.
int
__wasilibc_rename_oldat(int fromdirfd, const char *from, const char *to)
{
    char *to_relative_path;
    int to_dirfd = find_relpath(to, &to_relative_path);

    // If we can't find a preopen for it, fail as if we can't find the path.
    if (to_dirfd == -1) {
        errno = ENOENT;
        return -1;
    }

    return __wasilibc_nocwd_renameat(fromdirfd, from, to_dirfd, to_relative_path);
}

// Like `rename`, but to is relative to todirfd.
int
__wasilibc_rename_newat(const char *from, int todirfd, const char *to)
{
    char *from_relative_path;
    int from_dirfd = find_relpath(from, &from_relative_path);

    // If we can't find a preopen for it, fail as if we can't find the path.
    if (from_dirfd == -1) {
        errno = ENOENT;
        return -1;
    }

    return __wasilibc_nocwd_renameat(from_dirfd, from_relative_path, todirfd, to);
}
