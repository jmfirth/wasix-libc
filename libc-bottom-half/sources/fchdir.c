/* fchdir.c — POSIX fchdir(2) against the runtime's descriptor table (firebox#1K9).
 *
 * WHY THIS EXISTS AT ALL. Upstream wasi-libc gates `fchdir` out of <unistd.h>
 * with the comment "WASI has no fchdir", which is true of *plain WASI*: it has
 * no process cwd, so there is nothing for a descriptor to become. That is an
 * UPSTREAM bound, not a Firebox one (Inv-2). Firebox's runtime owns both halves
 * of what fchdir needs — a per-process cwd (`WasiFs::current_dir`, the thing
 * `chdir`/`getcwd` already move and read) and a descriptor table in which every
 * directory inode carries its guest-absolute path (`Kind::Dir { path, .. }`).
 * So `fchdir(fd)` is a real host operation here, and gating it out was making
 * us LOOK like plain WASI while being strictly more capable.
 *
 * WHAT THE ABSENCE COST (the bug that motivated this, MEASURED). gnulib's
 * `fchdir` module notices the missing symbol and substitutes an EMULATION that
 * is a pure fd -> directory-NAME table, populated only by gnulib's own `open`
 * replacement. Guests that do not enable that replacement — GNU findutils among
 * them — hand the emulation an fd it never saw, and it answers ENOTDIR. GNU
 * `find` then printed every correct result and exited 1 from its atexit
 * `restore_cwd`. A false failure on a correct answer is the worst shape of
 * unfaithfulness (Inv-0): every script testing find's status took the error
 * branch. The class is wider than find — every gnulib guest using
 * save_cwd/restore_cwd sits in it — which is exactly why the fix belongs here
 * and not in any one package (Inv-1).
 *
 * WHY NOT RECONSTRUCT THE PATH IN THE GUEST. The portable alternative is the
 * classic userspace getcwd walk: openat(fd, "..") and readdir the parent
 * looking for a matching inode, repeatedly. It needs READ permission on every
 * ancestor directory — permissions real `fchdir(2)` never consults, because it
 * moves to an already-opened descriptor. Implementing it that way would trade
 * this false failure for a narrower one (EACCES under an unreadable ancestor),
 * which is the same defect class. The host knows the answer without asking any
 * directory's permission bits, so the host is where the answer comes from.
 *
 * THE CWD MIRROR. wasi-libc resolves relative paths in USERSPACE against
 * `__wasilibc_cwd` (see chdir.c), so moving the runtime's cwd is only half the
 * operation — the mirror has to follow or every subsequent relative path
 * resolves against a stale directory. `chdir()` keeps them in step by having a
 * path to hand to chdir_legacy. fchdir has no path, so it re-reads the
 * authoritative answer with __wasilibc_resync_cwd() (chdir.c) instead of
 * inventing a second cwd model.
 */

#include <errno.h>
#include <unistd.h>
#include <wasi/api_firebox.h>

/* Defined in chdir.c, which owns the __wasilibc_cwd mirror and its lock. */
int __wasilibc_resync_cwd(void);

int fchdir(int fd)
{
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }

    __wasi_errno_t error = __wasix_fd_chdir((__wasi_fd_t)fd);
    if (error != 0) {
        errno = error;
        return -1;
    }

    /* The cwd HAS moved: POSIX says this call succeeded, and it did. A failed
     * mirror refresh must therefore not be reported as a failed fchdir — that
     * would be the false-failure shape this file exists to remove. Resync
     * leaves the mirror marked UNSYNCED on failure, so the next relative-path
     * resolution re-reads it rather than trusting a stale value, and errno is
     * restored so a successful call never leaves a surprise behind. */
    int saved_errno = errno;
    (void)__wasilibc_resync_cwd();
    errno = saved_errno;
    return 0;
}
