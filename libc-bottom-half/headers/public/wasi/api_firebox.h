/*
 * api_firebox.h — Firebox WASIX extension prototypes + constants (firebox#800).
 *
 * Firebox-OWNED (NOT generated). The prototypes for the extension wrappers defined in
 * libc-bottom-half/sources/__wasixlibc_firebox.c, plus the FIFO filetype constant.
 * Extracted from the generated api_wasix.h so a header regen no longer drops them +
 * needs the wasm64 overlay re-applied (firebox#796/#797/#798). Included by the router
 * <wasi/api.h>; safe to include directly (e.g. from flock.c).
 */
#ifndef __wasi_api_firebox_h
#define __wasi_api_firebox_h

/* The base WASI surface (self-contained) supplies __wasi_errno_t / __wasi_fd_t /
 * __WASI_ERRNO_* + pulls <stdint.h>. NOT api_wasix.h — that one is not self-contained
 * (it references base types like __wasi_timestamp_t and relies on api_wasi.h first). */
#include "api_wasi.h"
#include <stddef.h> /* size_t */

#ifdef __cplusplus
extern "C" {
#endif

/* Firebox extension: S_IFIFO filetype (path_mknod). Guarded — the wasm32 generation
 * also defines it; whichever is seen first wins, the other is skipped. */
#ifndef __WASI_FILETYPE_FIFO
#define __WASI_FILETYPE_FIFO (UINT8_C(10))
#endif

/* POSIX permission + advisory-lock extensions (jmfirth/wasmer#firebox-patches). */
__wasi_errno_t __wasix_fd_lock(__wasi_fd_t fd, uint32_t op);
__wasi_errno_t __wasix_fd_lock_range(__wasi_fd_t fd, uint32_t op, uint32_t l_type, uint32_t whence, int64_t start, int64_t len);

/* firebox#KZ0 — F_GETLK conflict readback. `out` is a caller-provided
 * uint64_t out[4] = {l_type, l_pid, l_start, l_len}; l_type is WASIX_F_UNLCK (2)
 * when the range is lockable (no conflict). Closes fork/11-1 (the child must see
 * the parent's F_WRLCK, not F_UNLCK). Additive companion to __wasix_fd_lock_range
 * — the existing fd_lock_range ABI is untouched (Inv-8). */
__wasi_errno_t __wasix_fd_getlk(__wasi_fd_t fd, uint32_t l_type, uint32_t whence, int64_t start, int64_t len, uint64_t *out);

/* firebox#KZ0 — interval-timer (alarm/setitimer) remaining-time readback. `sig`
 * is the WASI signal setitimer armed (REAL→ALRM, VIRTUAL→VTALRM, PROF→PROF).
 * `out` is a caller-provided uint64_t out[4] =
 * {it_value.tv_sec, it_value.tv_usec, it_interval.tv_sec, it_interval.tv_usec}.
 * A disarmed timer (incl. every timer in a freshly forked child) reads all
 * zeros. The host is the single source of truth; the guest tracks no timer
 * state. Closes fork/9-1 (alarm remaining) + fork/13-1 (getitimer in child). */
__wasi_errno_t __wasix_itimer_get(uint32_t sig, uint64_t *out);

__wasi_errno_t __wasix_fd_chmod(__wasi_fd_t fd, uint32_t mode);
__wasi_errno_t __wasix_path_chmod(__wasi_fd_t fd, const char *path, size_t path_len, uint32_t mode);
__wasi_errno_t __wasix_path_lchmod(__wasi_fd_t fd, const char *path, size_t path_len, uint32_t mode);
/* firebox#2E2 — chown(2) family imports (see __wasixlibc_firebox.c). */
__wasi_errno_t __wasix_fd_chown(__wasi_fd_t fd, uint32_t uid, uint32_t gid);
__wasi_errno_t __wasix_path_chown(__wasi_fd_t fd, const char *path, size_t path_len, uint32_t uid, uint32_t gid);
__wasi_errno_t __wasix_path_lchown(__wasi_fd_t fd, const char *path, size_t path_len, uint32_t uid, uint32_t gid);
/* firebox#5T3 — the owner (st_uid/st_gid) REPORT channel: the READ half of chown.
 * WASI's __wasi_filestat_t has no uid/gid, so fstat/stat/lstat/fstatat call these
 * after the standard filestat_get to fill st_uid/st_gid. `owner2` is a
 * caller-provided uint32_t owner2[2] = {uid, gid} (8 bytes). `flags` is the same
 * __wasi_lookupflags_t as __wasi_path_filestat_get (SYMLINK_FOLLOW → stat, cleared
 * → lstat). See __wasixlibc_firebox.c. */
__wasi_errno_t __wasix_fd_filestat_get_ext(__wasi_fd_t fd, uint32_t *owner2);
__wasi_errno_t __wasix_path_filestat_get_ext(__wasi_fd_t fd, uint32_t flags, const char *path, size_t path_len, uint32_t *owner2);
/* firebox#Q2Y — host-authoritative POSIX access/faccessat. `amode` is the
 * R_OK|W_OK|X_OK mask; `flags` accepts AT_EACCESS|AT_SYMLINK_NOFOLLOW. The
 * host snapshots real/effective ids plus supplementary groups atomically and
 * checks lossless mode/owner metadata including directory-prefix search.
 * ABI: this additive import changes the provider projection, so the provider
 * must be emitted under a NEW generation; preserving the prior generation is
 * invalid even though existing imports remain source-compatible. */
__wasi_errno_t __wasix_path_access(__wasi_fd_t fd, const char *path, size_t path_len, uint32_t amode, uint32_t flags);
/* firebox#1K9 — POSIX fchdir(2): move the process cwd to an already-open
 * directory DESCRIPTOR. The runtime is the only party that can answer this
 * without consulting any ancestor's permission bits, because it holds both the
 * cwd and the descriptor table in which each directory inode carries its
 * guest-absolute path. Errors are the POSIX set: EBADF (no such descriptor),
 * ENOTDIR (the descriptor is not a directory). See fchdir.c for why the guest
 * does not reconstruct the path itself. */
__wasi_errno_t __wasix_fd_chdir(__wasi_fd_t fd);
__wasi_errno_t __wasix_path_mknod(__wasi_fd_t fd, const char *path, size_t path_len, uint32_t mode, uint64_t dev);

/* fd_ioctl ASYMMETRY: on wasm32 __wasi_fd_ioctl is part of the committed generation
 * (api_wasix.h); only the wasm64 regen drops it, so it is declared here for wasm64
 * only (declaring it for wasm32 too would duplicate the generated prototype). */
#ifdef __wasm64__
__wasi_errno_t __wasi_fd_ioctl(__wasi_fd_t fd, uint32_t request, void *argp, int32_t *retptr0);
#endif

/* firebox#8B5 HYBRID — cooperative signal-poll. The wasmer `SignalPoll`
 * middleware injects throttled calls to the `__fbx_signal_poll` host import so a
 * thread spinning in JIT'd wasm reaches the host signal-drain. See
 * __wasixlibc_firebox.c for the full rationale. */
int32_t __fbx_signal_poll(void);

/* firebox#MHZ/#BDY (heavy half firebox#K9N) — the per-process POSIX credential
 * imports (jmfirth/wasmer#firebox-patches). Host-authoritative: the credential
 * lives in WasiProcess; the guest getXid/setXid wrappers call these on EVERY
 * invocation (never cached — a setuid must be immediately visible to a later
 * getuid).
 *
 * READ path (#MHZ):
 *   __wasix_proc_getcred(cred6) — writes SIX packed uint32_t at cred6 in the
 *     pinned order {ruid, euid, suid, rgid, egid, sgid} (24 bytes).
 *   __wasix_proc_getgroups(buf, buf_len, ret_count) — POSIX getgroups probe
 *     semantics: *ret_count ALWAYS receives the true supplementary count;
 *     buf_len==0 → count-only Success (buf untouched);
 *     0 < buf_len < ngroups → EINVAL; else buf is filled with ngroups entries.
 *
 * WRITE path (#BDY):
 *   __wasix_proc_setcred(which, id_a, id_b, id_c) — `which` selects the Linux
 *     KERNEL syscall (0=setuid 1=setreuid 2=setresuid 3=setgid 4=setregid
 *     5=setresgid; there is deliberately NO seteuid/setegid code — those are
 *     library functions over setresuid/setresgid, exactly as musl builds them).
 *     (uint32_t)-1 = "unchanged" for the re-/res- forms, invalid-id → EINVAL
 *     for the single-arg forms. POSIX privilege rules are enforced HOST-side
 *     (euid==0 is privileged) → EPERM/EINVAL as Linux would.
 *   __wasix_proc_setgroups(buf, buf_len) — privileged-only (EPERM otherwise);
 *     buf_len==0 clears the supplementary set. setgroups(n, list) →
 *     __wasix_proc_setgroups(list, n) (note the swapped arg order).
 */
__wasi_errno_t __wasix_proc_getcred(uint32_t *cred6);
__wasi_errno_t __wasix_proc_getgroups(uint32_t *buf, size_t buf_len, size_t *ret_count);
__wasi_errno_t __wasix_proc_setcred(uint32_t which, uint32_t id_a, uint32_t id_b, uint32_t id_c);
__wasi_errno_t __wasix_proc_setgroups(const uint32_t *buf, size_t buf_len);

/* firebox#R63 — sched_set*(2) write-permission probe. Given a target pid, returns
 * __WASI_ERRNO_SUCCESS (0) when the CALLER's credentials permit setting that
 * target's scheduling parameters per Linux check_same_owner (EUID-ONLY: caller
 * euid == target euid or ruid, or caller euid 0), __WASI_ERRNO_PERM for a foreign
 * owner, __WASI_ERRNO_SRCH for a missing/reaped pid. pid == 0 (the calling
 * process) is self → Success. This is the euid-vs-ruid sibling of kill's
 * ruid-INCLUSIVE rule: the guest sched_set* wrappers MUST route through this, NOT
 * kill(pid, 0), whose permission model gives the opposite answer under a partial
 * (seteuid) privilege drop (firebox#R63). The read/existence probe stays on kill.
 * Host: lib/wasix/src/syscalls/wasix/sched_check_owner.rs. */
__wasi_errno_t __wasix_sched_check_owner(uint32_t pid);

/* firebox#1GJ — host side of setrlimit(RLIMIT_NOFILE, …). Apply the process's
 * (soft, hard) fd-count limit to the host fd table, which is what actually
 * enforces it: an fd number allocated at or above `soft` fails with EMFILE.
 * Returns __WASI_ERRNO_SUCCESS (0) on success, or Linux's unprivileged
 * setrlimit(2) errnos surfaced verbatim: __WASI_ERRNO_INVAL when soft > hard,
 * __WASI_ERRNO_PERM for a hard-limit RAISE (the sandbox grants no
 * CAP_SYS_RESOURCE). The guest setrlimit(RLIMIT_NOFILE) wrapper routes through
 * this so the enforced limit moves; getrlimit still reads the echoed table.
 * Both args are 64-bit (rlim_t) regardless of pointer width, so the host takes
 * i64/i64 → registered identically in wasix_32v1 and wasix_64v1.
 * Host: lib/wasix/src/syscalls/wasix/resource_set_nofile.rs. */
__wasi_errno_t __wasix_resource_set_nofile(uint64_t soft, uint64_t hard);

#ifdef __cplusplus
}
#endif

#endif /* __wasi_api_firebox_h */
