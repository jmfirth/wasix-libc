#define _GNU_SOURCE
#include <wasi/api_firebox.h>
#include <errno.h>
#include <unistd.h>

/* firebox#K9N — getresuid(2) via the #MHZ proc_getcred import: the uid triple
 * is indices {0,1,2} of the pinned {ruid, euid, suid, rgid, egid, sgid}.
 * NEW symbol (WASI upstream has none — the prototype is un-guarded from
 * musl's unistd.h on the firebox fork). Never cached. */
int getresuid(uid_t *ruid, uid_t *euid, uid_t *suid) {
    uint32_t cred[6];
    __wasi_errno_t err = __wasix_proc_getcred(cred);
    if (err != __WASI_ERRNO_SUCCESS) {
        errno = (int)err;
        return -1;
    }
    *ruid = (uid_t)cred[0];
    *euid = (uid_t)cred[1];
    *suid = (uid_t)cred[2];
    return 0;
}
