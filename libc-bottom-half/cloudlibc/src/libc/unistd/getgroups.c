#include <wasi/api_firebox.h>
#include <errno.h>
#include <unistd.h>

/* firebox#K9N — getgroups(2) via the #MHZ proc_getgroups import. POSIX probe
 * semantics live HOST-side (the import always reports the true count;
 * buf_len==0 is the count-only probe; 0 < buf_len < ngroups → EINVAL), so this
 * wrapper only routes. gid_t is uint32_t on wasix, so `list` is passed
 * directly as the host's u32 buffer. A negative count is EINVAL per POSIX
 * (checked guest-side — the import's buf_len is unsigned). Never cached. */
int getgroups(int count, gid_t list[]) {
    if (count < 0) {
        errno = EINVAL;
        return -1;
    }
    size_t ret_count;
    __wasi_errno_t err = __wasix_proc_getgroups((uint32_t *)list, (size_t)count, &ret_count);
    if (err != __WASI_ERRNO_SUCCESS) {
        errno = (int)err;
        return -1;
    }
    return (int)ret_count;
}
