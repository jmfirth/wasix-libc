// Firebox-specific BSD flock(2) implementation (issue #243).
//
// musl's upstream flock.c calls Linux SYS_flock and is excluded from
// the wasix-libc build. This translation routes flock(fd, op) through
// the Firebox-extension WASIX import `__wasix_fd_lock`, which is
// implemented by the patched wasmer runtime
// (jmfirth/wasmer#firebox-patches branch). Without this implementation,
// `flock` becomes an unresolved import at instantiation time —
// preventing cargo / rustc / any tool that takes a session-dir lock
// from running inside Firebox.

#include <errno.h>
#include <sys/file.h>
#include <wasi/api_firebox.h> /* firebox#800: __wasix_fd_lock decl moved out of the generated api_wasix.h */

int flock(int fd, int op)
{
    __wasi_errno_t err = __wasix_fd_lock((__wasi_fd_t)fd, (uint32_t)op);
    if (err != __WASI_ERRNO_SUCCESS) {
        errno = (int)err;
        return -1;
    }
    return 0;
}
