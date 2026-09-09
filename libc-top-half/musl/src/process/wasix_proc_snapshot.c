#include <unistd.h>
#include <errno.h>
#include <wasi/libc.h>

#if !defined(__wasilibc_unmodified_upstream)
#include <wasi/api.h>
#endif

int wasix_proc_snapshot() {
    int err = __wasi_proc_snapshot();

    if (err != 0) {
        errno = __wasilibc_errno_from_wasi(err);
        return -1;
    }

    return 0;
}