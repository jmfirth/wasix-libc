#include <wasi/libc.h>

__wasi_errno_t __wasilibc_tty_get_for_fd(int fd, __wasi_tty_t *tty)
{
    __wasi_fdstat_t stat;
    __wasi_errno_t error = __wasi_fd_fdstat_get(fd, &stat);
    if (error) return error;
    if (stat.fs_filetype != __WASI_FILETYPE_CHARACTER_DEVICE ||
        (stat.fs_rights_base & (__WASI_RIGHTS_FD_SEEK | __WASI_RIGHTS_FD_TELL)))
        return __WASI_ERRNO_NOTTY;

    error = __wasi_tty_get(tty);
    /* No bridge means this descriptor has no terminal. Other errors survive. */
    if (error == __WASI_ERRNO_NOTSUP) return __WASI_ERRNO_NOTTY;
    if (error) return error;
    if ((fd == 0 && tty->stdin_tty == __WASI_BOOL_TRUE) ||
        (fd == 1 && tty->stdout_tty == __WASI_BOOL_TRUE) ||
        (fd == 2 && tty->stderr_tty == __WASI_BOOL_TRUE)) return 0;
    return __WASI_ERRNO_NOTTY;
}
