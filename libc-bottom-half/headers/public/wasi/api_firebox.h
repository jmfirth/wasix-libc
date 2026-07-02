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

#ifdef __cplusplus
}
#endif

#endif /* __wasi_api_firebox_h */
