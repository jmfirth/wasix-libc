#include <wasi/api_firebox.h>
#include <errno.h>
#include <unistd.h>

/* firebox#K9N — setregid(2) via the #BDY proc_setcred import (which=4).
 * The gid twin of setreuid: -1 = unchanged, Linux saved-gid auto-update
 * host-side, privilege still euid-based. Trailing arg ignored; pass 0. */
int setregid(gid_t rgid, gid_t egid) {
    __wasi_errno_t err =
        __wasix_proc_setcred(4, (uint32_t)rgid, (uint32_t)egid, 0);
    if (err != __WASI_ERRNO_SUCCESS) {
        errno = (int)err;
        return -1;
    }
    return 0;
}
