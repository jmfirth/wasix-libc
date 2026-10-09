#include <wasi/api.h>
#include <__errno.h>
#include <__errno_values.h>
#include <wasi/libc.h>
#include <__function___isatty.h>

int __isatty(int fd) {
    __wasi_fdstat_t statbuf;
    int r = __wasi_fd_fdstat_get(fd, &statbuf);
    if (r != 0) {
        errno = __wasilibc_errno_from_wasi(r);
        return 0;
    }

    // A tty is a character device that we can't seek or tell on.
    if (statbuf.fs_filetype != __WASI_FILETYPE_CHARACTER_DEVICE ||
        (statbuf.fs_rights_base & (__WASI_RIGHTS_FD_SEEK | __WASI_RIGHTS_FD_TELL)) != 0) {
        /* firebox#87F: the only site in the tree that named a WASI errno
         * constant on the guest side of the boundary. This is not a
         * translation -- nothing came back from the host here, the value is
         * chosen by this function, so the guest name is simply the right name
         * for it. <__errno_values.h> is what makes ENOTTY visible; this TU
         * only had <__errno.h>, which declares the lvalue and not the codes. */
        errno = ENOTTY;
        return 0;
    }

    return 1;
}
extern __typeof(__isatty) isatty __attribute__((weak, alias("__isatty")));
