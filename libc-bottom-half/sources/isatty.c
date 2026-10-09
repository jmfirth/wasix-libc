#include <wasi/api.h>
#include <__errno.h>
#include <__errno_values.h>
#include <wasi/libc.h>
#include <__function___isatty.h>

int __isatty(int fd) {
    __wasi_tty_t tty;
    int r = __wasilibc_tty_get_for_fd(fd, &tty);
    if (r != 0) {
        errno = __wasilibc_errno_from_wasi(r);
        return 0;
    }

    return 1;
}
extern __typeof(__isatty) isatty __attribute__((weak, alias("__isatty")));
