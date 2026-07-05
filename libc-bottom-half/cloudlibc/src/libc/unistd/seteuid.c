#include <wasi/api_firebox.h>
#include <errno.h>
#include <unistd.h>

/* firebox#K9N — seteuid(3) via the #BDY proc_setcred import. musl idiom:
 * seteuid(e) is the library function setresuid(-1, e, -1) — there is
 * deliberately NO dedicated host op; which=2 (setresuid) with ruid/suid
 * unchanged ((uint32_t)-1). Host enforces POSIX rules (unprivileged euid
 * may move only to ruid/suid → EPERM). */
int seteuid(uid_t euid) {
    __wasi_errno_t err =
        __wasix_proc_setcred(2, (uint32_t)-1, (uint32_t)euid, (uint32_t)-1);
    if (err != __WASI_ERRNO_SUCCESS) {
        errno = (int)err;
        return -1;
    }
    return 0;
}
