#include <wasi/api_firebox.h>
#include <unistd.h>

/* firebox#K9N — live credential read via the #MHZ proc_getcred import
 * (egid = index 4 of the pinned {ruid, euid, suid, rgid, egid, sgid}).
 * Host-authoritative, NEVER cached: a set*id must be immediately visible
 * here. POSIX: getegid() cannot fail; if the import faults (impossible with a
 * valid stack buffer) fall back to 0, the pre-#K9N constant. */
gid_t getegid(void) {
    uint32_t cred[6];
    if (__wasix_proc_getcred(cred) != __WASI_ERRNO_SUCCESS)
        return 0;
    return (gid_t)cred[4];
}
