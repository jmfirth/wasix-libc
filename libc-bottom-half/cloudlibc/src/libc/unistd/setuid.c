#include <wasi/api_firebox.h>
#include <errno.h>
#include <unistd.h>

/* firebox#K9N — setuid(2) via the #BDY proc_setcred import (which=0).
 * POSIX privilege rules are HOST-side (euid==0 privileged → all three of
 * ruid/euid/suid set; unprivileged → euid only, to ruid or suid, else EPERM;
 * (uid_t)-1 → EINVAL). Trailing args are ignored by which=0; pass 0. */
int setuid(uid_t uid) {
    __wasi_errno_t err = __wasix_proc_setcred(0, (uint32_t)uid, 0, 0);
    if (err != __WASI_ERRNO_SUCCESS) {
        errno = (int)err;
        return -1;
    }
    return 0;
}
