#define _GNU_SOURCE
#include <wasi/api_firebox.h>
#include <errno.h>
#include <unistd.h>

/* firebox#K9N — setresgid(2) via the #BDY proc_setcred import (which=5).
 * The gid twin of setresuid: -1 = unchanged per-field, no auto-update
 * (setegid routes through here), privilege euid-based. NEW symbol on wasix —
 * prototype un-guarded from musl's unistd.h on the firebox fork. */
int setresgid(gid_t rgid, gid_t egid, gid_t sgid) {
    __wasi_errno_t err = __wasix_proc_setcred(5, (uint32_t)rgid,
                                              (uint32_t)egid, (uint32_t)sgid);
    if (err != __WASI_ERRNO_SUCCESS) {
        errno = (int)err;
        return -1;
    }
    return 0;
}
