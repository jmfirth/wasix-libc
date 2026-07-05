#include <wasi/api_firebox.h>
#include <errno.h>
#include <unistd.h>

/* firebox#K9N — setegid(3) via the #BDY proc_setcred import. musl idiom:
 * setegid(e) is the library function setresgid(-1, e, -1) — which=5
 * (setresgid) with rgid/sgid unchanged ((uint32_t)-1). */
int setegid(gid_t egid) {
    __wasi_errno_t err =
        __wasix_proc_setcred(5, (uint32_t)-1, (uint32_t)egid, (uint32_t)-1);
    if (err != __WASI_ERRNO_SUCCESS) {
        errno = (int)err;
        return -1;
    }
    return 0;
}
