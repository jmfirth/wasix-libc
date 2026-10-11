#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wasi/libc-find-relpath.h>
#include <wasi/libc.h>

#ifdef _REENTRANT
void __wasilibc_cwd_lock(void);
void __wasilibc_cwd_unlock(void);
#else
#define __wasilibc_cwd_lock() (void)0
#define __wasilibc_cwd_unlock() (void)0
#endif
extern char *__wasilibc_cwd;
static int __wasilibc_cwd_mallocd = 0;
static int __wasilibc_cwd_is_synced = 0;

int chdir_legacy(const char *path)
{
    // firebox#PHH: owned by this call. Upstream keeps one static buffer here,
    // which two threads calling chdir() at once would both resize and fill.
    char *relative_buf = NULL;
    size_t relative_buf_len = 0;
    // Find a preopen'd directory as well as a relative path we're anchored
    // from which we're changing directories to.
    const char *abs;
    int parent_fd = __wasilibc_find_relpath_alloc(path, &abs, &relative_buf, &relative_buf_len, 1);
    if (parent_fd == -1) {
        free(relative_buf);
        return -1;
    }
    // Make sure that this directory we're accessing is indeed a directory.
    struct stat dirinfo;
    int ret = fstatat(parent_fd, relative_buf, &dirinfo, 0);
    if (ret == -1) {
        free(relative_buf);
        return -1;
    }
    if (!S_ISDIR(dirinfo.st_mode)) {
        free(relative_buf);
        errno = ENOTDIR;
        return -1;
    }
        
    // Create a string that looks like:
    //
    //    __wasilibc_cwd = "/" + abs + "/" + relative_buf
    //
    // If `relative_buf` is equal to "." or `abs` is equal to the empty string,
    // however, we skip that part and the middle slash.
    size_t abs_len = strlen(abs);
    int copy_relative = strcmp(relative_buf, ".") != 0;
    int mid = copy_relative && abs[0] != 0;
    char *new_cwd = malloc(1 + abs_len + mid + (copy_relative ? strlen(relative_buf) : 0) + 1);
    if (new_cwd == NULL) {
        free(relative_buf);
        errno = ENOMEM;
        return -1;
    }
    new_cwd[0] = '/';
    strcpy(new_cwd + 1, abs);
    if (mid)
        new_cwd[1 + abs_len] = '/';
    if (copy_relative)
        strcpy(new_cwd + 1 + abs_len + mid, relative_buf);
    free(relative_buf);

    __wasilibc_cwd_lock();
    char *prev_cwd = __wasilibc_cwd;
    __wasilibc_cwd = new_cwd;

    if (__wasilibc_cwd_mallocd)
        free(prev_cwd);

    __wasilibc_cwd_mallocd = 1;
    __wasilibc_cwd_unlock();
    return 0;
}

// firebox#1K9: re-read the runtime's authoritative cwd into the libc mirror.
//
// wasi-libc resolves relative paths in userspace against `__wasilibc_cwd`, so
// any operation that moves the cwd host-side WITHOUT a path in hand has to
// refresh the mirror or every later relative path resolves against a stale
// directory. `chdir()` does not need this — it has the path and feeds it to
// chdir_legacy. `fchdir()` (fchdir.c) moves the cwd by DESCRIPTOR and has no
// path, so it calls here.
//
// Returns 0 on success. On failure the mirror is left explicitly UNSYNCED so
// the next make_absolute() below re-reads it, rather than leaving a stale cwd
// sitting behind a value that looks current.
int __wasilibc_resync_cwd(void)
{
    // getcwd(NULL, 0) asks the runtime and mallocs to fit (getcwd.c), so there
    // is no buffer size to guess and no truncation case to get wrong.
    char *fresh = getcwd(NULL, 0);
    if (fresh == NULL) {
        __wasilibc_cwd_lock();
        __wasilibc_cwd_is_synced = 0;
        __wasilibc_cwd_unlock();
        return -1;
    }

    __wasilibc_cwd_lock();
    char *prev_cwd = __wasilibc_cwd;
    __wasilibc_cwd = fresh;
    if (__wasilibc_cwd_mallocd)
        free(prev_cwd);
    __wasilibc_cwd_mallocd = 1;
    __wasilibc_cwd_is_synced = 1;
    __wasilibc_cwd_unlock();
    return 0;
}

// firebox#PHH: the absolute form of `path` is built in storage that belongs to
// the CALLER — `inline_buf` when it fits, otherwise a block returned through
// `*heap` that the caller frees. Upstream builds it in one process-wide static
// buffer and fills that buffer after dropping the cwd lock, so threads
// resolving relative pathnames at once shared it: a thread was answered for
// another thread's pathname, and `__wasilibc_find_relpath_alloc` measured the
// string, sized its own buffer, then copied a string another thread had
// meanwhile made longer — a write past the end of a heap block (`git status`
// preload workers: "malloc(): heap corruption or invalid pointer").
// A static per-thread buffer would also be correct but is never released when
// its thread exits; nothing outlives the call here.
#define MAKE_ABSOLUTE_INLINE 256
static const char *make_absolute(const char *path, char *inline_buf, char **heap) {
    *heap = NULL;
    // if the libc has not synced with the runtime yet, call into the runtime to get the cwd
    if (!__wasilibc_cwd_is_synced) {
        char cwd[256];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            char *new_cwd = malloc(strlen(cwd) + 1);
            if (new_cwd == NULL)
                return NULL;
            strcpy(new_cwd, cwd);

            __wasilibc_cwd_lock();
            char *prev_cwd = __wasilibc_cwd;
            __wasilibc_cwd = new_cwd;

            if (__wasilibc_cwd_mallocd)
                free(prev_cwd);

            __wasilibc_cwd_is_synced = 1; // we synced the cwd
            __wasilibc_cwd_mallocd = 1; // we allocated so next time, prev_cwd must be freed.
            __wasilibc_cwd_unlock();
        } else {
            return NULL;
        }
    }


    // If this path is absolute, then we return it as-is.
    if (path[0] == '/') {
        return path;
    }

#ifndef _REENTRANT
    // If the path is empty, or points to the current directory, then return
    // the current directory.
    if (path[0] == 0 || !strcmp(path, ".") || !strcmp(path, "./")) {
        return __wasilibc_cwd;
    }
#endif

    // If the path starts with `./` then we won't be appending that to the cwd.
    if (path[0] == '.' && path[1] == '/')
        path += 2;

    // Otherwise we'll take the current directory, add a `/`, and then add the
    // input `path`. Note that this doesn't do any normalization (like removing
    // `/./`).
    __wasilibc_cwd_lock();
    size_t cwd_len = strlen(__wasilibc_cwd);
    size_t path_len = path ? strlen(path) : 0;
    int need_slash = __wasilibc_cwd[cwd_len - 1] == '/' ? 0 : 1;
    size_t alloc_len = cwd_len + path_len + 1 + need_slash;
    char *make_absolute_buf = inline_buf;
    if (alloc_len > MAKE_ABSOLUTE_INLINE) {
        make_absolute_buf = malloc(alloc_len);
        if (make_absolute_buf == NULL) {
            __wasilibc_cwd_unlock();
            return NULL;
        }
        *heap = make_absolute_buf;
    }
    // The cwd is copied under its lock; the rest is this call's own storage.
    strcpy(make_absolute_buf, __wasilibc_cwd);
    __wasilibc_cwd_unlock();

#ifdef _REENTRANT
    if (path[0] == 0 || !strcmp(path, ".") || !strcmp(path, "./")) {
        return make_absolute_buf;
    }
#endif

    if (need_slash)
        strcpy(make_absolute_buf + cwd_len, "/");
    strcpy(make_absolute_buf + cwd_len + need_slash, path);
    return make_absolute_buf;
}

// Helper function defined only in this object file and weakly referenced from
// `preopens.c` and `posix.c` This function isn't necessary unless `chdir` is
// pulled in because all paths are otherwise absolute or relative to the root.
int __wasilibc_find_relpath_alloc(
    const char *path,
    const char **abs_prefix,
    char **relative_buf,
    size_t *relative_buf_len,
    int can_realloc
) {
    // First, make our path absolute taking the cwd into account. `rel` below
    // points into it, so it lives until `rel` has been copied out.
    char inline_buf[MAKE_ABSOLUTE_INLINE];
    char *heap;
    const char *abspath = make_absolute(path, inline_buf, &heap);
    if (abspath == NULL) {
        errno = ENOMEM;
        return -1;
    }

    // Next use our absolute path and split it. Find the preopened `fd` parent
    // directory and set `abs_prefix`. Next up we'll be trying to fit `rel`
    // into `relative_buf`.
    const char *rel;
    int fd = __wasilibc_find_abspath(abspath, abs_prefix, &rel);
    if (fd == -1) {
        free(heap);
        return -1;
    }

    size_t rel_len = strlen(rel);
    if (*relative_buf_len < rel_len + 1) {
        if (!can_realloc) {
            free(heap);
            errno = ERANGE;
            return -1;
        }
        // firebox #4SF: realloc(NULL,n) is spec-legal (== malloc(n), C99 7.20.3.4) but a strict
        // allocator interposer (musl's flockfile-list regression test) rejects the NULL case, and
        // WASI's mandatory userspace relpath resolution makes this the FIRST allocation on every
        // path-based open (musl's raw sys_open never allocates here). Use malloc for the initial
        // fill so the allocation profile matches what portable code — and Linux musl — expects.
        char *tmp = *relative_buf ? realloc(*relative_buf, rel_len + 1) : malloc(rel_len + 1);
        if (tmp == NULL) {
            free(heap);
            errno = ENOMEM;
            return -1;
        }
        *relative_buf = tmp;
        *relative_buf_len = rel_len + 1;
    }
    strcpy(*relative_buf, rel);
    free(heap);
    return fd;
}

int chdir(const char *path)
{
    // we run the legacy chdir function that emulates real
    // current directory functionality for backwards compatibility reasons
    if (chdir_legacy(path) != 0) {
        // chdir_legacy already sets errno
        return -1;
    }

    // otherwise we let the operating system know we are changing the current path
    __wasi_errno_t error = __wasi_chdir(path);
    if (error != 0) {
      errno = __wasilibc_errno_from_wasi(error);
      return -1;
    }
    return 0;
}
