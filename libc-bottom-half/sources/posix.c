/* firebox#5X0: __FBX_THREAD_LOCAL */
#include <features.h>
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
    static __FBX_THREAD_LOCAL char *relative_buf = NULL;
    static __FBX_THREAD_LOCAL size_t relative_buf_len = 0;
    int fd = find_relpath2(path, &relative_buf, &relative_buf_len);
    // find_relpath2 can update relative_buf, so assign it after the call
    *relative = relative_buf;
    return fd;
}

// same as `find_relpath`, but uses another set of static variables to cache
static int find_relpath_alt(const char *path, char **relative) {
    static __FBX_THREAD_LOCAL char *relative_buf = NULL;
    static __FBX_THREAD_LOCAL size_t relative_buf_len = 0;
    int fd = find_relpath2(path, &relative_buf, &relative_buf_len);
    // find_relpath2 can update relative_buf, so assign it after the call
    *relative = relative_buf;
    return fd;
}

static int validate_access_pathname(const char *path, int mode, int flags) {
    if ((mode & ~(F_OK | R_OK | W_OK | X_OK)) != 0 ||
        (flags & ~(AT_EACCESS | AT_SYMLINK_NOFOLLOW)) != 0) {
        errno = EINVAL;
        return -1;
    }
    if (path[0] == '\0') {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int open(const char *path, int oflag, ...) {
    // Capture the varargs mode (meaningful only when the open may create) and
    // pass it to the runtime, which applies it in the create (firebox#DNG/#J04).
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
    // Preserve the caller's pathname before preopen resolution: make_absolute()
    // maps an empty string to the cwd for APIs where that is useful internally,
    // but Linux access(2) requires ENOENT for an empty public pathname.
    if (validate_access_pathname(path, amode, 0) != 0)
        return -1;

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
    // firebox#DNG: the mask lives in the runtime (proc_umask), as Linux's lives
    // in the kernel, so it survives exec and posix_spawn and reaches the creates
    // that pass no mode (fopen, bind, mknod). POSIX: only the low 0777 bits are
    // used, and umask() returns the previous mask; it cannot fail.
    uint32_t old = 0;
    (void)__wasix_proc_umask((uint32_t)(mode & 0777), &old);
    return (mode_t)(old & 0777);
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
        errno = __wasilibc_errno_from_wasi(error);
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
        errno = __wasilibc_errno_from_wasi(error);
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

// firebox#2E2 — the chown(2) family. These REPLACE the musl upstream
// unistd/{chown,fchown,fchownat,lchown}.c (removed from the Makefile), which
// issued a SYS_fchownat the WASIX/Firebox runtime does not provide. They mirror
// the chmod family above: resolve the guest path to a preopen dirfd + relative
// path (WASIX has no kernel CWD) and forward to the Firebox WASIX imports, which
// write the stored owner and enforce the POSIX ownership-change privilege rule
// in the runtime. A `(uid_t)-1` / `(gid_t)-1` marshals as 0xFFFFFFFF and the
// host decodes it to the "leave this field unchanged" sentinel.
int chown(const char *path, uid_t owner, gid_t group) {
    char *relative_path;
    int dirfd = find_relpath(path, &relative_path);

    // If we can't find a preopen for it, fail as if we can't find the path.
    if (dirfd == -1) {
        errno = ENOENT;
        return -1;
    }

    return fchownat(dirfd, relative_path, owner, group, 0);
}

int fchown(int fd, uid_t owner, gid_t group) {
    __wasi_errno_t error = __wasix_fd_chown(fd, (uint32_t)owner, (uint32_t)group);
    if (error != 0) {
        errno = __wasilibc_errno_from_wasi(error);
        return -1;
    }
    return 0;
}

int fchownat(int fd, const char *path, uid_t owner, gid_t group, int flag) {
    // Reject unknown flags per POSIX (mirrors fchmodat). AT_EMPTY_PATH is not
    // handled here; a chown of an fd itself goes through fchown().
    if (flag & ~AT_SYMLINK_NOFOLLOW) {
        errno = EINVAL;
        return -1;
    }
    // For AT_FDCWD and absolute paths, resolve through the preopen map to obtain
    // a real dirfd + relative path (WASIX has no kernel CWD), exactly as
    // fchmodat() does.
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
        ? __wasix_path_lchown(effective_fd, effective_path, path_len, (uint32_t)owner, (uint32_t)group)
        : __wasix_path_chown(effective_fd, effective_path, path_len, (uint32_t)owner, (uint32_t)group);
    if (error != 0) {
        errno = __wasilibc_errno_from_wasi(error);
        return -1;
    }
    return 0;
}

int lchown(const char *path, uid_t owner, gid_t group) {
    char *relative_path;
    int dirfd = find_relpath(path, &relative_path);

    // If we can't find a preopen for it, fail as if we can't find the path.
    if (dirfd == -1) {
        errno = ENOENT;
        return -1;
    }

    return fchownat(dirfd, relative_path, owner, group, AT_SYMLINK_NOFOLLOW);
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
    // Do not let find_relpath() normalize the public empty pathname to the cwd.
    // Validate first so invalid modes/flags retain Linux's EINVAL precedence.
    if (validate_access_pathname(path, mode, flags) != 0)
        return -1;

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
    // firebox#K1A: the old path MUST resolve through `find_relpath_alt`.
    // `find_relpath` hands back a pointer into a single thread-local buffer
    // that the next `find_relpath` call overwrites, so resolving both
    // endpoints through it left `old_relative_path` and `new_relative_path`
    // aliasing the SAME buffer, holding the NEW path. Every guest hard link
    // therefore reached `path_link` as link(new, new) and died ENOENT on a
    // destination that does not exist yet. `link()` and `rename()` above
    // already pair `_alt` with the plain one for exactly this reason; this
    // sibling was the one that did not, and `linkat()` routes here.
    int old_dirfd = find_relpath_alt(oldpath, &old_relative_path);
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
