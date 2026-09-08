#include <wasix/closure.h>
#include <errno.h>
#include <wasi/libc.h>

int wasix_closure_free(wasix_function_pointer_t closure)
{
    int err = __wasi_closure_free(closure);

    if (err != __WASI_ERRNO_SUCCESS)
    {
        errno = __wasilibc_errno_from_wasi(err);
        return -1;
    }

    return 0;
}