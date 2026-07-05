#include <wasi/api_firebox.h>
#include <errno.h>
#include <unistd.h>

/* firebox#K9N — setreuid(2) via the #BDY proc_setcred import (which=1).
 * (uid_t)-1 = unchanged. The Linux saved-id auto-update (suid ← new euid iff
 * the real id was set OR the effective id was set to ≠ previous ruid) is
 * HOST-side. Trailing arg ignored by which=1; pass 0. */
int setreuid(uid_t ruid, uid_t euid) {
    __wasi_errno_t err =
        __wasix_proc_setcred(1, (uint32_t)ruid, (uint32_t)euid, 0);
    if (err != __WASI_ERRNO_SUCCESS) {
        errno = (int)err;
        return -1;
    }
    return 0;
}
