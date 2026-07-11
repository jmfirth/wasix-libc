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
/* firebox#KZ0 — F_GETLK conflict readback (out = uint64_t[4]). intptr_t out ptr is
 * width-agnostic (i32 wasm32 / i64 wasm64), matching the host WasmPtr<u64,M>. */
int32_t __imported_wasix_fbx_fd_getlk(int32_t,int32_t,int32_t,int64_t,int64_t,intptr_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("fd_getlk")));
__wasi_errno_t __wasix_fd_getlk(__wasi_fd_t fd,uint32_t l_type,uint32_t whence,int64_t start,int64_t len,uint64_t *out){return (uint16_t)__imported_wasix_fbx_fd_getlk((int32_t)fd,(int32_t)l_type,(int32_t)whence,start,len,(intptr_t)out);}
/* firebox#KZ0 — interval-timer remaining-time readback (out = uint64_t[4]). */
int32_t __imported_wasix_fbx_itimer_get(int32_t,intptr_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("itimer_get")));
__wasi_errno_t __wasix_itimer_get(uint32_t sig,uint64_t *out){return (uint16_t)__imported_wasix_fbx_itimer_get((int32_t)sig,(intptr_t)out);}
int32_t __imported_wasix_fbx_fd_chmod(int32_t,int32_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("fd_chmod")));
__wasi_errno_t __wasix_fd_chmod(__wasi_fd_t fd,uint32_t mode){return (uint16_t)__imported_wasix_fbx_fd_chmod((int32_t)fd,(int32_t)mode);}
int32_t __imported_wasix_fbx_path_chmod(int32_t,intptr_t,intptr_t,int32_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("path_chmod")));
__wasi_errno_t __wasix_path_chmod(__wasi_fd_t fd,const char *path,size_t path_len,uint32_t mode){return (uint16_t)__imported_wasix_fbx_path_chmod((int32_t)fd,(intptr_t)path,(intptr_t)path_len,(int32_t)mode);}
int32_t __imported_wasix_fbx_path_lchmod(int32_t,intptr_t,intptr_t,int32_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("path_lchmod")));
__wasi_errno_t __wasix_path_lchmod(__wasi_fd_t fd,const char *path,size_t path_len,uint32_t mode){return (uint16_t)__imported_wasix_fbx_path_lchmod((int32_t)fd,(intptr_t)path,(intptr_t)path_len,(int32_t)mode);}
/* firebox#2E2 — chown(2) family. path/path_len are intptr_t so ONE declaration
   resolves under both wasix_32v1 and wasix_64v1, exactly like the chmod imports
   above. uid/gid are uint32_t; 0xFFFFFFFF is the "leave unchanged" sentinel. */
int32_t __imported_wasix_fbx_fd_chown(int32_t,int32_t,int32_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("fd_chown")));
__wasi_errno_t __wasix_fd_chown(__wasi_fd_t fd,uint32_t uid,uint32_t gid){return (uint16_t)__imported_wasix_fbx_fd_chown((int32_t)fd,(int32_t)uid,(int32_t)gid);}
int32_t __imported_wasix_fbx_path_chown(int32_t,intptr_t,intptr_t,int32_t,int32_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("path_chown")));
__wasi_errno_t __wasix_path_chown(__wasi_fd_t fd,const char *path,size_t path_len,uint32_t uid,uint32_t gid){return (uint16_t)__imported_wasix_fbx_path_chown((int32_t)fd,(intptr_t)path,(intptr_t)path_len,(int32_t)uid,(int32_t)gid);}
int32_t __imported_wasix_fbx_path_lchown(int32_t,intptr_t,intptr_t,int32_t,int32_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("path_lchown")));
__wasi_errno_t __wasix_path_lchown(__wasi_fd_t fd,const char *path,size_t path_len,uint32_t uid,uint32_t gid){return (uint16_t)__imported_wasix_fbx_path_lchown((int32_t)fd,(intptr_t)path,(intptr_t)path_len,(int32_t)uid,(int32_t)gid);}
int32_t __imported_wasix_fbx_path_mknod(int32_t,intptr_t,intptr_t,int32_t,int64_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("path_mknod")));
__wasi_errno_t __wasix_path_mknod(__wasi_fd_t fd,const char *path,size_t path_len,uint32_t mode,uint64_t dev){return (uint16_t)__imported_wasix_fbx_path_mknod((int32_t)fd,(intptr_t)path,(intptr_t)path_len,(int32_t)mode,(int64_t)dev);}

#ifdef __wasm64__
int32_t __imported_wasix_fbx_fd_ioctl(int32_t,int32_t,intptr_t,intptr_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("fd_ioctl")));
__wasi_errno_t __wasi_fd_ioctl(__wasi_fd_t fd,uint32_t request,void *argp,int32_t *retptr0){return (uint16_t)__imported_wasix_fbx_fd_ioctl((int32_t)fd,(int32_t)request,(intptr_t)argp,(intptr_t)retptr0);}
#endif

/* firebox#61X (Stage-1 1b) — the cross-process shared-memory window import.
 *
 * Maps the POSIX shm object identified by `inode` (the #ADZ/#TPH link-identity
 * inode the guest's /dev/shm fd resolves to) into this process's shared window
 * — the fixed top region of the wasm32 linear memory 1a/1b reserve. On success
 * (return 0) *ret_offset receives the guest linear-memory BYTE OFFSET where a
 * raw store/load now aliases the object's physical bytes, coherent with every
 * other mapping of the same inode (a sibling process's independent sem_open, or
 * a proc_fork child's re-aliased window). A nonzero return means the memory has
 * no window (wasm64 / non-Static heap / the browser js/v8 backends — the §4-B3
 * reasoned known-gap) or the OS object could not be mapped; mman.c then falls
 * back to the private aligned_alloc+pread emulation (never silently wrong).
 *
 * `ret_offset` is a guest pointer passed as intptr_t (i32 on wasm32, i64 on
 * wasm64 — host sig WasmPtr<u64, M>), so the import resolves under both
 * wasix_32v1 and wasix_64v1, exactly like the wrappers above. The host
 * registers it as "shm_map" (wasmer firebox-patches, lib/wasix/src/lib.rs).
 * Pulled by any mmap()-using program (mman.o references the wrapper), so it
 * pairs with the firebox runtime that provides it — same ship-together
 * contract as the firebox imports above. */
int32_t __imported_wasix_fbx_shm_map(int64_t,int64_t,intptr_t,intptr_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("shm_map")));
int32_t __wasilibc_shm_map(uint64_t inode,uint64_t len,uint64_t *ret_offset,uint32_t *ret_created){return __imported_wasix_fbx_shm_map((int64_t)inode,(int64_t)len,(intptr_t)ret_offset,(intptr_t)ret_created);}

/* firebox#V12 (Stage-1 1c gap #3) — the shared-window UNLINK import.
 *
 * Invalidates the host-global shared-window segment for `inode`: the host drops
 * its registry entry and shm_unlink()s the backing OS object. The guest calls it
 * from shm_unlink()/sem_unlink() (shm_open.c) after a successful unlink() of a
 * /dev/shm object it had mapped, because the firebox VFS RECYCLES st_ino across
 * an unlink+recreate — so without this a later sem_open of a NEW object that
 * reuses the freed inode would resolve to the PRIOR object's window slot (and OS
 * object), reading back the wrong value (sem_unlink/6-1, 9-1). Always returns 0;
 * a name never mapped, or a build against a runtime with no window (browser),
 * is a benign no-op. Ships together with the matching runtime, like shm_map. */
int32_t __imported_wasix_fbx_shm_unmap(int64_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("shm_unmap")));
int32_t __wasilibc_shm_unmap(uint64_t inode){return __imported_wasix_fbx_shm_unmap((int64_t)inode);}

/* firebox#SAH (native-only, #5WQ mechanism) — the HOST mprotect import.
 *
 * WebAssembly has no MMU, so the no-MMU mman.c emulation backs every mapping
 * with plain malloc — a guest access to a PROT_READ/PROT_NONE region simply
 * SUCCEEDS and no SIGSEGV/SIGBUS is ever delivered (the documented no-MMU gap).
 * The NATIVE fix: the HOST `region::protect`s the slice of the wasm linear
 * memory that backs a protected guest mapping. A guest access that violates the
 * protection then faults on the HOST — a real SIGSEGV that wasmer's trap handler
 * catches and maps to a guest SIGSEGV. mman.c calls this on a restrictive-prot
 * mmap()/mprotect() and on munmap() (→ PROT_NONE), closing the mmap
 * memory-protection POSIX family (mmap/6-*, mmap/11-*, munmap/1-*).
 *
 * `offset`/`len` are guest linear-memory coordinates passed as int64_t (a wasm32
 * guest zero-extends its 32-bit pointer; wasm64 passes the full 64-bit value),
 * so the import resolves under both wasix_32v1 and wasix_64v1 — host sig
 * (u64,u64,i32)->i32. `prot` is the POSIX prot mask (PROT_NONE/READ/WRITE/EXEC).
 * Returns 0 on success, -1 on error (bad alignment/bounds, host mprotect fail).
 *
 * NATIVE-ONLY: the browser js/v8 backends return -1 (the WebAssembly.Memory
 * backing has no page-wise mprotect). The capability is declared ABSENT on the
 * browser profile via sysconf(_SC_MEMORY_PROTECTION) == -1 (capability-profiles
 * #1BK native-only registry). A -1 here means "not enforced" — the caller leaves
 * the mapping RW; a mapping is never FAILED for lack of enforcement. Ships
 * together with the matching runtime, same contract as shm_map. */
int32_t __imported_wasix_fbx_mprotect_host(int64_t,int64_t,int32_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("mprotect_host")));
int __wasilibc_mprotect_host(uintptr_t offset,uintptr_t len,int prot){return __imported_wasix_fbx_mprotect_host((int64_t)offset,(int64_t)len,(int32_t)prot);}

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

/* firebox#8B5 HYBRID — the cooperative signal-poll import.
 *
 * The runtime (jmfirth/wasmer#firebox-patches) registers a host function under
 * the WASIX namespace's `__fbx_signal_poll` field. Its host body drives the
 * existing signal-drain (`do_pending_operations` → `process_signals_and_exit`):
 * a pending/due signal with a handler is dispatched to `__wasm_signal` and the
 * caller RESUMES; a no-handler default-terminate signal terminates the process
 * (rc=128+signo). It returns an i32 errno (always Success today; reserved).
 *
 * The wasmer `SignalPoll` compiler middleware injects a *throttled* `Call` to
 * THIS import's function-index at every wasm loop header, so a thread spinning
 * in JIT'd wasm periodically reaches the host drain — the faithful fix for
 * "a signal cannot reach a thread spinning in pure JIT'd wasm" (#8B5). The
 * middleware CANNOT add the import itself (adding an import shifts every
 * FunctionIndex and invalidates the parsed module), so wasix-libc declares it
 * here and the middleware injects a call to the already-present index.
 *
 * IMPORT-NAME = exactly "__fbx_signal_poll" (the middleware scans the module's
 * imports for this field). The C wrapper `__fbx_signal_poll` is force-linked
 * from sigaction.c's `__firebox_force_link_signals[]` so the import survives
 * wasm-ld's GC in every program (crt1 → __wasi_init_signals always pulls
 * sigaction.o). If a program is built against an OLD runtime that lacks the
 * host registration, the import is unresolved at instantiate time — but the
 * middleware is the only caller and it is only injected by the matching
 * runtime, so the pair always ships together. */
int32_t __imported_wasix_fbx_signal_poll(void) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("__fbx_signal_poll")));
int32_t __fbx_signal_poll(void){return __imported_wasix_fbx_signal_poll();}

/* firebox#MHZ/#BDY (heavy half firebox#K9N) — the per-process POSIX credential
 * imports. The host (jmfirth/wasmer#firebox-patches, lib/wasix/src/syscalls/
 * wasix/proc_{getcred,getgroups,setcred,setgroups}.rs) owns the credential in
 * WasiProcess; these wrappers are the guest ends of the pinned ABI (see
 * api_firebox.h for the full contract). Pointer/length args follow the
 * intptr_t width convention of the wrappers above (host WasmPtr<_, M> /
 * M::Offset → i32 on wasm32, i64 on wasm64, resolving under both wasix_32v1
 * and wasix_64v1); proc_setcred is all-scalar (host fn is NOT generic over
 * MemorySize — four i32 args either width). `ret_count` in proc_getgroups
 * points at an M::Offset-sized slot, i.e. a size_t guest-side. Ships together
 * with the matching runtime, same contract as shm_map. */
int32_t __imported_wasix_fbx_proc_getcred(intptr_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("proc_getcred")));
__wasi_errno_t __wasix_proc_getcred(uint32_t *cred6){return (uint16_t)__imported_wasix_fbx_proc_getcred((intptr_t)cred6);}
int32_t __imported_wasix_fbx_proc_getgroups(intptr_t,intptr_t,intptr_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("proc_getgroups")));
__wasi_errno_t __wasix_proc_getgroups(uint32_t *buf,size_t buf_len,size_t *ret_count){return (uint16_t)__imported_wasix_fbx_proc_getgroups((intptr_t)buf,(intptr_t)buf_len,(intptr_t)ret_count);}
int32_t __imported_wasix_fbx_proc_setcred(int32_t,int32_t,int32_t,int32_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("proc_setcred")));
__wasi_errno_t __wasix_proc_setcred(uint32_t which,uint32_t id_a,uint32_t id_b,uint32_t id_c){return (uint16_t)__imported_wasix_fbx_proc_setcred((int32_t)which,(int32_t)id_a,(int32_t)id_b,(int32_t)id_c);}
int32_t __imported_wasix_fbx_proc_setgroups(intptr_t,intptr_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("proc_setgroups")));
__wasi_errno_t __wasix_proc_setgroups(const uint32_t *buf,size_t buf_len){return (uint16_t)__imported_wasix_fbx_proc_setgroups((intptr_t)buf,(intptr_t)buf_len);}

/* firebox#R63 — sched_set*(2) check_same_owner write-permission probe. All-scalar
 * (pid in, __wasi_errno_t out), host fn NOT generic over MemorySize (like
 * proc_setcred) → registered identically in wasix_32v1 and wasix_64v1. The guest
 * sched_setparam/sched_setscheduler wrappers route their permission check through
 * this rather than kill(pid, 0): sched uses the kernel's EUID-ONLY
 * check_same_owner, whereas kill(2) is ruid-INCLUSIVE, and the two diverge under a
 * partial (seteuid) privilege drop (see api_firebox.h / sched_impl.h). */
int32_t __imported_wasix_fbx_sched_check_owner(int32_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("sched_check_owner")));
__wasi_errno_t __wasix_sched_check_owner(uint32_t pid){return (uint16_t)__imported_wasix_fbx_sched_check_owner((int32_t)pid);}

/* firebox#1GJ — setrlimit(RLIMIT_NOFILE) host wire. Applies the (soft, hard)
 * fd-count limit to the host fd table (which returns EMFILE past the soft limit);
 * the guest setrlimit.c could previously only echo NOFILE into its BSS table
 * (firebox#KZ1) with nothing enforcing it. Both args are rlim_t (64-bit on every
 * width), so the import takes int64_t/int64_t — NOT intptr_t — matching the host's
 * fixed u64/u64 signature, registered identically in wasix_32v1 and wasix_64v1. */
int32_t __imported_wasix_fbx_resource_set_nofile(int64_t,int64_t) __attribute__((__import_module__(FBX_WASIX_V1),__import_name__("resource_set_nofile")));
__wasi_errno_t __wasix_resource_set_nofile(uint64_t soft,uint64_t hard){return (uint16_t)__imported_wasix_fbx_resource_set_nofile((int64_t)soft,(int64_t)hard);}
