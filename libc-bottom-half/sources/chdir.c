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
// Both are written only under the cwd lock and read without it, which is why
// every access goes through these. `__wasilibc_cwd_generation` counts
// publications of the mirror.
static int __wasilibc_cwd_is_synced = 0;
static unsigned __wasilibc_cwd_generation = 0;
#define cwd_synced() __atomic_load_n(&__wasilibc_cwd_is_synced, __ATOMIC_ACQUIRE)
#define cwd_set_synced(v) __atomic_store_n(&__wasilibc_cwd_is_synced, (v), __ATOMIC_RELEASE)
#define cwd_generation() __atomic_load_n(&__wasilibc_cwd_generation, __ATOMIC_ACQUIRE)

// Checks that `path` names a directory this process can reach, and hands back
// the legacy spelling of it (malloc'd): "/" + preopen prefix + "/" + rest,
// not normalized.
static int chdir_resolve(const char *path, char **new_cwd_out)
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
    *new_cwd_out = new_cwd;
    return 0;
}

// Caller holds the cwd lock. Takes ownership of `new_cwd` and returns the
// string it replaced when that one is the mirror's to free, which the caller
// does after unlocking: nothing with a loop in it runs under this lock here.
static char *cwd_publish_locked(char *new_cwd)
{
    char *prev_cwd = __wasilibc_cwd_mallocd ? __wasilibc_cwd : NULL;
    __wasilibc_cwd = new_cwd;
    __wasilibc_cwd_mallocd = 1;
    __atomic_store_n(&__wasilibc_cwd_generation, __wasilibc_cwd_generation + 1, __ATOMIC_RELEASE);
    return prev_cwd;
}

// The mirror could not be refilled. Counts as a publication, so a refill that
// asked its question before this one failed asks again instead of declaring
// the mirror synced with an answer that may predate the change behind it.
static void cwd_mark_unsynced(void)
{
    __wasilibc_cwd_lock();
    cwd_set_synced(0);
    __atomic_store_n(&__wasilibc_cwd_generation, __wasilibc_cwd_generation + 1, __ATOMIC_RELEASE);
    __wasilibc_cwd_unlock();
}

// Moves the libc mirror ONLY; the runtime's cwd is untouched. Kept for callers
// of this exported name. chdir() below no longer goes through it.
int chdir_legacy(const char *path)
{
    char *new_cwd;
    if (chdir_resolve(path, &new_cwd) != 0)
        return -1;
    __wasilibc_cwd_lock();
    char *prev_cwd = cwd_publish_locked(new_cwd);
    __wasilibc_cwd_unlock();
    free(prev_cwd);
    return 0;
}

// firebox#PHH: the mirror is a cache of the runtime's cwd, and it is filled in
// ONE way: ask the runtime, then publish the answer only if no other
// publication happened since the question was asked; otherwise ask again.
// chdir() and fchdir() change the runtime's cwd first and then refill; the
// first sync in make_absolute() refills.
//
// Before this there were three ways, and they disagreed. The first sync tested
// its flag and queried the runtime with no lock, so an answer read before
// another thread's chdir could be published after it and pin the old directory
// for good. chdir() published its own spelling of the new directory BEFORE the
// runtime had accepted the change, so a refused chdir still moved the mirror,
// and two chdir() calls could reach the mirror and the runtime in opposite
// orders. Now nothing is published for a change the runtime refused, and
// whichever refill publishes last asked its question after the last change:
// a refill that asked earlier finds the generation moved and asks again.
//
// The runtime is asked with NO lock held, and the lock covers only the
// compare-and-publish, which has no call and no loop in it. That is deliberate:
// the runtime delivers signals inside its calls and at loop back-edges, a
// handler may resolve a relative pathname, and that takes this lock.
//
// What this does not give: while one thread is inside chdir(), between the
// runtime's change and the refill, another thread can see getcwd() report the
// new directory and still resolve a relative name in the old one. The order
// before this change had the same window the other way round.
//
// `only_if_unsynced` is the first sync: it gives up its answer if another
// refill has synced the mirror meanwhile. Returns 0, or -1 with errno set to
// what the runtime reported (ENOMEM only when an allocation failed) and the
// mirror marked UNSYNCED, so the next resolution asks again rather than
// trusting a value that merely looks current.
static int cwd_refresh(int only_if_unsynced)
{
    for (;;) {
        unsigned asked_at = cwd_generation();

        // The common cwd fits the first block. A longer one is sized from
        // the length the runtime reports with ERANGE; a runtime that reports
        // none still terminates, by doubling. Each block is zeroed and one
        // byte longer than the runtime is told, so the string is terminated
        // whether or not the runtime writes the terminator itself.
        size_t cap = 256;
        char *fresh = NULL;
        __wasi_errno_t error;
        for (;;) {
            fresh = calloc(1, cap + 1);
            if (fresh == NULL) {
                cwd_mark_unsynced();
                errno = ENOMEM;
                return -1;
            }
            __wasi_pointersize_t size = cap;
            error = __wasi_getcwd((uint8_t *)fresh, &size);
            if (error != __WASI_ERRNO_RANGE)
                break;
            free(fresh);
            fresh = NULL;
            cap = (size_t)size > cap ? (size_t)size : cap * 2;
            if (cap > (size_t)1 << 24)
                break; // still ERANGE: not a pathname any caller could use
        }
        if (error != 0) {
            free(fresh);
            cwd_mark_unsynced();
            errno = __wasilibc_errno_from_wasi(error);
            return -1;
        }

        __wasilibc_cwd_lock();
        if (only_if_unsynced && __wasilibc_cwd_is_synced) {
            __wasilibc_cwd_unlock();
            free(fresh);
            return 0;
        }
        if (__wasilibc_cwd_generation != asked_at) {
            // Someone published since we asked; our answer may predate
            // their change.
            __wasilibc_cwd_unlock();
            free(fresh);
            continue;
        }
        char *prev_cwd = cwd_publish_locked(fresh);
        cwd_set_synced(1);
        __wasilibc_cwd_unlock();
        free(prev_cwd);
        return 0;
    }
}

// firebox#1K9: re-read the runtime's authoritative cwd into the libc mirror,
// for fchdir() (fchdir.c), which moves the cwd by DESCRIPTOR and has no path.
// Returns 0 on success; on failure the mirror is left UNSYNCED (see
// cwd_refresh).
int __wasilibc_resync_cwd(void)
{
    return cwd_refresh(0);
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
    // If the libc has not synced with the runtime yet, ask the runtime for
    // the cwd. On failure errno is the runtime's.
    if (!cwd_synced() && cwd_refresh(1) != 0)
        return NULL;

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
            errno = ENOMEM;
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
        // errno is make_absolute's: the runtime's own error when the cwd could
        // not be read, ENOMEM only for a failed allocation.
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
    // The libc-side check comes first and decides nothing but the error: it
    // tells "not a directory" from "no such entry", which the runtime folds
    // into ENOENT. Its spelling of the directory is not what gets published.
    char *legacy_spelling;
    if (chdir_resolve(path, &legacy_spelling) != 0) {
        // chdir_resolve already sets errno
        return -1;
    }
    free(legacy_spelling);

    // Change the runtime's cwd, then publish what it reports (firebox#PHH, at
    // cwd_refresh): nothing is published for a change the runtime refused,
    // and the mirror is the runtime's own answer.
    __wasi_errno_t error = __wasi_chdir(path);
    if (error != 0) {
      errno = __wasilibc_errno_from_wasi(error);
      return -1;
    }

    // The cwd HAS moved. A failed re-read leaves the mirror UNSYNCED for the
    // next resolution to retry; it is not a failed chdir.
    int saved_errno = errno;
    (void)cwd_refresh(0);
    errno = saved_errno;
    return 0;
}
