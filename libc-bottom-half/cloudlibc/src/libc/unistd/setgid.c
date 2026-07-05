#include <wasi/api_firebox.h>
#include <errno.h>
#include <unistd.h>

/* firebox#K9N — setgid(2) via the #BDY proc_setcred import (which=3).
 * Privilege is euid-based even for the gid family (Linux CAP_SETGID
 * semantics, enforced host-side). (gid_t)-1 → EINVAL. */
int setgid(gid_t gid) {
    __wasi_errno_t err = __wasix_proc_setcred(3, (uint32_t)gid, 0, 0);
    if (err != __WASI_ERRNO_SUCCESS) {
        errno = (int)err;
        return -1;
    }
    return 0;
}
