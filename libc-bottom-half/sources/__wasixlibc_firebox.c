/* __wasixlibc_firebox.c — Firebox WASIX extension wrappers (firebox#800).
 *
 * The Firebox WASIX extensions provided by Firebox's patched wasmer runtime
 * (jmfirth/wasmer#firebox-patches): the 6 __wasix_* POSIX-permission / advisory-lock
 * wrappers, plus __wasi_fd_ioctl (wasm64 only — see below).
 *
 * WHY THIS FILE EXISTS: these used to live INSIDE the generated WASI files
 * (__wasixlibc_real.c / api_wasix.h). A header regen drops them, so the wasm64 build
 * needed wasm64-overlay.sh re-applied by hand (firebox#796/#797). Extracting them into
 * this firebox-owned source makes the regen + #if-wrap flow clean (no overlay step).
 * The Makefile auto-discovers every .c under libc-bottom-half/sources, so this
 * compiles into libc.a for both wasm32 and wasm64.
 *
 * WIDTH: the import module name is width-selected (wasix_32v1 / wasix_64v1) via
 * FBX_WASIX_V1; the wrappers are width-agnostic (intptr_t == i32 on wasm32, i64 on
 * wasm64). The compiled object is byte-for-byte equivalent to the prior inline code.
 *
 * fd_ioctl ASYMMETRY: on wasm32, __wasi_fd_ioctl is part of the committed generation
 * (__wasilibc_real.c). Only the wasm64 regen drops it, so it is defined HERE for
 * wasm64 ONLY — defining it for wasm32 too would duplicate the generated symbol.
 */
#include <wasi/api_firebox.h>

#ifdef __wasm64__
#define FBX_WASIX_V1 "wasix_64v1"
#else
#define FBX_WASIX_V1 "wasix_32v1"
#endif

int32_t __imported_wasix_fbx_fd_lock(int32_t,int32_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("fd_lock")));
__wasi_errno_t __wasix_fd_lock(__wasi_fd_t fd,uint32_t op){return (uint16_t)__imported_wasix_fbx_fd_lock((int32_t)fd,(int32_t)op);}
int32_t __imported_wasix_fbx_fd_lock_range(int32_t,int32_t,int32_t,int32_t,int64_t,int64_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("fd_lock_range")));
__wasi_errno_t __wasix_fd_lock_range(__wasi_fd_t fd,uint32_t op,uint32_t l_type,uint32_t whence,int64_t start,int64_t len){return (uint16_t)__imported_wasix_fbx_fd_lock_range((int32_t)fd,(int32_t)op,(int32_t)l_type,(int32_t)whence,start,len);}
int32_t __imported_wasix_fbx_fd_chmod(int32_t,int32_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("fd_chmod")));
__wasi_errno_t __wasix_fd_chmod(__wasi_fd_t fd,uint32_t mode){return (uint16_t)__imported_wasix_fbx_fd_chmod((int32_t)fd,(int32_t)mode);}
int32_t __imported_wasix_fbx_path_chmod(int32_t,intptr_t,intptr_t,int32_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("path_chmod")));
__wasi_errno_t __wasix_path_chmod(__wasi_fd_t fd,const char *path,size_t path_len,uint32_t mode){return (uint16_t)__imported_wasix_fbx_path_chmod((int32_t)fd,(intptr_t)path,(intptr_t)path_len,(int32_t)mode);}
int32_t __imported_wasix_fbx_path_lchmod(int32_t,intptr_t,intptr_t,int32_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("path_lchmod")));
__wasi_errno_t __wasix_path_lchmod(__wasi_fd_t fd,const char *path,size_t path_len,uint32_t mode){return (uint16_t)__imported_wasix_fbx_path_lchmod((int32_t)fd,(intptr_t)path,(intptr_t)path_len,(int32_t)mode);}
int32_t __imported_wasix_fbx_path_mknod(int32_t,intptr_t,intptr_t,int32_t,int64_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("path_mknod")));
__wasi_errno_t __wasix_path_mknod(__wasi_fd_t fd,const char *path,size_t path_len,uint32_t mode,uint64_t dev){return (uint16_t)__imported_wasix_fbx_path_mknod((int32_t)fd,(intptr_t)path,(intptr_t)path_len,(int32_t)mode,(int64_t)dev);}

#ifdef __wasm64__
int32_t __imported_wasix_fbx_fd_ioctl(int32_t,int32_t,intptr_t,intptr_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("fd_ioctl")));
__wasi_errno_t __wasi_fd_ioctl(__wasi_fd_t fd,uint32_t request,void *argp,int32_t *retptr0){return (uint16_t)__imported_wasix_fbx_fd_ioctl((int32_t)fd,(int32_t)request,(intptr_t)argp,(intptr_t)retptr0);}
#endif

/* firebox#811 Phase 2 — HOST held-list registration syscalls.
 *
 * The host (jmfirth/wasmer#firebox-patches) records (lock-word-addr, tid)
 * in WasiFutexState.held; #811 Phase 1's host thread-exit sweep clears the
 * word AND wakes waiters for every addr the exiting tid still holds. These
 * two wrappers let the musl lock primitives register/deregister their
 * lock-word addresses with that host list (see __lock.c's shared helpers).
 *
 * The single argument is the linear-memory OFFSET of the lock word
 * (host signature: WasmPtr<u32, M>). It is passed as intptr_t — i32 on
 * wasm32, i64 on wasm64 — so the import resolves under both wasix_32v1 and
 * wasix_64v1 (the #797 target split). rust-std (#496) hardcodes i32 because
 * it is wasm32-only; the libc path is dual-target, so it mirrors the
 * (intptr_t) pointer-passing convention of the wrappers above. The host
 * derives the tid; the return is always Success (best-effort: duplicate
 * register / unmatched deregister are benign no-ops host-side). */
int32_t __imported_wasix_fbx_futex_register_held(intptr_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("futex_register_held")));
int32_t __imported_wasix_fbx_futex_deregister_held(intptr_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("futex_deregister_held")));
void __firebox_host_register_held(volatile int *l){(void)__imported_wasix_fbx_futex_register_held((intptr_t)l);}
void __firebox_host_deregister_held(volatile int *l){(void)__imported_wasix_fbx_futex_deregister_held((intptr_t)l);}
