#define _GNU_SOURCE
#include <wasi/api_firebox.h>
#include <errno.h>
#include <unistd.h>

/* firebox#K9N — getresgid(2) via the #MHZ proc_getcred import: the gid triple
 * is indices {3,4,5} of the pinned {ruid, euid, suid, rgid, egid, sgid}.
 * NEW symbol (WASI upstream has none — the prototype is un-guarded from
 * musl's unistd.h on the firebox fork). Never cached. */
int getresgid(gid_t *rgid, gid_t *egid, gid_t *sgid) {
    uint32_t cred[6];
    __wasi_errno_t err = __wasix_proc_getcred(cred);
    if (err != __WASI_ERRNO_SUCCESS) {
        errno = (int)err;
        return -1;
    }
    *rgid = (gid_t)cred[3];
    *egid = (gid_t)cred[4];
    *sgid = (gid_t)cred[5];
    return 0;
}
