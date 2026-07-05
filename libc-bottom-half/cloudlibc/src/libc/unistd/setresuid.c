#define _GNU_SOURCE
#include <wasi/api_firebox.h>
#include <errno.h>
#include <unistd.h>

/* firebox#K9N — setresuid(2) via the #BDY proc_setcred import (which=2).
 * (uid_t)-1 = unchanged per-field; unprivileged each SET field must be one of
 * the current {ruid,euid,suid} (host-side rules); NO saved-id auto-update
 * (which is exactly why seteuid routes through here). NEW symbol on wasix —
 * the prototype is un-guarded from musl's unistd.h on the firebox fork. */
int setresuid(uid_t ruid, uid_t euid, uid_t suid) {
    __wasi_errno_t err = __wasix_proc_setcred(2, (uint32_t)ruid,
                                              (uint32_t)euid, (uint32_t)suid);
    if (err != __WASI_ERRNO_SUCCESS) {
        errno = (int)err;
        return -1;
    }
    return 0;
}
