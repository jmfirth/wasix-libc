/* firebox#25D / #9FB — the one reader of __wasix_port_if_list.
 *
 * getifaddrs, if_nameindex, if_nametoindex and if_indextoname all derive from
 * the same host list, so they cannot disagree about which interfaces exist.
 * The host reports the required count with EOVERFLOW; an interface can appear
 * between the two calls, so this retries rather than trusting one sizing. */
#include <wasi/api_firebox.h>
#include <stdlib.h>

__wasi_errno_t __wasix_if_list_fetch(struct __wasix_if_rec **out, size_t *count) {
    uint64_t cap = 16;
    *out = NULL;
    *count = 0;
    for (;;) {
        struct __wasix_if_rec *buf = malloc(cap ? cap * sizeof *buf : 1);
        if (buf == NULL)
            return __WASI_ERRNO_NOMEM;
        uint64_t n = cap;
        __wasi_errno_t err = __wasix_port_if_list(buf, &n);
        if (err == __WASI_ERRNO_SUCCESS) {
            *out = buf;
            *count = (size_t)n;
            return __WASI_ERRNO_SUCCESS;
        }
        free(buf);
        if (err != __WASI_ERRNO_OVERFLOW || n <= cap)
            return err;
        if (n > SIZE_MAX / sizeof *buf)
            return __WASI_ERRNO_NOMEM;
        cap = n;
    }
}
