// Userspace emulation of mmap and munmap. Restrictions apply.
//
// This is meant to be complete enough to be compatible with code that uses
// mmap for simple file I/O. It just allocates memory with malloc and reads
// and writes data with pread and pwrite.

#ifdef __wasilibc_unmodified_upstream
#define _WASI_EMULATED_MMAN
#else
#define _WASI_EMULATED_MMAN 1
#endif
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
// firebox#7C5/#QAB: PAGESIZE (the POSIX page size sysconf(_SC_PAGE_SIZE) reports,
// = 4096 since #QAB, matching Linux) for the offset-alignment / MAP_FIXED-addr /
// partial-page-zero-fill semantics. <limits.h> exposes PAGESIZE on wasm via
// arch/wasm{32,64}/bits/limits.h → <__macro_PAGESIZE.h>. The 64 KiB WebAssembly
// linear-memory page is a DISTINCT concept (WASIX_MMAN_HOST_PAGE_SIZE below, and
// the raw 65536 in the reservation allocator) — see the constants below.
#include <limits.h>
// firebox#TTB: the POSIX mmap(2) precondition errnos (EBADF/EACCES/EOVERFLOW)
// are derived from the file descriptor's WASI rights and the offset/length
// arithmetic. <wasi/api.h> provides __wasi_fd_fdstat_get + the
// __WASI_RIGHTS_FD_{READ,WRITE} / __WASI_FILETYPE_REGULAR_FILE constants.
#include <wasi/api.h>
// firebox#61X (Stage-1 1b-libc): struct stat / fstat / st_ino to recognise a
// /dev/shm fd at mmap time and key the host shared-window mapping.
#include <sys/stat.h>
// firebox#R23: the POSIX RLIMIT_MEMLOCK privilege/accounting gate for
// mlock/mlockall (getrlimit, struct rlimit, RLIMIT_MEMLOCK, RLIM_INFINITY,
// rlim_t). getrlimit here reads the firebox#KZ1 stored process rlimit table
// (setrlimit persists every resource, RLIMIT_MEMLOCK included). We use the
// PUBLIC getrlimit — NOT the private __wasilibc_get_stored_rlimit — because the
// emulated-mman objects are not compiled with -I headers/private (only the
// top-half objects are); getrlimit reaches the same table with no Makefile change.
#include <sys/resource.h>
#include <wasi/libc.h>

// firebox#6ZJ: PIC-THIN errno routing — reach errno through __errno_location()
// instead of the bare `errno` lvalue, so this object can be linked into a THIN
// main that dynamically links a shared libc.so.
//
// WHY (the defect this closes): scripts/wasix-libc/build.sh compiles the
// emulated-mman objects with -D__WASILIBC_BUILDING_LIBC, which vetoes the
// PIC-consumer branch of libc-bottom-half/headers/public/__errno.h. Combined
// with -ftls-model=local-exec, bare `errno` therefore emits as
// R_WASM_MEMORY_ADDR_TLS_SLEB — a link-time-constant offset into THE LINKING
// MODULE'S OWN TLS segment, reached from that module's __tls_base. In a thin
// main errno is not defined locally at all; it lives in libc.so's TLS and is
// imported as GOT.mem.errno. wasm-ld has no mechanism to manufacture a TLS
// offset for a symbol it did not lay out, so it refuses outright:
//   relocation R_WASM_MEMORY_ADDR_TLS_SLEB cannot be used against
//   non-TLS symbol `errno`
// and the archive is simply unlinkable into a thin main. That blocks a thin
// main from PROVIDEing mmap/munmap back to libc.so's `env` imports.
//
// WHY THIS SHAPE and not "just drop -D__WASILIBC_BUILDING_LIBC for mman":
// dropping the define would flip errno to the non-TLS GOT.mem extern, which is
// correct ONLY in a thin link. The same object linked into any of the ~20 FAT
// guests that carry -lwasi-emulated-mman (perl, git, cmake, ninja, ssh, bash,
// ccache, blink, …) would then reference a different cell than the rest of
// their statically-linked libc — the #99/#317 errno split-brain class, silent
// and per-thread. __errno_location() is POLARITY-AGNOSTIC: it is defined and
// exported by every libc flavor (static, fat-PIC, shared libc.so), it executes
// INSIDE the libc that owns errno, and it returns the calling thread's own
// cell by construction. One source, correct in all three linkage polarities.
//
// Cost: one indirect call per errno store, all on error paths.
//
// The guard keeps this OFF by default, so the ordinary
// libwasi-emulated-mman.a is byte-identical to what it was before #6ZJ. It is
// switched ON only by scripts/wasix-libc/build-picthin-mman.sh, which emits the
// separately-named libwasi-emulated-mman-picthin.a.
//
// ⚠️ NEVER LINK BOTH ARCHIVES INTO ONE ARTIFACT. g_fixed_maps and g_frags
// below are file-static mapping registries; two copies of this object in one
// module means two divergent registries and a mapping registered in one that
// the other cannot find.
#ifdef __FIREBOX_MMAN_PIC_THIN__
int *__errno_location(void);
#undef errno
#define errno (*__errno_location())
#endif

// firebox#61X: the process-local /dev/shm inode set (defined in
// libc-top-half/musl/src/mman/shm_open.c). A MAP_SHARED mapping of a /dev/shm
// object routes to the host shared window instead of the private
// aligned_alloc+pread emulation, making raw cross-process MAP_SHARED store/load
// coherent (fork/16-1) and sharing the named-semaphore byte region.
extern int __wasix_is_shm_inode(ino_t ino);

// firebox#GAG/#MMP/#RST: the ONE host import behind a MAP_SHARED mapping in the
// shared window, shm_file(op, fd, len, inout_offset), through the wrapper in
// libc-bottom-half/sources/__wasixlibc_firebox.c (which returns 0 or a libc
// errno — the WASI errno is translated there, so ENOTSUP below is POSIX's 95,
// never WASI's 58). The host binds the window segment to the object behind
// `fd` (keyed by the open file, so unlink and name reuse cannot alias it): it
// seeds the segment from the object itself on the first bind, keeps read(),
// write() and ftruncate() coherent with it, and writes its stores to the file
// however this process ends — exit, _exit, exec, a fatal signal — not only at
// msync/munmap. A writable regular file and a /dev/shm object take the same path.
//   WASIX_SHM_FILE_MAP      map fd's object; *inout_offset := the window address.
//   WASIX_SHM_FILE_SYNC     msync: write the segment's stores to the file now.
//   WASIX_SHM_FILE_UNMAP    this process no longer maps the object (its last
//                           munmap of it).
//   WASIX_SHM_FILE_MAP_AT   MAP at the window address *inout_offset, replacing
//                           the whole window mappings there (MAP_FIXED at a
//                           window address, mmap/3-1).
//   WASIX_SHM_FILE_RELEASE  unmap this process's window mapping at
//                           *inout_offset, leaving the range inaccessible so a
//                           later access faults (munmap/1-1, 1-2); fd ignored.
// (op 3, PROBE, is sysconf's.) ENOTSUP means this memory has no window — the
// browser, a small wasm64 memory — and MAP's caller then keeps the
// process-local private copy. Any other error is a real failure with a window
// present (full window, a live segment shorter than this mapping, a request
// larger than the window) and reaches the mmap caller.
extern int32_t __wasilibc_shm_file(uint32_t op, int fd, uint64_t len, uint64_t *inout_offset);
#define WASIX_SHM_FILE_MAP 0
#define WASIX_SHM_FILE_SYNC 1
#define WASIX_SHM_FILE_UNMAP 2
#define WASIX_SHM_FILE_MAP_AT 4
#define WASIX_SHM_FILE_RELEASE 5

// firebox#SAH (native-only, #5WQ mechanism): the HOST mprotect libcall. Applies
// a host `region::protect` to `[offset, offset+len)` of the guest linear memory
// so a guest access that violates `prot` FAULTS on the host — a real SIGSEGV
// wasmer's trap handler catches and maps to a guest SIGSEGV. This is the
// enforcement the no-MMU malloc backing below cannot provide on its own. Returns
// 0 on success, -1 on error / browser-absent (the capability is native-only;
// declared absent on browser via sysconf(_SC_MEMORY_PROTECTION) == -1). A -1
// means "not enforced" — the mapping is never FAILED for lack of enforcement.
// Defined in libc-bottom-half/sources/__wasixlibc_firebox.c.
extern int __wasilibc_mprotect_host(uintptr_t offset, uintptr_t len, int prot);

// firebox#SAH: does this mapping's protection require host-mprotect enforcement?
// A mapping missing PROT_WRITE (read-only or PROT_NONE) must fault a guest write
// (and PROT_NONE must additionally fault a guest read). The malloc-backed no-MMU
// emulation always yields RW pages, so such a mapping is instead backed by host
// pages that belong to it ALONE (64 KiB-isolated) and `region::protect`ed via
// __wasilibc_mprotect_host. An ordinary read-write mapping needs no enforcement
// and keeps the compact legacy backing. (A PROT_EXEC-only / PROT_READ|PROT_EXEC
// mapping also lacks PROT_WRITE, so a guest write to it faults too — faithful
// W^X; the host JIT executes guest code, so PROT_EXEC never affects guest reads.)
static int wasix_prot_needs_enforcement(int prot) {
    return (prot & PROT_WRITE) == 0;
}

// firebox#61X: the base of the cross-process shared window in the guest address
// space. A pointer at or above this was returned by mmap() routing a /dev/shm
// MAP_SHARED to the host window — it lives ABOVE the grow-capped heap, has NO
// malloc header, and the host owns its segment lifetime (the inode registry).
// munmap()/msync() must therefore treat it as a successful no-op (NOT recover a
// header / free() it). MUST MATCH wasmer shm_registry::SHM_BASE
// (= WASM32_MAX_BYTES - SHM_WINDOW_BYTES = 4 GiB - 256 MiB).
//
// firebox#V12 gap #3 (wasm64 width-generalization): this window handling is
// WIDTH-AGNOSTIC — NOT wasm32-only. The #F4A window core installs the window for
// a wasm64 shared memory too, and in the ≤4 GiB conformance posture
// (run-conformance.sh links BOTH widths --max-memory=4GiB) a wasm64 shared memory
// lands on the 4 GiB Static reservation and the host places the window at
// `wasm64_window_base(4 GiB) == SHM_BASE == 0xF0000000` — the SAME base as wasm32
// (wasmer shm_registry.rs `wasm64_window_base`). `memory.grow` is capped there
// (window_grow_ceiling), so no ordinary wasm64 mapping can sit at or above this
// base. The threshold therefore uniquely identifies window addresses on BOTH
// widths in that posture. The wasm32 encoding of this constant + every guard that
// reads it is byte-IDENTICAL (the value and code are unchanged; only the wasm64
// #if-out is removed), so re-enabling it for wasm64 cannot regress wasm32.
//
// FOLLOW-UP (out of scope, un-shipped): a wasm64 shared memory with a max > 4 GiB
// (the #F4A "case C" LLVM Dynamic band-admit) places the window at max − 256 MiB
// — a >4 GiB base — where a >4 GiB non-window heap address could exceed this
// constant. Closing that faithfully needs the guest to learn the real window base
// from the host (a host import), a separate task once #F4A case C is validated
// end-to-end; the conformance posture (this fix's scope) is unaffected.
#define WASIX_SHM_WINDOW_BASE ((uintptr_t)0xF0000000u)

// POSIX mmap(2) requires returned addresses to be aligned to
// sysconf(_SC_PAGESIZE). On WebAssembly the architectural page is 65536
// bytes but the POSIX page size every Linux-targeted port assumes is 4096
// — that is what every guest (Blink's x86-64 page-table packing, JIT
// engines that W^X-toggle code pages, busybox/perl/lua/R as latent
// witnesses) actually asserts on.
//
// Pre-fix history (firebox#458 Phase 1.5, 2026-05-23): mmap used
// `malloc(length + sizeof(struct map))` and returned `map + 1`. wasm32
// `malloc` is 16-byte aligned and the +sizeof(struct map) (24-byte) skip
// guaranteed the returned pointer was *never* page-aligned. Blink's
// `AllocateAnonymousPage` (`blink/memorymalloc.c:463`) asserts
// `!(real & ~PAGE_TA)` with PAGE_TA = 0x0000fffffffff000 and trapped on
// the first guest page-table allocation. Class lesson:
// class_lesson_wasix_libc_emulated_mmap_unaligned.
//
// Layout after the fix (guard-page-prefix header):
//
//     base                                     base + WASIX_MMAN_PAGE_SIZE
//     |                                         |
//     v                                         v
//     +-----------------------------------------+--------- ... ---------+
//     | struct map header  | unused padding ... | user memory (length)  |
//     +-----------------------------------------+--------- ... ---------+
//     ^                                         ^
//     | aligned_alloc(4096, ...) returns here   | returned to caller
//
// We allocate `aligned_alloc(WASIX_MMAN_PAGE_SIZE, length +
// WASIX_MMAN_PAGE_SIZE)`. The first page holds the header; the remaining
// bytes are the user-visible mapping. The returned pointer is `base +
// WASIX_MMAN_PAGE_SIZE`, which is page-aligned because base is.
// Cost: one wasted page (4 KiB) per mmap — accepted as the price of
// POSIX compliance for the emulated path.
//
// firebox#FWX: munmap()/msync() find a mapping's header through the private
// fragment table (see g_frags), never by reading below an arbitrary addr; the
// header is still `(struct map *)((char *)addr - WASIX_MMAN_PAGE_SIZE)` for the
// mapping's FIRST byte. free() takes the base pointer (= addr - page), which is
// exactly what aligned_alloc returned. aligned_alloc lives in
// libc-top-half/musl/src/malloc/mallocng/aligned_alloc.c — it does NOT
// require the length argument to be a multiple of the alignment, so we
// don't round up explicitly.

#define WASIX_MMAN_PAGE_SIZE ((size_t)4096)

// firebox#QAB (was #7C5): the POSIX *page size* — the granularity POSIX mmap(2)
// defines `off`/`addr` alignment against and that `sysconf(_SC_PAGE_SIZE)` /
// `getpagesize()` report. Firebox reports 4096, matching real Linux
// (`<__macro_PAGESIZE.h>`: `#define PAGESIZE 0x1000`). #7C5 historically pinned
// this to the 64 KiB WebAssembly linear-memory page; that was anti-faithful
// (Invariant 2) and rejected real programs that mmap() at a 4096-aligned but
// not-64 KiB-aligned address Linux accepts (e.g. a MAP_FIXED at 0x711000, which
// the 64 KiB alignment gate below turned into a spurious EINVAL). At the faithful
// 4096, WASIX_MMAN_SYS_PAGE_SIZE governs POSIX *semantics*:
//   - the offset-multiple EINVAL (mmap/11-1),
//   - the MAP_FIXED addr-alignment EINVAL (mmap/9-1),
//   - the partial-page zero-fill extent past EOF for ordinary RW mappings
//     (mmap/11-4/5/6).
// It is DISTINCT from two other page constants that must NOT track it:
//   - WASIX_MMAN_PAGE_SIZE (4096): the allocator-side alignment of an ordinary
//     mapping's header-prefix page. A malloc-backing implementation detail — it
//     numerically equals the POSIX page today, but is a separate concept
//     (header-prefix layout, not POSIX addr/offset semantics).
//   - WASIX_MMAN_HOST_PAGE_SIZE (64 KiB): the host-page isolation unit for the
//     #SAH region::protect enforcement (defined just below).
// Blink's wasm64 linear mapping hands us only 64 KiB-aligned MAP_FIXED addresses
// (map.c: ReserveVirtual rejects a non-64 KiB-aligned `virt`). 64 KiB is a
// multiple of 4096, so LOOSENING the addr-alignment gate from 64 KiB to 4096 is
// monotonic: every address that passed before still passes (the #796 path is not
// regressed), while genuinely-illegal 4096-unaligned fixed requests — which a
// Linux program is also denied — stay rejected.
#define WASIX_MMAN_SYS_PAGE_SIZE ((size_t)PAGESIZE)

// firebox#SAH/#QAB: the HOST-page-granularity isolation unit — the 64 KiB safe
// superset of any host OS page (4 KiB Linux / 16 KiB Apple Silicon). This is a
// HOST-side requirement, NOT a POSIX page size, so it stays 64 KiB regardless of
// the POSIX page (#QAB decoupled it from PAGESIZE, which is now 4096). A mapping
// that must ENFORCE a sub-RW protection is backed by host pages belonging to it
// ALONE: both its header prefix AND its rounded body use this unit so the later
// host `region::protect` (__wasilibc_mprotect_host) covers only whole host pages
// of THIS mapping and can never disturb a neighbouring allocation. Rounding the
// enforced body to 4096 instead would leave the body's tail sharing a host page
// with the next allocation — a region::protect(PROT_NONE) there would corrupt the
// neighbour. (Numerically equal to the WebAssembly linear-memory page, but a
// distinct concept: host-page isolation vs wasm memory.grow granularity.)
#define WASIX_MMAN_HOST_PAGE_SIZE ((size_t)0x10000)

// firebox#9VY-24: the Linux per-process mapping-count ceiling (vm.max_map_count,
// default 65530). POSIX mmap(2) mandates [ENOMEM] "if the process exceeds the
// maximum number of allowed memory mappings" — the SAME clause Open POSIX
// mmap/24-1 targets (it opens /proc/sys/vm/max_map_count and clamps it to 65530
// to bound its own runtime, then maps a /dev/shm object in a loop until ENOMEM).
//
// WHY Firebox needs an explicit counter (the substrate can't exhaust naturally
// here): a MAP_SHARED /dev/shm mapping routes to the host shared-memory window
// (the #61X/#F4A window — see the routing block in mmap() below), which is
// IDEMPOTENT PER INODE (invariant C1: the same object occupies the same guest
// offset in every process, so a fork-inherited child and an independent
// sem_open of the same name agree). Idempotency means mapping the SAME object N
// times returns the SAME window address every time and consumes NO additional
// resource — so the exhaustion loop never receives MAP_FAILED and spins forever
// (mmap/24-1 TIMEOUT on BOTH widths; on wasm64 the loop bound is 2^64). The
// window CANNOT be made to exhaust by handing out distinct addresses without
// breaking C1 coherence. The faithful resolution is the one Linux itself uses
// for this test: cap the number of live mappings. This is a real Linux limit
// Firebox otherwise lacks — width-independent, and it preserves every coherence
// invariant (the window still returns its canonical slot; we merely refuse to
// CREATE a new mapping past the ceiling). The blink MAP_FIXED linear-mapping
// path (handled/returned before this gate) is deliberately NOT counted — it is
// a Firebox-specific whole-address-space mode, not a portable VMA.
#define WASIX_MMAN_MAX_MAP_COUNT 65530

// Live mapping count. Accessed with __atomic builtins so the threaded build is
// race-free (in the non-threaded build these lower to plain loads/stores).
// Rides proc_fork's private-memory copy, so a child inherits the parent's live
// count — exactly as a forked process inherits the parent's VMAs on Linux.
static int g_map_count;

// True iff creating one more mapping would exceed vm.max_map_count. Checked
// once per mmap() (a pure gate — the increment happens only on a successful
// map), so a rare check/inc race can overshoot by at most the thread count,
// which is harmless for a 65530 ceiling.
static int wasix_map_count_would_exceed(void) {
    return __atomic_load_n(&g_map_count, __ATOMIC_SEQ_CST) >= WASIX_MMAN_MAX_MAP_COUNT;
}
static void wasix_map_count_inc(void) {
    __atomic_fetch_add(&g_map_count, 1, __ATOMIC_SEQ_CST);
}
static void wasix_map_count_dec(void) {
    __atomic_fetch_sub(&g_map_count, 1, __ATOMIC_SEQ_CST);
}

// firebox#R23: process memory-lock accounting for mlock/munlock/mlockall/
// munlockall. WASM linear memory is all-resident and unswappable, so a lock's
// RESIDENCY guarantee is satisfied trivially — the success path is a genuine
// no-op. What is real and must be faithful is the POSIX CAP_IPC_LOCK +
// RLIMIT_MEMLOCK gate (Linux performs the permission check INDEPENDENTLY of the
// residency it guarantees) and the msync(MS_INVALIDATE)->EBUSY-on-locked rule.
// Atomics keep the threaded build race-free (mirroring g_map_count above); the
// counters ride proc_fork's private-memory copy exactly like g_map_count (a
// child inherits the parent's locked state, as on Linux). geteuid()==0 ==
// holds CAP_IPC_LOCK (the sandbox has no fine-grained cap set; root ==
// privileged — the same model as the fs check_access root-bypass).
static int    g_mlock_mode;     // mlockall() flags in effect: 0 | MCL_CURRENT | MCL_FUTURE
static size_t g_locked_bytes;   // total currently-locked bytes (advisory accounting)

// True iff the caller holds CAP_IPC_LOCK. The sandbox models capabilities as
// "root has all, non-root has none" (identical to the shipped fs-enforcement
// root-bypass), and the conformance tests drop euid precisely to LOSE
// CAP_IPC_LOCK, so this is faithful to the common Linux case they target.
static int wasix_has_ipc_lock(void) { return geteuid() == 0; }

// The process RLIMIT_MEMLOCK soft limit (rlim_cur). Reads the firebox#KZ1
// stored rlimit table via the public getrlimit; RLIM_INFINITY if never set.
// getrlimit cannot fail for a valid resource (RLIMIT_MEMLOCK < RLIM_NLIMITS),
// but rl is pre-initialised to RLIM_INFINITY so an impossible failure fails
// open (never denies on garbage) rather than reading an uninitialised field.
static rlim_t wasix_memlock_limit(void) {
    struct rlimit rl = { RLIM_INFINITY, RLIM_INFINITY };
    (void)getrlimit(RLIMIT_MEMLOCK, &rl);
    return rl.rlim_cur;
}

static int    wasix_mlock_mode(void)   { return __atomic_load_n(&g_mlock_mode,   __ATOMIC_SEQ_CST); }
static size_t wasix_locked_bytes(void) { return __atomic_load_n(&g_locked_bytes, __ATOMIC_SEQ_CST); }

// [addr, addr+len) escapes linear memory => not "valid mapped pages" => ENOMEM.
// Faithful-enough on a no-MMU target: catches the LONG_MAX probe (mlock/8-1,
// munlock/10-1); an in-bounds hole is the documented no-MMU known-gap (no test
// hits it). len==0 is not a range and never ENOMEMs.
static int wasix_range_unmapped(const void *addr, size_t len) {
    uintptr_t a = (uintptr_t)addr, end;
    size_t mem = (size_t)__builtin_wasm_memory_size(0) * (size_t)65536;
    if (len == 0) return 0;
    if (__builtin_add_overflow(a, len, &end)) return 1;
    return a >= mem || end > mem;
}

// firebox#7KH: backing discriminator for a mapping's storage. An ANON mapping is
// an address-space RESERVATION (a Linux VMA), not a heap object — it is served by
// the reservation allocator below, OUT of the dlmalloc arena, so that multi-GiB
// PROT_NONE map/unmap churn never enters dlmalloc's treebins (the source of the
// regression/pthread_create-oom free-list corruption on BOTH widths — task 7KH
// reports). A file-backed mapping keeps the aligned_alloc + pread emulation.
#define WASIX_MMAN_BACKING_DLMALLOC 0
#define WASIX_MMAN_BACKING_RESERVE  1

// firebox#7KH: a struct-tail poison marking an already-unmapped header. Before
// #FWX it made a REPEATED munmap of a not-yet-reused range a DEFINED silent
// `return 0` (Linux munmap of an unmapped range succeeds) instead of a
// double-free / trap; since #FWX munmap never reads a header — an unmapped range
// simply has no fragment — and the poison is kept only as a marker. It lives
// at the END of struct map precisely so it
// survives a free-list node clobbering the payload start (reservation release) or
// dlfree linkage written into the payload (dlmalloc release). `dead_magic` is only
// ever 0 (live, set explicitly at map time) or WASIX_MMAN_DEAD (poisoned), so the
// guard never false-positives on a fresh mapping. NO ABORT / __builtin_trap /
// unreachable is introduced anywhere on the munmap path (the #W7T ghost mandate).
#define WASIX_MMAN_DEAD ((size_t)0xDEADD00Du)

struct map {
    int prot;
    int flags;
    off_t offset;
    size_t length;     // user-requested mapping length
    size_t body_len;   // allocated user-visible body = round_up(length, body page):
                       // the 4096 POSIX page for an ordinary mapping, the 64 KiB
                       // host page (WASIX_MMAN_HOST_PAGE_SIZE) for an enforced one
    int fd;
    // firebox#SAH: host-mprotect bookkeeping.
    //   prefix         distance from the user addr back to the aligned_alloc base
    //                  (== WASIX_MMAN_PAGE_SIZE for the compact 4 KiB-prefixed
    //                  legacy layout; == WASIX_MMAN_HOST_PAGE_SIZE / 64 KiB for a
    //                  host-protectable mapping whose body is 64 KiB-isolated so
    //                  region::protect never disturbs a neighbour). The header
    //                  always sits at addr - WASIX_MMAN_PAGE_SIZE either way, so
    //                  munmap()/msync() header recovery is uniform; free() takes
    //                  addr - prefix.
    //   host_protected 1 iff the body's host pages were mprotect'd to a sub-RW
    //                  protection and must be restored to RW before free() (so
    //                  malloc can safely reuse them — a still-PROT_NONE page
    //                  handed back to the allocator would fault a normal access).
    size_t prefix;
    int host_protected;
    // firebox#MMP: the mapped file's identity (st_dev, st_ino), recorded for a
    // file-backed mapping so a LATER mapping of the same object can be seeded from
    // this one's live bytes (wasix_seed_from_live_copy). On Linux every mapping of a
    // file is a view of one page cache; this emulation keeps a private copy per
    // mapping and writes a MAP_SHARED one back only at msync/munmap, so the live
    // bytes of a MAP_SHARED mapping are newer than the file's. `has_ino` is 0 for an
    // anonymous mapping and for a file whose fstat failed.
    int has_ino;
    dev_t dev;
    ino_t ino;
    // firebox#7KH: file-local storage discriminator + double-unmap poison. Both
    // are at the END of the struct (dead_magic last) so a reservation free-list
    // node or dlfree bin-linkage overwrite of the payload start cannot reach
    // dead_magic. Neither crosses the libc boundary — struct map is file-local to
    // mman.c (no ABI change, no exported symbol added).
    int backing;         // WASIX_MMAN_BACKING_{DLMALLOC,RESERVE}
    // firebox#FWX: fragment accounting, both guarded by g_vma_lock. `nfrags` is
    // how many fragment-table entries point at this header; `inflight` how many
    // pieces taken out of those fragments are still being released (or pinned by
    // msync) outside the lock. The backing is released exactly once, by whoever
    // drops the pair to (0, 0).
    size_t nfrags;
    size_t inflight;
    size_t dead_magic;   // 0 while live; WASIX_MMAN_DEAD once unmapped
};

// ── firebox#7KH: reservation allocator for ANON mappings ─────────────────────
// On Linux, mmap(MAP_ANON) is an address-space RESERVATION — a VMA, pure kernel
// bookkeeping, zero heap involvement, free overcommit. The pre-#7KH emulation
// instead routed every anonymous mapping through aligned_alloc (→ dlmalloc
// memalign), so t_vmfill's multi-GiB PROT_NONE map/unmap churn put GiB-scale
// chunks into dlmalloc's treebins right at the memory.grow ceiling and corrupted
// its free list (the [N]:0xffffffff OOB, both widths — task 7KH). This allocator
// restores the Linux separation STRUCTURALLY: anonymous mappings are served from
// linear memory obtained DIRECTLY from __builtin_wasm_memory_grow (never sbrk,
// never malloc), tracked in an address-ordered intrusive free list (nodes live in
// the freed extents themselves) that coalesces on insert. dlmalloc never sees the
// churn, so it exhausts cleanly (sbrk GROWFAIL → malloc NULL) and pthread_create
// returns EAGAIN. Carve granularity = WASIX_MMAN_PAGE_SIZE (4096), matching the
// ANON path's aligned_alloc(prefix, …) contract byte-for-byte in structure.
//
// Disjoint by construction: each memory.grow returns the base of a range that
// belongs exclusively to the caller. sbrk's #853 ownership model already treats
// our grows as foreign (goes discontiguous around them); symmetrically this
// allocator only ever owns ranges its OWN grows returned — no overlap possible,
// no coordination beyond what #853 shipped. Fork: head + nodes live in private
// linear memory, so a child inherits a coherent copy (same story as g_map_count).
struct wasix_res_node {
    struct wasix_res_node *next;   // next free extent, ascending address
    size_t len;                    // extent length, multiple of WASIX_MMAN_PAGE_SIZE
};
static struct wasix_res_node *g_res_free_head;
static volatile int g_res_lock;    // spinlock; alloc/free critical sections are short

static void wasix_res_lock_acquire(void) {
    while (__atomic_exchange_n(&g_res_lock, 1, __ATOMIC_ACQUIRE)) { /* spin */ }
}
static void wasix_res_lock_release(void) {
    __atomic_store_n(&g_res_lock, 0, __ATOMIC_RELEASE);
}

// Current linear-memory size in bytes (the reservation allocator's high-water:
// every address it hands out was returned by a memory.grow, so it is always
// < wasix_mem_end()).
static size_t wasix_res_mem_end(void) {
    return (size_t)__builtin_wasm_memory_size(0) * (size_t)65536;
}

// Grow linear memory by `pages`, returning the byte-base of the freshly grown
// extent, or (uintptr_t)-1 on failure. Two failure modes are folded into one
// clean NULL-equivalent:
//   1. memory.grow returns -1 (the ordinary ceiling-exhaustion signal).
//   2. firebox#7KH/#845: on wasm64 the memory.grow page-DELTA is truncated to 32
//      bits, so a request of >= 2^32 pages (which t_vmfill's mmap(SIZE_MAX/2)
//      binary-search issues on wasm64 — up to ~2^47 pages) does NOT return -1; it
//      returns the OLD size having grown by only `pages mod 2^32` (zero for the
//      power-of-two probe sizes), i.e. memory.grow LIES about success. Trusting
//      that lie would hand back a pointer into unbacked address space → OOB. We
//      therefore VERIFY memory actually reached old+pages and treat any shortfall
//      as failure — the huge request legitimately exceeds the ceiling, so ENOMEM
//      is the faithful answer, and wasm64 exhaustion then behaves exactly like
//      wasm32 (grow -1). (An allocator must never trust a grow it did not verify;
//      the underlying wasm64 memory.grow u32-truncation is a separate substrate
//      bug to fix in the runtime — this guard is correct defensive code either way.)
static uintptr_t wasix_res_grow(size_t pages) {
    if (pages == 0) return (uintptr_t)-1;
    size_t old = __builtin_wasm_memory_grow(0, pages);
    if (old == (size_t)-1) return (uintptr_t)-1;
    if (__builtin_wasm_memory_size(0) < old + pages) return (uintptr_t)-1;  // truncated/partial grow
    return (uintptr_t)old * (uintptr_t)65536;
}

// Insert [base, base+len) into the free list (address-ordered), coalescing with
// adjacent extents in BOTH directions. If the range OVERLAPS an existing free
// extent this is a repeated free (double-munmap of a reservation) — silently
// ignore it: Linux munmap of an already-unmapped range succeeds, and a trap here
// is forbidden (munmap/2-1, the #W7T ghost row). Caller holds g_res_lock.
static void wasix_res_free_insert(uintptr_t base, size_t len) {
    if (len == 0) return;
    struct wasix_res_node **pp = &g_res_free_head;
    struct wasix_res_node *prev = NULL, *n;
    // Walk to the first extent at or past base.
    while ((n = *pp) != NULL && (uintptr_t)n < base) {
        if ((uintptr_t)n + n->len > base) return;   // overlap with predecessor
        prev = n;
        pp = &n->next;
    }
    if (n != NULL && base + len > (uintptr_t)n) return;   // overlap with successor
    // Coalesce forward: [base,base+len) abuts the successor?
    if (n != NULL && base + len == (uintptr_t)n) {
        len += n->len;
        n = n->next;                 // absorb successor; new node inherits its link
    }
    // Coalesce backward: the predecessor abuts base?
    if (prev != NULL && (uintptr_t)prev + prev->len == base) {
        prev->len += len;
        prev->next = n;              // predecessor absorbs [base, …) (+ merged succ)
        return;
    }
    // Otherwise write a fresh node at base, linking predecessor → base → n.
    struct wasix_res_node *nn = (struct wasix_res_node *)base;
    nn->next = n;
    nn->len = len;
    *pp = nn;
}

// First-fit carve of an `align`-aligned block of exactly `want` bytes (want a
// multiple of WASIX_MMAN_PAGE_SIZE) from the free list. Head splinter [n, aligned)
// and tail splinter [aligned+want, n_end) are 4096-granular (align ≥ 4096 and
// every extent is 4096-granular, so a non-empty splinter is ≥ 4096 ≥ sizeof(node))
// and stay on the list. Returns NULL if no extent fits. Caller holds g_res_lock.
static void *wasix_res_carve(size_t align, size_t want) {
    struct wasix_res_node **pp = &g_res_free_head;
    struct wasix_res_node *n;
    while ((n = *pp) != NULL) {
        uintptr_t nbase = (uintptr_t)n;
        uintptr_t nend  = nbase + n->len;
        uintptr_t aligned = (nbase + (align - 1)) & ~(uintptr_t)(align - 1);
        if (aligned + want > aligned /* no wrap */ && aligned + want <= nend) {
            struct wasix_res_node *after = n->next;
            uintptr_t head_len = aligned - nbase;
            uintptr_t tail_start = aligned + want;
            uintptr_t tail_len = nend - tail_start;
            struct wasix_res_node *chain = after;
            if (tail_len > 0) {
                struct wasix_res_node *t = (struct wasix_res_node *)tail_start;
                t->len = (size_t)tail_len;
                t->next = after;
                chain = t;
            }
            if (head_len > 0) {
                n->len = (size_t)head_len;   // reuse n's slot in place (within head)
                n->next = chain;
                chain = n;
            }
            *pp = chain;
            return (void *)aligned;
        }
        pp = &n->next;
    }
    return NULL;
}

// Return an `align`-aligned pointer to round_up(size, 4096) bytes from the free
// list or directly from memory.grow — NEVER malloc/sbrk. NULL with no side effect
// visible to malloc on exhaustion. *needs_zero = 1 iff the extent was REUSED
// (fresh memory.grow pages are already zero by wasm semantics, so a fresh
// PROT_NONE reservation needs no memset — this preserves Linux overcommit for
// t_vmfill's multi-GiB fills). Caller must NOT hold g_res_lock.
static void *wasix_res_alloc(size_t align, size_t size, int *needs_zero) {
    size_t want;
    if (__builtin_add_overflow(size, (size_t)WASIX_MMAN_PAGE_SIZE - 1, &want))
        return NULL;
    want &= ~((size_t)WASIX_MMAN_PAGE_SIZE - 1);
    if (want == 0)
        want = WASIX_MMAN_PAGE_SIZE;   // size==0 never reached (caller gates length>0)

    wasix_res_lock_acquire();

    // (1) First-fit carve from the free list (reused memory — always stale).
    void *r = wasix_res_carve(align, want);
    if (r) {
        wasix_res_lock_release();
        *needs_zero = 1;
        return r;
    }

    // (2) Top-extension: if the HIGHEST free extent ends exactly at the current
    // memory end, grow only the shortfall and merge, so t_vmfill's binary search
    // reuses the top instead of burning the ceiling on dead fresh growth. A
    // foreign grow interleaved (sbrk/dlmalloc, the dynamic linker) breaks the
    // adjacency — detected via the grow-result base — so we insert the newly
    // grown range as a FRESH extent rather than assume contiguity (#853 model).
    struct wasix_res_node *hi = NULL;
    for (struct wasix_res_node *s = g_res_free_head; s != NULL; s = s->next)
        hi = s;                        // address-ordered list → tail = highest extent
    size_t mem_end = wasix_res_mem_end();
    if (hi != NULL && (uintptr_t)hi + hi->len == mem_end) {
        uintptr_t aligned = ((uintptr_t)hi + (align - 1)) & ~(uintptr_t)(align - 1);
        if (aligned + want > aligned) {           // no wrap
            uintptr_t need_end = aligned + want;
            if (need_end > mem_end) {
                size_t shortfall = (size_t)(need_end - mem_end);
                size_t pages = (shortfall + 65535) / 65536;
                uintptr_t got = wasix_res_grow(pages);
                if (got == (uintptr_t)-1) {
                    wasix_res_lock_release();
                    return NULL;                  // EXHAUSTION → mmap ENOMEM
                }
                size_t grown = pages * (size_t)65536;
                if (got == (uintptr_t)mem_end) {
                    hi->len += grown;             // contiguous: extend the top extent
                } else {
                    wasix_res_free_insert(got, grown);   // foreign grow: fresh extent
                }
                r = wasix_res_carve(align, want);
                if (r) {
                    wasix_res_lock_release();
                    *needs_zero = 1;              // carve spans reused memory → stale
                    return r;
                }
                // adjacency broke and carve still short → fall to fresh grow below
            }
        }
    }

    // (3) Fresh grow: a brand-new extent at the current memory end (zero pages).
    size_t pages = (want + 65535) / 65536;
    uintptr_t base = wasix_res_grow(pages);       // 64 KiB-aligned ≥ any align; verifies growth
    if (base == (uintptr_t)-1) {
        wasix_res_lock_release();
        return NULL;                              // EXHAUSTION → mmap ENOMEM
    }
    size_t grown = pages * (size_t)65536;
    if (grown > want)
        wasix_res_free_insert(base + want, grown - want); // return the unused tail
    wasix_res_lock_release();
    *needs_zero = 0;                              // fresh pages are zero by wasm rule
    return (void *)base;
}

// firebox#796: MAP_FIXED support for blink's wasm64 linear mapping.
//
// Blink built for wasm64 has CAN_64BIT=1 → HasLinearMapping() true → it places
// guest memory at host offset == guest VA (ToHost(va)=va+kSkew, kSkew==0) via
// mmap(addr, ..., MAP_FIXED). The malloc/aligned_alloc path below cannot honor a
// fixed address, so a fixed request is handled specially: grow the wasm linear
// memory to cover [addr, addr+len), fill it (file pread or zero), and return
// addr. This makes the guest's emulated address space coincide with linear-
// memory offsets — the whole point of the wasm64 port (collapses the software
// MMU). Blink's heap and the guest vaspace share this one linear memory; what
// keeps them apart is MAP_FIXED_NOREPLACE refusing any range mman cannot prove
// unowned (firebox#RZA, see mmap() below) — blink then treats the refusal as a
// collision. munmap of a fixed mapping cannot shrink wasm memory; an OWNED range
// goes back on the hole list, anything else is simply forgotten. A small
// registry records fixed ranges so munmap/msync distinguish them from the
// malloc-backed mappings.
//
// firebox#FWX: entries are page-granular RANGES, trimmed and split by munmap
// exactly as Linux trims and splits a VMA — munmap of any page-aligned range
// unmaps whatever part of each entry it covers and leaves the rest mapped. Before
// #FWX an entry was keyed by its start address alone: munmap of a sub-range that
// did not begin at a start fell through to the malloc-header path and failed
// EINVAL (jemalloc's chunk trim and blink's FreeVirtual under blink64 linear
// mode, #FWX), and munmap of a HEAD sub-range dropped the WHOLE entry and donated
// the still-live tail to the hole list, where the next NOREPLACE or anonymous
// reservation could hand it out a second time. Entries never overlap each other:
// a plain MAP_FIXED first unmaps whatever fixed range it replaces.
#define WASIX_MMAN_FIXED_MAX 4096
// `owned` (firebox#RZA): 1 iff mman PROVED the range unowned when it mapped it
// (a MAP_FIXED_NOREPLACE success, or a MAP_FIXED that landed wholly in fresh
// growth / a tracked hole). Only an owned range is handed back to the hole list
// on munmap — donating memory mman never owned would let the reservation
// allocator hand out someone else's live bytes. `len` is page-rounded.
// `wb_fd`/`wb_off` (firebox#MMP): for a writable MAP_SHARED file mapping, a dup of
// its fd and the file offset of `addr`, so its stores reach the file at msync and
// munmap — and when a later MAP_FIXED replaces it — as the private-copy path's
// do. -1 for every other fixed mapping. Before this a MAP_FIXED|MAP_SHARED file
// mapping's stores were silently dropped (a false success).
static struct {
    uintptr_t addr;
    size_t len;
    int owned;
    int wb_fd;
    off_t wb_off;
} g_fixed_maps[WASIX_MMAN_FIXED_MAX];
static int g_fixed_count;

// firebox#FWX: the lock over both mapping registries (g_fixed_maps and the
// private fragment table below). Lock order is g_vma_lock, then g_res_lock;
// nothing that holds g_res_lock takes g_vma_lock. No syscall runs under it.
static volatile int g_vma_lock;
static void wasix_vma_lock_acquire(void) {
    while (__atomic_exchange_n(&g_vma_lock, 1, __ATOMIC_ACQUIRE)) { /* spin */ }
}
static void wasix_vma_lock_release(void) {
    __atomic_store_n(&g_vma_lock, 0, __ATOMIC_RELEASE);
}

// ── firebox#FWX: the private-mapping fragment table ──────────────────────────
// A malloc-/reservation-backed mapping keeps its struct map header in-band, one
// page below its first byte, and before #FWX munmap/msync found it ONLY from
// there — so they could act on a mapping only at its exact start, and munmap
// refused every other range with EINVAL ("We don't support partial munmapping").
// Linux unmaps any page-aligned range: a sub-range, a range spanning several
// mappings, a range with unmapped holes in it. Finding the mapping that covers an
// arbitrary address needs an out-of-band index, so every live private mapping is
// recorded here as one or more FRAGMENTS — [start, end) page ranges that all
// point at the mapping's single header. munmap trims, removes or splits
// fragments; the backing itself is released when its last fragment is gone and
// no release of one of its pieces is still in flight (struct map nfrags/inflight).
// The table's storage comes from the reservation allocator (never malloc), and it
// grows by doubling. Unsorted: lookups are linear, as g_fixed_maps' always were.
struct wasix_frag {
    uintptr_t start;
    uintptr_t end;
    struct map *hdr;
};
static struct wasix_frag *g_frags;
static size_t g_frag_count;
static size_t g_frag_cap;

// firebox#V12 gap #3 (inode-lifetime pinning): a window-routed mmap of a
// /dev/shm object must keep that object's INODE alive as long as the mapping
// exists — exactly as a real mmap(MAP_SHARED, fd) does on Linux (the mapping
// holds a reference to the underlying file even after the fd is closed). The
// legacy aligned_alloc path already does this implicitly via dup(fd) (see the
// `new_fd = dup(fd)` below); the host-window path does NOT (its backing is a
// SEPARATE host shm object, decoupled from the /dev/shm VFS inode), so after
// sem_open closes its fd the /dev/shm inode has zero references and the firebox
// VFS recycles its st_ino on the next create. That recycling defeats musl
// sem_open's own (dev,ino)-keyed dedup: an unlink+recreate that reuses the inode
// makes musl treat the NEW sem as the OLD one and hand back the stale mapping
// (sem_unlink/6-1 reads the predecessor's value). Host-side slot invalidation
// (shm_unmap) cannot fix this — musl never consults the fresh host slot. The
// faithful fix is to pin the inode: dup(fd) on the first window map and hold it
// until the window mapping is unmapped, so a still-mapped sem keeps its inode
// reserved and a recreated name gets a DISTINCT inode (as on Linux). This small
// registry tracks the held fd per window address so munmap() can release it.
//
// firebox#V12 gap #3 (wasm64 width-generalization): the window (and therefore
// this registry) exists on wasm64 too under the #F4A window core — see
// WASIX_SHM_WINDOW_BASE above. `uintptr_t addr` holds the full 64-bit window
// address on wasm64 (no 32-bit truncation), so the registry is width-agnostic.
#define WASIX_MMAN_WINDOW_MAX 4096
// firebox#ZBK: the registry now also carries the backing fd's offset + mapped
// length + a `writeback` discriminator, so a regular-file MAP_SHARED window can be
// flushed to its VFS file at the msync/munmap sync points (guest-side write-back).
// A /dev/shm entry registers writeback=0 and keeps the pure inode-pin behaviour
// (#V12) — its window IS the shared object, there is nothing to flush.
static struct {
    uintptr_t addr;
    int fd;            // held (dup'd) backing fd — inode-pin AND writeback target
    off_t offset;      // fd offset the mapping starts at (0 in the routed posture)
    size_t length;     // user-requested mapping length (the writeback extent)
    int writeback;     // 1 => regular-file MAP_SHARED PROT_WRITE (flush on sync)
                       // 2 => firebox#GAG: the host page cache owns the bytes;
                       //      msync/munmap tell the host (shm_file), the guest
                       //      never copies them itself
    dev_t dev;         // firebox#MMP: the mapped object's identity, so a later
    ino_t ino;         // mapping of the same object seeds from the window's bytes
} g_window_maps[WASIX_MMAN_WINDOW_MAX];
static int g_window_count;

static void wasix_window_register(uintptr_t addr, int fd, off_t offset,
                                  size_t length, int writeback, dev_t dev, ino_t ino) {
    if (g_window_count < WASIX_MMAN_WINDOW_MAX) {
        g_window_maps[g_window_count].addr = addr;
        g_window_maps[g_window_count].fd = fd;
        g_window_maps[g_window_count].offset = offset;
        g_window_maps[g_window_count].length = length;
        g_window_maps[g_window_count].writeback = writeback;
        g_window_maps[g_window_count].dev = dev;
        g_window_maps[g_window_count].ino = ino;
        g_window_count++;
    } else {
        // Registry full — cannot track the held fd for later close, so don't
        // leak it. The inode-pin is best-effort; drop it rather than leak.
        close(fd);
    }
}

// firebox#MMP: the POSIX extent of a `length`-byte mapping — whole 4096 pages.
// POSIX mmap(2) maps whole pages ("any partial page specified by [pa, pa+len)"),
// so the bytes of the last page past `length` are part of the mapping: they show
// the FILE's bytes where the file has them (mmap/3-1 reads pa[len+1] and expects
// the file's byte there) and zero past the end of the file (mmap/11-4/5/6).
static size_t wasix_posix_extent(size_t length) {
    size_t r;
    if (__builtin_add_overflow(length, WASIX_MMAN_SYS_PAGE_SIZE - 1, &r))
        return length;
    return r & ~(WASIX_MMAN_SYS_PAGE_SIZE - 1);
}

// firebox#MMP: how many bytes of a mapping's `extent`, starting at file offset
// `offset`, a write-back may write: never past the file's CURRENT end. Linux
// writes a MAP_SHARED page back only up to the end of the file, so a store into
// the zero-filled tail past EOF is never persisted (mmap/11-4), while a store into
// the part of the last page that IS inside the file reaches it. `extent` itself is
// returned when the size cannot be read (a write-back has always been
// best-effort about the file it targets).
static size_t wasix_file_writeback_extent(int fd, off_t offset, size_t extent) {
    struct stat st;
    if (fd < 0 || fstat(fd, &st) != 0)
        return extent;
    if (st.st_size <= offset)
        return 0;
    const uint64_t in_file = (uint64_t)(st.st_size - offset);
    return in_file < (uint64_t)extent ? (size_t)in_file : extent;
}

// firebox#ZBK: flush a regular-file MAP_SHARED window mapping's live bytes back to
// its backing VFS file — the guest-side write-back that makes a regular file's
// MAP_SHARED faithful ("write references shall change the underlying object",
// mmap/7-1). Called at the SAME sync points the legacy private path uses
// (msync / munmap), so this is a strict reproduction of that path's file coherence
// plus the cross-fork window sharing. It is the INVERSE of the shm_created seed:
// read [0, min(length, cap)) out of the window and pwrite it to the fd. Only fires
// for entries registered writeback=1 (regular file, PROT_WRITE); a /dev/shm entry
// (writeback=0) stays the pure no-op the #61X msync/munmap path already is.
//
// CRITICAL — symmetric to the seed (see the routing block): the window lives ABOVE
// the grow-capped memory size (SHM_BASE), so only Cranelift-elided RAW loads reach
// it. Read the window with a volatile i32.load8 loop (never memcpy → memory.copy,
// which bounds-TRAPS past memory.size), bounce through a stack buffer BELOW SHM_BASE
// (bounds-OK), then pwrite from there. `cap` bounds the flush to the caller's
// requested extent (SIZE_MAX = whole mapping). Best-effort like the seed: a pwrite
// error leaves the file partially synced rather than failing the sync.
static void wasix_window_writeback(uintptr_t addr, size_t cap) {
    for (int i = 0; i < g_window_count; i++) {
        if (g_window_maps[i].addr == addr) {
            if (!g_window_maps[i].writeback)
                return;                          // /dev/shm entry: no file flush
            if (g_window_maps[i].writeback == 2) {
                // firebox#GAG: the host page cache flushes the whole file; a
                // guest pwrite here would race other mappers' stores.
                if (g_window_maps[i].fd >= 0)
                    (void)__wasilibc_shm_file(WASIX_SHM_FILE_SYNC, g_window_maps[i].fd, 0, 0);
                return;
            }
            const int fd = g_window_maps[i].fd;
            const off_t base_off = g_window_maps[i].offset;
            if (fd < 0)
                return;
            // firebox#MMP: the whole last page, up to the file's current end —
            // not just `length` (see wasix_file_writeback_extent).
            size_t total = wasix_file_writeback_extent(
                fd, base_off, wasix_posix_extent(g_window_maps[i].length));
            if (cap < total)
                total = cap;
            volatile unsigned char *win = (volatile unsigned char *)addr;
            unsigned char wbbuf[512];
            size_t done = 0;
            while (done < total) {
                size_t want = total - done;
                if (want > sizeof wbbuf) want = sizeof wbbuf;
                for (size_t k = 0; k < want; k++)
                    wbbuf[k] = win[done + k];    // raw i32.load8 <- window
                const ssize_t nw = pwrite(fd, wbbuf, want, base_off + (off_t)done);
                if (nw < 0) {
                    if (errno == EINTR) continue;
                    return;                       // best-effort
                }
                if (nw == 0) return;
                done += (size_t)nw;
            }
            return;
        }
    }
}

// Close + drop the held /dev/shm fd for a window mapping at `addr` (releasing the
// inode-lifetime pin). No-op if `addr` was not a window mapping we pinned.
// firebox#MMP: returns 1 and the released entry's mapped length in *length when an
// entry was released, else 0.
// `keep` (or NULL) is an object a mapping that REPLACES this one at `addr` already
// claimed (MAP_FIXED of the same object over its own mapping): its host claim is
// that new mapping's, so it is not ended here.
static int wasix_window_release(uintptr_t addr, size_t *length, const struct stat *keep) {
    for (int i = 0; i < g_window_count; i++) {
        if (g_window_maps[i].addr == addr) {
            const int fd = g_window_maps[i].fd;
            const int host_cached = g_window_maps[i].writeback == 2;
            const dev_t dev = g_window_maps[i].dev;
            const ino_t ino = g_window_maps[i].ino;
            if (length) *length = g_window_maps[i].length;
            g_window_maps[i] = g_window_maps[--g_window_count];
            // firebox#GAG: the last mapping of a host-cached file in this
            // process ends this process's claim on the host page cache (which
            // flushes). Told while the held fd is still open: it names the file.
            if (host_cached && fd >= 0) {
                int still_mapped = 0;
                for (int j = 0; j < g_window_count; j++)
                    if (g_window_maps[j].writeback == 2 && g_window_maps[j].dev == dev &&
                        g_window_maps[j].ino == ino)
                        still_mapped = 1;
                if (keep && keep->st_dev == dev && keep->st_ino == ino)
                    still_mapped = 1;
                if (!still_mapped)
                    (void)__wasilibc_shm_file(WASIX_SHM_FILE_UNMAP, fd, 0, 0);
            }
            if (fd >= 0) close(fd);
            return 1;
        }
    }
    return 0;
}

// firebox#MMP: does any registered window mapping still start at `addr`? Every
// mapping of one window slot shares its address, so the host pages there may be
// released only when the last of them goes.
static int wasix_window_addr_live(uintptr_t addr) {
    for (int i = 0; i < g_window_count; i++)
        if (g_window_maps[i].addr == addr) return 1;
    return 0;
}

// firebox#6Q8: widen the host protection of every WASIX_MMAN_HOST_PAGE_SIZE unit
// that [start, start+len) touches back to read-write. Rounding OUTWARD is the only
// safe direction for a widen: it can lose a fault in a partially-covered unit, but
// it can never forge one. An enforced (#SAH) mapping owns its 64 KiB units alone,
// so the units rounded over belong either to the range's own mapping or to memory
// that was read-write already. A -1 from the host (browser: no page protection at
// all, declared by sysconf(_SC_MEMORY_PROTECTION) == -1) means nothing was ever
// enforced there, so it is ignored.
static void wasix_host_widen_rw(uintptr_t start, size_t len) {
    const uintptr_t unit = (uintptr_t)WASIX_MMAN_HOST_PAGE_SIZE;
    uintptr_t end;
    if (len == 0 || __builtin_add_overflow(start, (uintptr_t)len, &end) ||
        __builtin_add_overflow(end, unit - 1, &end))
        return;
    const uintptr_t lo = start & ~(unit - 1);
    const uintptr_t hi = end & ~(unit - 1);
    (void)__wasilibc_mprotect_host(lo, hi - lo, PROT_READ | PROT_WRITE);
}

// firebox#RZA: does any registered fixed mapping intersect [a, b)?
static int wasix_fixed_overlaps(uintptr_t a, uintptr_t b) {
    for (int i = 0; i < g_fixed_count; i++) {
        uintptr_t fa = g_fixed_maps[i].addr;
        uintptr_t fb = fa + g_fixed_maps[i].len;
        if (fa < b && a < fb) return 1;
    }
    return 0;
}

// firebox#RZA: remove [a, b) from the hole list iff it lies WHOLLY inside ONE
// free extent — the only case in which mman can prove nobody owns it. Returns 1
// on success (the range now belongs to the caller), 0 otherwise (list
// untouched). a and b are WASIX_MMAN_PAGE_SIZE-aligned, as is every extent, so a
// non-empty splinter is >= 4096 >= sizeof(node). Caller holds g_res_lock.
static int wasix_res_take_range(uintptr_t a, uintptr_t b) {
    struct wasix_res_node **pp = &g_res_free_head;
    struct wasix_res_node *n;
    while ((n = *pp) != NULL) {
        uintptr_t nbase = (uintptr_t)n;
        uintptr_t nend = nbase + n->len;
        if (nbase > a) return 0;              // address-ordered: nothing further can contain a
        if (a < nend) {
            if (b > nend) return 0;           // runs past this extent into owned memory
            struct wasix_res_node *chain = n->next;
            if (nend > b) {
                struct wasix_res_node *t = (struct wasix_res_node *)b;
                t->len = (size_t)(nend - b);
                t->next = chain;
                chain = t;
            }
            if (a > nbase) {
                n->len = (size_t)(a - nbase);
                n->next = chain;
                chain = n;
            }
            *pp = chain;
            return 1;
        }
        pp = &n->next;
    }
    return 0;
}

// firebox#RZA: remove EVERY part of [a, b) from the hole list, whatever else it
// overlaps. A plain MAP_FIXED overwrites what is there (Linux semantics), and the
// bytes it now occupies must never be carved out again by the reservation
// allocator. Caller holds g_res_lock.
static void wasix_res_take_overlap(uintptr_t a, uintptr_t b) {
    struct wasix_res_node **pp = &g_res_free_head;
    struct wasix_res_node *n;
    while ((n = *pp) != NULL) {
        uintptr_t nbase = (uintptr_t)n;
        uintptr_t nend = nbase + n->len;
        if (nbase >= b) return;
        if (nend <= a) { pp = &n->next; continue; }
        struct wasix_res_node *chain = n->next;
        struct wasix_res_node **next_pp = pp;
        if (nend > b) {
            struct wasix_res_node *t = (struct wasix_res_node *)b;
            t->len = (size_t)(nend - b);
            t->next = chain;
            chain = t;
        }
        if (a > nbase) {
            n->len = (size_t)(a - nbase);
            n->next = chain;
            chain = n;
            next_pp = &n->next;
        }
        *pp = chain;
        pp = next_pp;
    }
}

// firebox#FWX: does a fixed mapping cover `addr`? Caller holds g_vma_lock.
static int wasix_fixed_contains(uintptr_t addr) {
    for (int i = 0; i < g_fixed_count; i++) {
        uintptr_t fa = g_fixed_maps[i].addr;
        if (fa <= addr && addr - fa < g_fixed_maps[i].len) return 1;
    }
    return 0;
}

// firebox#MMP: write [s, e) of fixed mapping `i` back to its file, if it is a
// written-back one (see `wb_fd`): from `wb_off + (s - addr)`, never past the
// file's current end (wasix_file_writeback_extent, as the private-copy path).
// Best-effort like that path's munmap; returns -1 if a pwrite failed (msync
// reports it). Caller holds g_vma_lock. A fixed mapping lives below
// memory.size, so its bytes are written straight from linear memory.
static int wasix_fixed_writeback_locked(int i, uintptr_t s, uintptr_t e) {
    const int fd = g_fixed_maps[i].wb_fd;
    if (fd < 0) return 0;
    const uintptr_t fa = g_fixed_maps[i].addr;
    const off_t base = g_fixed_maps[i].wb_off;
    const size_t limit = wasix_file_writeback_extent(fd, base, g_fixed_maps[i].len);
    if (e > fa + limit) e = fa + limit;
    const char *body = (const char *)s;
    size_t len = s < e ? (size_t)(e - s) : 0;
    off_t off = base + (off_t)(s - fa);
    while (len > 0) {
        const ssize_t n = pwrite(fd, body, len, off);
        if (n > 0) {
            len -= (size_t)n;
            off += n;
            body += (size_t)n;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            return -1;
        }
    }
    return 0;
}

// firebox#FWX: unmap the page range [a, b) from the fixed registry, as Linux
// unmaps it from its VMAs: an entry wholly inside is removed, one that overlaps
// an edge is trimmed, and one that strictly contains the range is split in two.
// At most one entry can strictly contain the range (entries never overlap), so at
// most one new slot is needed; if the registry is full that is ENOMEM — the Linux
// answer when a split would exceed the mapping limit — and nothing is changed.
// Returns 0, or -1 for that ENOMEM. Caller holds g_vma_lock (not g_res_lock).
static int wasix_fixed_unmap_locked(uintptr_t a, uintptr_t b) {
    // firebox#MMP: the split tail of a written-back mapping needs its own fd; dup
    // it before anything changes, so a failure leaves the registry as it was.
    int tail_fd = -1;
    for (int i = 0; i < g_fixed_count; i++) {
        uintptr_t fa = g_fixed_maps[i].addr;
        uintptr_t fb = fa + g_fixed_maps[i].len;
        if (fa < a && b < fb) {
            if (g_fixed_count >= WASIX_MMAN_FIXED_MAX) return -1;
            if (g_fixed_maps[i].wb_fd >= 0 && (tail_fd = dup(g_fixed_maps[i].wb_fd)) < 0)
                return -1;
        }
    }
    for (int i = 0; i < g_fixed_count;) {
        uintptr_t fa = g_fixed_maps[i].addr;
        uintptr_t fb = fa + g_fixed_maps[i].len;
        if (!(fa < b && a < fb)) { i++; continue; }
        uintptr_t s = fa > a ? fa : a;
        uintptr_t e = fb < b ? fb : b;
        int owned = g_fixed_maps[i].owned;
        const int wb_fd = g_fixed_maps[i].wb_fd;
        const off_t wb_off = g_fixed_maps[i].wb_off;
        // firebox#MMP: the piece's stores reach the file before it goes away.
        wasix_fixed_writeback_locked(i, s, e);
        if (s == fa && e == fb) {
            if (wb_fd >= 0) (void)close(wb_fd);
            g_fixed_maps[i] = g_fixed_maps[--g_fixed_count];   // re-examine slot i
        } else {
            if (s == fa) {                                     // head unmapped
                g_fixed_maps[i].addr = e;
                g_fixed_maps[i].len = (size_t)(fb - e);
                if (wb_fd >= 0) g_fixed_maps[i].wb_off = wb_off + (off_t)(e - fa);
            } else {                                           // tail (and maybe middle)
                g_fixed_maps[i].len = (size_t)(s - fa);
                if (e != fb) {                                 // middle: split off the tail
                    g_fixed_maps[g_fixed_count].addr = e;
                    g_fixed_maps[g_fixed_count].len = (size_t)(fb - e);
                    g_fixed_maps[g_fixed_count].owned = owned;
                    g_fixed_maps[g_fixed_count].wb_fd = wb_fd >= 0 ? tail_fd : -1;
                    g_fixed_maps[g_fixed_count].wb_off = wb_off + (off_t)(e - fa);
                    g_fixed_count++;
                }
            }
            i++;
        }
        // firebox#6Q8: a fixed mapping may have host-protected whole units of
        // itself (its own sub-RW prot). The memory is not released (wasm memory
        // cannot shrink), so it stays addressable to whatever mapping contains it
        // or reuses it next — lift the protection over the unmapped piece so a
        // later legal access does not fault on a dead mapping's prot.
        wasix_host_widen_rw(s, (size_t)(e - s));
        // firebox#RZA: an OWNED piece goes back on the hole list, so a later
        // MAP_FIXED_NOREPLACE (or anon reservation) may reuse it — but only if no
        // other live fixed mapping still covers any of it.
        if (owned) {
            wasix_res_lock_acquire();
            if (!wasix_fixed_overlaps(s, e))
                wasix_res_free_insert(s, (size_t)(e - s));
            wasix_res_lock_release();
        }
    }
    return 0;
}

static int wasix_fixed_unmap_shared_files(uintptr_t a, uintptr_t b);

// firebox#MMP: a MAP_FIXED that fails after registering its range undoes it with
// munmap — which would write the range back to the NEW mapping's file before its
// bytes were ever read from it. Drop the write-back first.
static void wasix_fixed_disarm_writeback(uintptr_t addr) {
    wasix_vma_lock_acquire();
    for (int i = 0; i < g_fixed_count; i++) {
        if (g_fixed_maps[i].addr == addr && g_fixed_maps[i].wb_fd >= 0) {
            (void)close(g_fixed_maps[i].wb_fd);
            g_fixed_maps[i].wb_fd = -1;
        }
    }
    wasix_vma_lock_release();
}

static void *wasix_mmap_fixed(void *addr, size_t length, int prot, int flags,
                              int fd, off_t offset) {
    uintptr_t target = (uintptr_t)addr;
    size_t need;
    if (__builtin_add_overflow(target, length, &need)) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    // firebox#RZA: decide OWNERSHIP before touching a byte. The page-rounded span
    // [target, span_end) is what the mapping occupies.
    uintptr_t span_end;
    if (__builtin_add_overflow((uintptr_t)need, (uintptr_t)WASIX_MMAN_PAGE_SIZE - 1,
                               &span_end)) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    span_end &= ~(uintptr_t)(WASIX_MMAN_PAGE_SIZE - 1);
    int noreplace = 0;
#ifdef MAP_FIXED_NOREPLACE
    noreplace = (flags & MAP_FIXED_NOREPLACE) != 0;   // wins over MAP_FIXED, as on Linux
#endif
    // firebox#MMP: a writable MAP_SHARED file mapping keeps a dup of its fd so its
    // stores are written back (see g_fixed_maps' `wb_fd`). dup()'s errno (EMFILE)
    // is kept, as on the private-copy path.
    int wb_fd = -1;
    if ((flags & MAP_SHARED) && !(flags & MAP_ANON) && (prot & PROT_WRITE) && fd >= 0 &&
        (wb_fd = dup(fd)) < 0)
        return MAP_FAILED;
    int owned;
    wasix_vma_lock_acquire();
    // A live fixed mapping is occupied by definition.
    if (noreplace && wasix_fixed_overlaps(target, span_end)) {
        wasix_vma_lock_release();
        if (wb_fd >= 0) (void)close(wb_fd);
        errno = EEXIST;
        return MAP_FAILED;
    }
    // firebox#MMP: a written-back mapping must be registered, or its stores are
    // lost; refuse it while the registry is (nearly — a split may need a slot)
    // full, before anything changes. ENOMEM: the mapping limit.
    if (wb_fd >= 0 && g_fixed_count >= WASIX_MMAN_FIXED_MAX - 1) {
        wasix_vma_lock_release();
        (void)close(wb_fd);
        errno = ENOMEM;
        return MAP_FAILED;
    }
    // firebox#FWX: a plain MAP_FIXED REPLACES the fixed mappings under it — it
    // unmaps them first, as Linux does, so registry entries never overlap and
    // the replaced range can never be unmapped twice. An owned piece goes back on
    // the hole list here, so a MAP_FIXED over mman's own earlier fixed mapping is
    // proved unowned by the take below and stays owned.
    if (!noreplace && wasix_fixed_unmap_locked(target, span_end) != 0) {
        wasix_vma_lock_release();
        if (wb_fd >= 0) (void)close(wb_fd);
        errno = ENOMEM;
        return MAP_FAILED;
    }
    wasix_res_lock_acquire();
    // Grow the single wasm linear memory to cover the span. The grown extent is
    // mman's own until something takes it, so it goes on the hole list; a grow
    // interleaved by someone else (sbrk, the dynamic linker) returns a base above
    // the size we read, and the gap between is THEIRS — never assumed ours.
    size_t have = wasix_res_mem_end();
    if ((size_t)span_end > have) {
        size_t grow_pages = ((size_t)span_end - have + 65535) / 65536;
        uintptr_t got = wasix_res_grow(grow_pages);
        if (got == (uintptr_t)-1) {
            wasix_res_lock_release();
            wasix_vma_lock_release();
            if (wb_fd >= 0) (void)close(wb_fd);
            errno = ENOMEM;
            return MAP_FAILED;
        }
        wasix_res_free_insert(got, grow_pages * (size_t)65536);
    }
    // PROOF OF UNOWNERSHIP: the span lies wholly inside ONE hole mman tracks —
    // fresh growth, a munmap'd owned fixed range, or a reservation it got back.
    owned = wasix_res_take_range(target, span_end);
    if (!owned) {
        if (noreplace) {
            // Owned or unknown is OCCUPIED: a spurious EEXIST is honest and
            // recoverable (the caller picks another address or falls back); a
            // false success silently overwrites live memory (invariant 0).
            wasix_res_lock_release();
            wasix_vma_lock_release();
            if (wb_fd >= 0) (void)close(wb_fd);
            errno = EEXIST;
            return MAP_FAILED;
        }
        // Plain MAP_FIXED overwrites (Linux semantics). Whatever part of the span
        // was a hole is taken off the list so it is never handed out twice.
        wasix_res_take_overlap(target, span_end);
    }
    wasix_res_lock_release();
    // Register the page-rounded range while still under g_vma_lock, so no other
    // thread can NOREPLACE into it between the take above and the fill below.
    // (A full registry leaves the mapping unregistered, as it always has: munmap
    // then finds nothing to trim and it is never donated.)
    int registered = 0;
    if (g_fixed_count < WASIX_MMAN_FIXED_MAX) {
        g_fixed_maps[g_fixed_count].addr = target;
        g_fixed_maps[g_fixed_count].len = (size_t)(span_end - target);
        g_fixed_maps[g_fixed_count].owned = owned;
        g_fixed_maps[g_fixed_count].wb_fd = wb_fd;
        g_fixed_maps[g_fixed_count].wb_off = offset;
        g_fixed_count++;
        registered = 1;
    } else if (wb_fd >= 0) {
        (void)close(wb_fd);   // unreachable: refused above while the registry was full
    }
    wasix_vma_lock_release();
    // firebox#MMP: MAP_FIXED UNMAPS what it replaces (POSIX: "any previous mapping
    // ... for those whole pages ... shall be removed"); see
    // wasix_fixed_unmap_shared_files. Only once the range is REGISTERED as fixed:
    // releasing the last piece of a backing finalizes it, and finalize keeps (never
    // frees) an allocation a live fixed mapping covers. An unregistered range (a
    // full registry) keeps the old behaviour rather than risk handing the new
    // mapping's bytes to malloc.
    if (!noreplace && registered && wasix_fixed_unmap_shared_files(target, span_end) != 0) {
        wasix_fixed_disarm_writeback(target);
        (void)munmap(addr, length);
        errno = ENOMEM;
        return MAP_FAILED;
    }
    // firebox#6Q8: MAP_FIXED REPLACES whatever was mapped at [addr, addr+length)
    // — on Linux the new mapping carries its OWN protection and the old mapping's
    // protection does not constrain it. ld-musl loads every shared library that
    // way: it reserves the whole span file-backed with the first segment's prot
    // (PROT_READ), then MAP_FIXEDs each later segment (R-X text, RW data, anon
    // bss) inside it. Since #SAH that PROT_READ reservation is really read-only on
    // the host, and the fill below is a HOST write (pread → EIO, memset → trap)
    // into pages the OLD mapping protected. So first retire the old mapping's
    // protection over the range (the "replace" half of MAP_FIXED); the new
    // mapping's own protection is applied after the fill (below).
    // firebox#MMP: the fill covers the whole POSIX extent — the last page past
    // `length` shows the file's bytes (zero past its end), as Linux maps whole
    // pages (mmap/3-1 reads pa2[size2+1] of its MAP_FIXED replacement and expects
    // the NEW file's byte there, not the replaced mapping's).
    const size_t extent = wasix_posix_extent(length);
    wasix_host_widen_rw(target, extent);
    if ((flags & MAP_ANON) == 0) {
        char *body = (char *)addr;
        size_t rem = extent;
        off_t off = offset;
        while (rem > 0) {
            const ssize_t n = pread(fd, body, rem, off);
            if (n < 0) {
                if (errno == EINTR) continue;
                // firebox#FWX: undo the registration (an owned range goes back
                // on the hole list) and keep pread's errno.
                const int e = errno;
                wasix_fixed_disarm_writeback(target);
                (void)munmap(addr, length);
                errno = e;
                return MAP_FAILED;
            }
            if (n == 0) {           // short file — zero-fill the remainder
                memset(body, 0, rem);
                break;
            }
            rem -= (size_t)n;
            off += n;
            body += (size_t)n;
        }
    } else {
        memset(addr, 0, extent);
    }
    // firebox#6Q8: the new mapping's own protection — the other half of MAP_FIXED
    // replace. Before #6Q8 a fixed mapping ignored `prot` entirely, so a PROT_READ
    // MAP_FIXED segment accepted writes. The host protects whole 64 KiB units while
    // the POSIX page is 4 KiB, so only the units lying WHOLLY inside the range get
    // the requested protection; a partial unit at either edge is shared with a
    // neighbour and stays read-write (widened above) — over-granting loses a fault,
    // narrowing would forge one. This is the same routing the blink64 mprotect
    // bridge (#86C) applies to a guest mprotect. PROT_EXEC is dropped: wasm never
    // executes linear memory, so it is meaningless to the host pages.
    if (wasix_prot_needs_enforcement(prot)) {
        const uintptr_t unit = (uintptr_t)WASIX_MMAN_HOST_PAGE_SIZE;
        const uintptr_t lo = (target + unit - 1) & ~(unit - 1);
        const uintptr_t hi = (uintptr_t)need & ~(unit - 1);
        if (lo >= target && lo < hi)
            (void)__wasilibc_mprotect_host(lo, hi - lo,
                                           prot & (PROT_READ | PROT_WRITE));
    }
    return addr;
}

// firebox#TTB: validate the POSIX mmap(2) preconditions for a file-backed
// mapping and return the MANDATED errno (EBADF / EACCES / EOVERFLOW) before
// any allocation, matching musl/Linux. The faithful source of "is this fd
// valid / readable / writable" on WASI is the descriptor's capability rights
// (`fs_rights_base`), exactly as fcntl(F_GETFL) derives the access mode — an
// O_RDONLY fd carries __WASI_RIGHTS_FD_READ and not FD_WRITE; O_WRONLY the
// reverse (see libc-bottom-half/cloudlibc/.../fcntl/openat.c rights map).
//
// On success returns 0; on a precondition failure sets errno and returns -1.
// ANON mappings have no fd and skip every fd-derived check (caller gates on
// MAP_ANON before calling).
//
//   EBADF     : fildes is not a valid open file descriptor (mmap/19-1).
//               __wasi_fd_fdstat_get returns __WASI_ERRNO_BADF for a closed or
//               negative fd; EBADF == __WASI_ERRNO_BADF in wasix-libc so the
//               WASI errno is the POSIX errno with no translation.
//   EACCES    : the mapping's access does not match the fd's open mode
//               (mmap/6-4, mmap/6-6). POSIX: the fd "shall have been opened
//               with read permission, regardless of the protection options";
//               and "if PROT_WRITE is specified, ... opened ... with write
//               permission unless MAP_PRIVATE". So: missing FD_READ => EACCES
//               (6-6, O_WRONLY); PROT_WRITE && MAP_SHARED && missing FD_WRITE
//               => EACCES (6-4, O_RDONLY shared write). PROT_WRITE+MAP_PRIVATE
//               on a read-only fd is copy-on-write and must SUCCEED (mmap/6-5)
//               — it is deliberately NOT an EACCES path.
//   EOVERFLOW : the file is a regular file and off + len overflows the offset
//               maximum (mmap/31-1). Faithful to the Linux do_mmap() check
//               `(pgoff + (len >> PAGE_SHIFT)) < pgoff` in unsigned-long (here
//               size_t) page-count arithmetic: a wrap means off+len cannot be
//               represented and POSIX mandates EOVERFLOW. Width-correct for
//               both wasm32 (size_t=32-bit, where the test triggers it) and
//               wasm64 (size_t=64-bit, where it is unreachable — as on 64-bit
//               Linux, where mmap/31-1 self-reports UNSUPPORTED).
static int wasix_mmap_check_file_preconditions(int prot, int flags, int fd,
                                               off_t offset, size_t length) {
    __wasi_fdstat_t fds;
    __wasi_errno_t error = __wasi_fd_fdstat_get((__wasi_fd_t)fd, &fds);
    if (error != 0) {
        // Bad/closed fd surfaces as __WASI_ERRNO_BADF, which is EBADF.
        errno = __wasilibc_errno_from_wasi((int)error);
        return -1;
    }

    // firebox#9VY-23: POSIX mmap(2) [ENODEV] — "The fildes argument refers to a
    // file whose type is not supported by mmap()" (mmap/23-1). Only a regular
    // file (the malloc-backed pread emulation / the /dev/shm window) and a
    // character device are mmap-able; a pipe/FIFO, socket, directory, or block
    // device is not. This MUST precede the FD_READ (EACCES) and the alloc/pread
    // path: the emulation below unconditionally `pread`s a non-ANON fd to seed
    // the mapping, and a pread on a pipe with no writer BLOCKS FOREVER — that is
    // exactly mmap/23-1's 142 s TIMEOUT (map the read end of an empty pipe). On
    // WASI a pipe reports fs_filetype == __WASI_FILETYPE_UNKNOWN (0); a regular
    // file and a /dev/shm object both report __WASI_FILETYPE_REGULAR_FILE (4), so
    // the window-routed and legacy file paths are unaffected. Faithful to Linux,
    // where mmap of a pipe/socket returns ENODEV rather than reading it.
    if (fds.fs_filetype != __WASI_FILETYPE_REGULAR_FILE &&
        fds.fs_filetype != __WASI_FILETYPE_CHARACTER_DEVICE) {
        errno = ENODEV;
        return -1;
    }

    // POSIX: the fd must have been opened for reading regardless of prot.
    if ((fds.fs_rights_base & __WASI_RIGHTS_FD_READ) == 0) {
        errno = EACCES;
        return -1;
    }

    // POSIX: a writable SHARED mapping requires the fd to be writable. A
    // MAP_PRIVATE mapping is copy-on-write and does NOT (mmap/6-5).
    if ((prot & PROT_WRITE) != 0 && (flags & MAP_PRIVATE) == 0 &&
        (fds.fs_rights_base & __WASI_RIGHTS_FD_WRITE) == 0) {
        errno = EACCES;
        return -1;
    }

    // POSIX EOVERFLOW: only for regular files, when off + len exceeds the
    // file's offset maximum. Mirror the Linux kernel page-count wrap check.
    if (fds.fs_filetype == __WASI_FILETYPE_REGULAR_FILE) {
        // offset is page-aligned by the time mmap reaches a file read; the
        // emulated path requires no alignment, but the overflow test is on the
        // page-count sum exactly as the kernel computes it.
        size_t pgoff = (size_t)((uint64_t)offset >> 12);
        size_t len_pages = length >> 12;
        if (pgoff + len_pages < pgoff) {
            errno = EOVERFLOW;
            return -1;
        }
    }

    return 0;
}

// ── firebox#FWX: private-fragment helpers (see the table note above) ─────────

// The mapping's first byte: its header sits one WASIX_MMAN_PAGE_SIZE below it.
static uintptr_t wasix_map_addr0(const struct map *m) {
    return (uintptr_t)m + WASIX_MMAN_PAGE_SIZE;
}

// The POSIX extent of a mapping: its length rounded up to the 4096 page. An
// enforced mapping's body runs on to the 64 KiB host page (body_len); that slack
// is never a fragment — it belongs to the backing and goes with it.
static size_t wasix_map_extent(const struct map *m) {
    return (m->length + (WASIX_MMAN_SYS_PAGE_SIZE - 1)) & ~(WASIX_MMAN_SYS_PAGE_SIZE - 1);
}

// Make room for `extra` more fragments. Caller holds g_vma_lock; takes g_res_lock.
// Returns 0, or -1 when the reservation allocator is exhausted.
static int wasix_frags_reserve(size_t extra) {
    if (g_frag_count + extra <= g_frag_cap) return 0;
    size_t ncap = g_frag_cap ? g_frag_cap : 256;
    while (ncap < g_frag_count + extra) {
        if (__builtin_mul_overflow(ncap, (size_t)2, &ncap)) return -1;
    }
    size_t bytes;
    if (__builtin_mul_overflow(ncap, sizeof(struct wasix_frag), &bytes)) return -1;
    int needs_zero;
    struct wasix_frag *n = wasix_res_alloc(WASIX_MMAN_PAGE_SIZE, bytes, &needs_zero);
    if (n == NULL) return -1;
    if (g_frag_count) memcpy(n, g_frags, g_frag_count * sizeof(struct wasix_frag));
    if (g_frags != NULL) {
        size_t old = (g_frag_cap * sizeof(struct wasix_frag) + WASIX_MMAN_PAGE_SIZE - 1) &
                     ~(size_t)(WASIX_MMAN_PAGE_SIZE - 1);
        wasix_res_lock_acquire();
        wasix_res_free_insert((uintptr_t)g_frags, old);
        wasix_res_lock_release();
    }
    g_frags = n;
    g_frag_cap = ncap;
    return 0;
}

// Record a new mapping as one fragment. Caller holds g_vma_lock and has reserved.
static void wasix_frag_add_locked(uintptr_t start, uintptr_t end, struct map *m) {
    g_frags[g_frag_count].start = start;
    g_frags[g_frag_count].end = end;
    g_frags[g_frag_count].hdr = m;
    g_frag_count++;
    m->nfrags++;
    wasix_map_count_inc();   // firebox#9VY-24: every fragment is a VMA
}

// A piece taken out of a fragment, released outside the lock.
struct wasix_piece {
    uintptr_t start;
    uintptr_t end;
    struct map *hdr;
};

// firebox#MMP: a writable MAP_SHARED file mapping — the one kind of private copy
// whose bytes are written back to a file (wasix_map_writeback) and read by a later
// mapping of that file (wasix_seed_from_live_copy). A MAP_FIXED over part of one
// must unmap that part first (wasix_mmap_fixed), or the new mapping's bytes
// would later be written into the OLD file and seed the old file's next mapping.
static int wasix_map_is_writable_shared_file(const struct map *m) {
    return m->fd >= 0 && (m->flags & MAP_SHARED) != 0 && (m->flags & MAP_ANON) == 0 &&
           (m->prot & PROT_WRITE) != 0;
}

// Take ONE piece of [a, b) out of the fragment table: remove, trim or split the
// first fragment that overlaps it, pin its header (inflight), and return 1; or 0
// when nothing in [a, b) is mapped. A split needs one free slot, which the caller
// reserved before its first take. With `shared_only`, only fragments of writable
// MAP_SHARED file mappings are considered (firebox#MMP). Caller holds g_vma_lock.
static int wasix_frags_take_locked(uintptr_t a, uintptr_t b, struct wasix_piece *pc,
                                   int shared_only) {
    for (size_t i = 0; i < g_frag_count; i++) {
        struct wasix_frag *f = &g_frags[i];
        if (!(f->start < b && a < f->end)) continue;
        if (shared_only && !wasix_map_is_writable_shared_file(f->hdr)) continue;
        uintptr_t s = f->start > a ? f->start : a;
        uintptr_t e = f->end < b ? f->end : b;
        struct map *m = f->hdr;
        if (s == f->start && e == f->end) {
            *f = g_frags[--g_frag_count];
            m->nfrags--;
            wasix_map_count_dec();
        } else if (s == f->start) {
            f->start = e;
        } else if (e == f->end) {
            f->end = s;
        } else {
            uintptr_t tail_end = f->end;
            f->end = s;
            wasix_frag_add_locked(e, tail_end, m);
        }
        m->inflight++;
        pc->start = s;
        pc->end = e;
        pc->hdr = m;
        return 1;
    }
    return 0;
}

// Would unmapping [a, b) split a fragment (one strictly contains the range)?
static int wasix_frags_would_split_locked(uintptr_t a, uintptr_t b, int shared_only) {
    for (size_t i = 0; i < g_frag_count; i++) {
        if (shared_only && !wasix_map_is_writable_shared_file(g_frags[i].hdr)) continue;
        if (g_frags[i].start < a && b < g_frags[i].end) return 1;
    }
    return 0;
}

// The fragment containing `addr`, pinned (inflight) so its header outlives a
// concurrent munmap; NULL when no private mapping covers it. *end receives the
// fragment's end. Caller holds g_vma_lock.
static struct map *wasix_frags_pin_locked(uintptr_t addr, uintptr_t *end) {
    for (size_t i = 0; i < g_frag_count; i++) {
        if (g_frags[i].start <= addr && addr < g_frags[i].end) {
            *end = g_frags[i].end;
            g_frags[i].hdr->inflight++;
            return g_frags[i].hdr;
        }
    }
    return NULL;
}

// Write [start, end) of a MAP_SHARED writable file mapping back to its file,
// never past the mapping's length (the zero-filled partial-page tail is not the
// file's). firebox#7C5: a MAP_PRIVATE mapping never writes back (mmap/7-2,
// munmap/4-1). Returns 0, or -1 with errno from pwrite.
static int wasix_map_writeback(const struct map *m, uintptr_t start, uintptr_t end) {
    if (m->fd < 0 || (m->prot & PROT_WRITE) == 0 || (m->flags & MAP_PRIVATE) != 0 ||
        (m->flags & MAP_ANON) != 0)
        return 0;
    const uintptr_t addr0 = wasix_map_addr0(m);
    // firebox#MMP: the whole last page, up to the file's current end.
    const size_t limit =
        wasix_file_writeback_extent(m->fd, m->offset, wasix_posix_extent(m->length));
    if (end > addr0 + limit) end = addr0 + limit;
    const char *body = (const char *)start;
    size_t len = start < end ? (size_t)(end - start) : 0;
    off_t off = m->offset + (off_t)(start - addr0);
    while (len > 0) {
        const ssize_t n = pwrite(m->fd, body, len, off);
        if (n > 0) {
            len -= (size_t)n;
            off += n;
            body += (size_t)n;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            return -1;
        }
    }
    return 0;
}

// Release the backing once no fragment and no in-flight piece remains. Every
// fragment piece was already released on its own (a reservation piece straight
// back to the hole list); what is left is the header prefix, the enforced
// mapping's host-page slack, the dup'd fd, or — for an aligned_alloc backing,
// which cannot be freed piecewise — the whole allocation.
static void wasix_map_finalize(struct map *m) {
    const uintptr_t addr0 = wasix_map_addr0(m);
    const size_t prefix = m->prefix;
    const int fd = m->fd;
    // firebox#7KH: poison the header before releasing it (see WASIX_MMAN_DEAD).
    m->dead_magic = WASIX_MMAN_DEAD;
    if (m->backing == WASIX_MMAN_BACKING_RESERVE) {
        const size_t ext = wasix_map_extent(m);
        const size_t body_len = m->body_len;
        // firebox#SAH: the slack was host-protected with the body; lift it before
        // the allocator can hand it out again. The prefix never was protected.
        if (body_len > ext) wasix_host_widen_rw(addr0 + ext, body_len - ext);
        wasix_res_lock_acquire();
        if (body_len > ext) wasix_res_free_insert(addr0 + ext, body_len - ext);
        wasix_res_free_insert(addr0 - prefix, prefix);
        wasix_res_lock_release();
    } else {
        // An aligned_alloc backing is freed whole, so a piece munmap'd earlier
        // stayed inside the allocation — and a plain MAP_FIXED may since have
        // been placed into that hole. Handing the allocation to malloc would put
        // that live mapping's bytes on the heap; leaking it is the safe answer.
        wasix_vma_lock_acquire();
        const int covered = wasix_fixed_overlaps(addr0 - prefix, addr0 + m->body_len);
        wasix_vma_lock_release();
        if (!covered) {
            // firebox#SAH + #6Q8: restore the body's host pages to RW before
            // free() hands them back — a host-protected mapping, or a MAP_FIXED
            // segment once placed inside it with its own prot, would otherwise
            // leave pages that fault a perfectly normal access once malloc
            // reuses them.
            wasix_host_widen_rw(addr0, m->body_len);
            free((char *)addr0 - prefix);
        }
    }
    if (fd >= 0) close(fd);
}

// Release one piece taken by wasix_frags_take_locked, then drop its pin and
// finalize the backing if that was the last reference. Takes g_vma_lock.
static void wasix_piece_release(const struct wasix_piece *pc) {
    struct map *m = pc->hdr;
    // Linux writes a shared mapping's dirty pages back when they are unmapped.
    (void)wasix_map_writeback(m, pc->start, pc->end);
    if (m->backing == WASIX_MMAN_BACKING_RESERVE) {
        // The piece's pages are the reservation allocator's again, right now —
        // this is what lets a later MAP_FIXED_NOREPLACE into the freed sub-range
        // succeed (#RZA's rule). Lift any host protection first: the free-list
        // node is written into the piece, and the next owner expects RW.
        wasix_host_widen_rw(pc->start, (size_t)(pc->end - pc->start));
        wasix_res_lock_acquire();
        wasix_res_free_insert(pc->start, (size_t)(pc->end - pc->start));
        wasix_res_lock_release();
    }
    wasix_vma_lock_acquire();
    int last = --m->inflight == 0 && m->nfrags == 0;
    wasix_vma_lock_release();
    if (last) wasix_map_finalize(m);
}

// firebox#MMP: unmap, under a MAP_FIXED that replaces [a, b), every piece of a
// writable MAP_SHARED file mapping there. Its fragment must not outlive the
// replacement: its later write-back would put the NEW mapping's bytes into the OLD
// file, and wasix_seed_from_live_copy would hand them to that file's next mapping
// (mmap/3-1 on the browser, where both mappings are private copies). Each piece is
// written back first — the memory still holds the old mapping's bytes, the new
// mapping's fill comes after — exactly as munmap would. Other kinds keep their
// fragment: their bytes reach nothing but themselves, and munmap of the range trims
// both. Only file mappings qualify, and those are never reservation-backed, so the
// release never hands the range back to the reservation allocator. Returns -1
// (ENOMEM) when a split finds no fragment slot.
static int wasix_fixed_unmap_shared_files(uintptr_t a, uintptr_t b) {
    for (;;) {
        struct wasix_piece pcs[8];
        size_t k = 0;
        wasix_vma_lock_acquire();
        if (wasix_frags_would_split_locked(a, b, 1) && wasix_frags_reserve(1) != 0) {
            wasix_vma_lock_release();
            return -1;
        }
        while (k < sizeof pcs / sizeof pcs[0] && wasix_frags_take_locked(a, b, &pcs[k], 1))
            k++;
        wasix_vma_lock_release();
        for (size_t i = 0; i < k; i++) wasix_piece_release(&pcs[i]);
        if (k < sizeof pcs / sizeof pcs[0])
            return 0;
    }
}

// Drop an msync pin; finalize if a munmap emptied the mapping meanwhile.
static void wasix_map_unpin(struct map *m) {
    wasix_vma_lock_acquire();
    int last = --m->inflight == 0 && m->nfrags == 0;
    wasix_vma_lock_release();
    if (last) wasix_map_finalize(m);
}

// firebox#MMP: overlay onto a NEW private-copy mapping of a file — `dst` holds
// file bytes [off, off+extent), just pread from the file — the bytes this
// process's live SHARED mappings of the same file hold over that range.
//
// WHY: on Linux every mapping of a file is a view of one page cache, so a store
// through a MAP_SHARED mapping is visible at once to every later mapping of the
// file, MAP_PRIVATE included (POSIX: MAP_SHARED stores "change the underlying
// object"; mmap/7-4 stores through a /dev/shm MAP_SHARED mapping, forks, and maps
// the object MAP_PRIVATE in the child). Here a shared mapping is NOT the file:
// either it is the host shared window (a separate host object — the VFS file is
// only its seed), or, where there is no window (the browser, or a mapping the
// window cannot take), it is a private copy written back only at msync/munmap. So
// the file is stale while such a mapping lives, and a new mapping must take the
// overlap from the live copy instead. A forked child inherits its parent's
// mappings and their bytes, so the same rule covers mmap/7-4's child on both
// profiles. Only THIS process's mappings are visible here; a shared mapping held
// solely by an unrelated process is still read from the file (the residual of the
// no-shared-page-cache model).
//
// The window is preferred: it is the object itself. A legacy copy qualifies only
// if it is MAP_SHARED, readable, and writable — a read-only copy can hold nothing
// newer than the file. Caller does not hold g_vma_lock.
static void wasix_seed_from_live_copy(char *dst, off_t off, size_t extent, dev_t dev,
                                      ino_t ino, const struct map *self) {
    if (off < 0 || extent == 0)
        return;
    const uint64_t a = (uint64_t)off;
    const uint64_t b = a + (uint64_t)extent;
    // Every window mapping of one object shares one slot. Read no further than the
    // SHORTEST of them: the slot was sized by its first mapper, and a byte past
    // the slot is a host fault, not a byte of the object.
    int win_i = -1;
    for (int i = 0; i < g_window_count; i++) {
        if (g_window_maps[i].dev != dev || g_window_maps[i].ino != ino)
            continue;
        if (win_i < 0 || g_window_maps[i].length < g_window_maps[win_i].length)
            win_i = i;
    }
    if (win_i >= 0) {
        const uint64_t w0 = (uint64_t)g_window_maps[win_i].offset;
        const uint64_t w1 = w0 + (uint64_t)wasix_posix_extent(g_window_maps[win_i].length);
        const uint64_t s = a > w0 ? a : w0;
        const uint64_t e = b < w1 ? b : w1;
        // The window lives above memory.size: read it with raw volatile loads
        // (never memcpy, which bounds-traps there — see the routing block).
        volatile const unsigned char *win =
            (volatile const unsigned char *)g_window_maps[win_i].addr;
        for (uint64_t o = s; o < e; o++)
            dst[o - a] = win[o - w0];
        return;
    }
    wasix_vma_lock_acquire();
    for (size_t i = 0; i < g_frag_count; i++) {
        const struct map *h = g_frags[i].hdr;
        if (h == self || !h->has_ino || h->dev != dev || h->ino != ino)
            continue;
        if ((h->flags & MAP_SHARED) == 0 || (h->flags & MAP_ANON) != 0 ||
            (h->prot & (PROT_READ | PROT_WRITE)) != (PROT_READ | PROT_WRITE))
            continue;
        const uint64_t f0 = (uint64_t)h->offset + (uint64_t)(g_frags[i].start - wasix_map_addr0(h));
        const uint64_t f1 = f0 + (uint64_t)(g_frags[i].end - g_frags[i].start);
        const uint64_t s = a > f0 ? a : f0;
        const uint64_t e = b < f1 ? b : f1;
        if (s < e)
            memcpy(dst + (s - a), (const char *)g_frags[i].start + (s - f0), (size_t)(e - s));
    }
    wasix_vma_lock_release();
}

// firebox#V12/#ZBK (moved here by firebox#MMP): adopt a window slot the host just
// mapped for `fd` at `win_addr` as THIS mmap's mapping — seed it, mark the atime,
// pin the inode and count the mapping. The reasoning for each step is the routing
// block's in mmap() below; this is its single body, shared by the MAP_FIXED-into-
// the-window path (wasix_mmap_fixed_window).
//
// firebox#MMP: the seed covers the whole POSIX extent `map_len` (the 4096-rounded
// length), not just `length`. The bytes of the last page past `length` show the
// FILE's bytes where the file has them — mmap/3-1 maps page_size+2 bytes of a
// two-page file and reads pa[page_size+2], which Linux answers with the file's
// byte — and zero past the end of the file, as before (mmap/11-4/5/6).
//
// firebox#GAG: `host_cached` — the mapping came from shm_file(MAP): the host page
// cache seeded the segment and owns its bytes, so the guest seeds nothing (a guest
// re-seed would overwrite another live mapper's stores with the file's older
// bytes) and registers the entry writeback=2.
static void wasix_window_adopt(uintptr_t win_addr, uint32_t created, int fd, int prot,
                               size_t length, size_t map_len, const struct stat *st,
                               int is_shm, int is_regfile, int host_cached) {
    const int reg_reseed = (is_regfile && !is_shm && !host_cached);
    if (created || reg_reseed) {
        volatile unsigned char *win = (volatile unsigned char *)win_addr;
        unsigned char seedbuf[512];
        size_t done = 0;
        while (done < map_len) {
            size_t want = map_len - done;
            if (want > sizeof seedbuf) want = sizeof seedbuf;
            const ssize_t nread = pread(fd, seedbuf, want, (off_t)done);
            if (nread < 0 && errno == EINTR) continue;
            if (nread <= 0) {
                // End of file (or a best-effort seed failure): the rest of the
                // extent is the file's zeros. A FRESH /dev/shm object is already
                // zero, so this only writes for a regular-file RE-map, which may
                // hold a prior mapping's bytes (#ZBK, mmap/11-4).
                if (reg_reseed)
                    for (size_t z = done; z < map_len; z++) win[z] = 0;
                break;
            }
            for (ssize_t i = 0; i < nread; i++)
                win[done + (size_t)i] = seedbuf[i];   // raw i32.store8 -> window
            done += (size_t)nread;
        }
    }
    if (reg_reseed || host_cached) {
        const struct timespec atime_now[2] = {
            { .tv_sec = 0, .tv_nsec = UTIME_NOW },   // st_atime -> now
            { .tv_sec = 0, .tv_nsec = UTIME_OMIT },  // st_mtime untouched
        };
        (void)futimens(fd, atime_now);
    }
    const int held = dup(fd);
    if (held >= 0)
        wasix_window_register(win_addr, held, 0, length,
                              /*writeback=*/host_cached ? 2
                                  : (reg_reseed && (prot & PROT_WRITE) != 0),
                              st->st_dev, st->st_ino);
    wasix_map_count_inc();
}

// firebox#MMP: MAP_FIXED at the address of a live window mapping — replace it.
//
// POSIX mmap(2): with MAP_FIXED "any previous mapping ... for those whole pages
// ... shall be removed" and the new one takes its place at exactly `addr`
// (mmap/3-1 maps a second file over a live MAP_SHARED mapping of the first and
// must read the second file's bytes there). A window address is not linear memory
// mman owns — it is host pages aliasing an OS object — so the private MAP_FIXED
// path cannot place anything there (its grow fails: ENOMEM). The host can:
// shm_file MAP_AT maps the new object's segment over the old one.
//
// Only a mapping the window can hold is placed there: MAP_SHARED, offset 0, a
// /dev/shm object or a writable regular file — exactly the routing rule of mmap()
// below. Anything else, and any host refusal (a replacement that would split a
// mapping, or one that is not host-page aligned), fails with ENOMEM and leaves
// the old mapping as it was. An honest failure: the private path's answer for
// this address was already ENOMEM, and a half-replaced mapping would be a lie.
// MAP_FIXED_NOREPLACE over a live mapping is EEXIST, as on Linux.
static void *wasix_mmap_fixed_window(void *addr, size_t length, int prot, int flags,
                                     int fd, off_t offset) {
    const uintptr_t a = (uintptr_t)addr;
#ifdef MAP_FIXED_NOREPLACE
    if ((flags & MAP_FIXED_NOREPLACE) != 0) {
        errno = EEXIST;
        return MAP_FAILED;
    }
#endif
    struct stat st;
    if ((flags & MAP_SHARED) == 0 || (flags & MAP_ANON) != 0 || fd < 0 || offset != 0 ||
        fstat(fd, &st) != 0) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    const int is_shm = __wasix_is_shm_inode(st.st_ino);
    const int is_regfile = S_ISREG(st.st_mode);
    if (!(is_shm || (is_regfile && (prot & PROT_WRITE) != 0))) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    const size_t map_len = wasix_posix_extent(length);
    // The old mapping's bytes reach its file before its pages are replaced, as a
    // munmap would write them back.
    wasix_window_writeback(a, (size_t)-1);
    // firebox#RST B3: the host resolves `fd` exactly as MAP does, so a MAP_FIXED
    // over a /dev/shm object maps the object shm_open+mmap maps — never a
    // zero-filled twin keyed by st_ino — and the page cache seeds and claims it.
    uint64_t at = (uint64_t)a;
    const int32_t map_error =
        __wasilibc_shm_file(WASIX_SHM_FILE_MAP_AT, fd, (uint64_t)map_len, &at);
    if (map_error != 0) {
        // No window cannot happen at a window address; either way the old
        // mapping stays as it was and the MAP_FIXED fails honestly.
        errno = map_error == ENOTSUP ? ENOMEM : map_error;
        return MAP_FAILED;
    }
    // Every mapping that started at `a` is gone: drop its inode pin and its count
    // (and its host claim, unless the new mapping is of the same object).
    while (wasix_window_release(a, NULL, &st))
        wasix_map_count_dec();
    wasix_window_adopt(a, 0, fd, prot, length, map_len, &st, is_shm, is_regfile,
                       /*host_cached=*/1);
    return addr;
}

void *mmap(void *addr, size_t length, int prot, int flags,
           int fd, off_t offset) {
    // firebox#796: a fixed-address request (blink's wasm64 linear mapping) is
    // placed AT addr rather than malloc'd. MAP_FIXED overwrites whatever is at
    // [addr, addr+len), as on Linux. firebox#RZA: MAP_FIXED_NOREPLACE succeeds
    // ONLY when mman can PROVE the whole range unowned — it lies at or above the
    // current linear-memory size (the grow owns it) or inside a hole mman itself
    // tracks (a munmap'd owned range, or a reservation it got back). Everything
    // else — the malloc heap, the stack, static data, a live mapping, anything
    // mman cannot vouch for — fails EEXIST, the Linux answer. Blink's MAP_DEMAND
    // is MAP_FIXED_NOREPLACE, and its guest brk growth into the host's own heap
    // used to "succeed" here and overwrite live memory (#YAB). PROT_EXEC is
    // allowed (blink maps executable guest segments PROT_READ|PROT_EXEC; on wasm
    // the page is data either way — the JIT executes via the host, not guest
    // page perms).
    int fixed_req = (flags & MAP_FIXED) != 0;
#ifdef MAP_FIXED_NOREPLACE
    fixed_req = fixed_req || (flags & MAP_FIXED_NOREPLACE) != 0;
#endif
    if (fixed_req && addr != NULL && length != 0) {
        // firebox#7C5/#QAB: POSIX mmap(2) — "If MAP_FIXED is set ... and addr is
        // not a multiple of the page size ... mmap() shall fail [EINVAL]"
        // (mmap/9-1). The page size is the POSIX system page, 4096 since #QAB
        // (matching Linux). Blink's wasm64 linear mapping always passes 64 KiB-
        // aligned addresses (FLAG_pagesize == 0x10000), and 64 KiB is a multiple
        // of 4096, so loosening this gate from 64 KiB to 4096 is monotonic — every
        // Blink address still passes (the #796 path is unaffected) while a
        // genuinely-illegal 4096-unaligned fixed request a portable program would
        // also be denied on Linux stays rejected.
        if (((uintptr_t)addr & (WASIX_MMAN_SYS_PAGE_SIZE - 1)) != 0) {
            errno = EINVAL;
            return MAP_FAILED;
        }
        // firebox#MMP: a fixed request at a live window mapping's address is a
        // replacement of THAT mapping, which only the host can perform.
        if (wasix_window_addr_live((uintptr_t)addr))
            return wasix_mmap_fixed_window(addr, length, prot, flags, fd, offset);
        return wasix_mmap_fixed(addr, length, prot, flags, fd, offset);
    }
    // Check for unsupported flags.
    if ((flags & (MAP_PRIVATE | MAP_SHARED)) == 0 ||
#ifdef MAP_SHARED_VALIDATE
        (flags & MAP_SHARED_VALIDATE) == MAP_SHARED_VALIDATE ||
#endif
#ifdef MAP_NORESERVE
        (flags & MAP_NORESERVE) != 0 ||
#endif
#ifdef MAP_GROWSDOWN
        (flags & MAP_GROWSDOWN) != 0 ||
#endif
#ifdef MAP_HUGETLB
        (flags & MAP_HUGETLB) != 0 ||
#endif
#ifdef MAP_FIXED_NOREPLACE
        (flags & MAP_FIXED_NOREPLACE) != 0 ||
#endif
        0)
    {
        errno = EINVAL;
        return MAP_FAILED;
    }

    // firebox#7C5: accept every PROTECTION combination POSIX/Linux accepts
    // (mmap/5-1 maps PROT_NONE, PROT_EXEC, and every RWX permutation and
    // requires each to either SUCCEED or fail with ENOTSUP — never any other
    // errno). The pre-#7C5 code rejected PROT_NONE and any PROT_EXEC with
    // EINVAL, which is a wrong errno AND a wrong behavior: Linux maps all of
    // these. The malloc-backed emulation has no MMU, so it cannot ENFORCE a
    // protection weaker than read-write — the memory is always RW. That is the
    // documented no-MMU known-gap (PROT_NONE/PROT_READ-only ranges do not fault
    // on a disallowed access; see docs/reference/runtime-gotchas), NOT a reason
    // to refuse the mapping. Refusing PROT_NONE/PROT_EXEC broke the two real
    // Linux idioms that depend on them — reserve-with-PROT_NONE-then-mprotect,
    // and JIT map-RX — so we accept the request and simply provide RW backing.
    // No prot combo returns ENOTSUP today (all are honored as RW), so the test's
    // ENOTSUP branch is permitted-but-unused.

    //  To be consistent with POSIX.
    if (length == 0) {
        errno = EINVAL;
        return MAP_FAILED;
    }

    // firebox#7C5/#QAB: POSIX mmap(2) — `off` must be a multiple of the page size;
    // otherwise EINVAL (mmap/11-1). Mirrors the musl top-half OFF_MASK check
    // (the unused-on-wasm path) and the Linux do_mmap() `offset & ~PAGE_MASK`
    // gate, against the POSIX system page size (4096 since #QAB), which is what
    // sysconf(_SC_PAGE_SIZE) reports and the test computes its illegal offset
    // from. ANON mappings ignore offset, so this only gates file-backed maps.
    if ((flags & MAP_ANON) == 0 &&
        ((uint64_t)offset & (uint64_t)(WASIX_MMAN_SYS_PAGE_SIZE - 1)) != 0) {
        errno = EINVAL;
        return MAP_FAILED;
    }

    // firebox#TTB: for a file-backed mapping, validate the POSIX preconditions
    // (EBADF / EACCES / EOVERFLOW) BEFORE allocating, matching musl/Linux. An
    // ANON mapping has no fd and skips these. The length==0 EINVAL above still
    // takes precedence (mmap/32-1 keeps returning EINVAL for a len=0 request on
    // a valid fd, not EBADF/EACCES).
    if ((flags & MAP_ANON) == 0) {
        if (wasix_mmap_check_file_preconditions(prot, flags, fd, offset,
                                                length) != 0) {
            return MAP_FAILED;
        }
    }

    // firebox#9VY-24: enforce the Linux vm.max_map_count ceiling (mmap/24-1).
    // AFTER the precondition checks so a bad fd / wrong-type / access error takes
    // precedence (POSIX errno ordering: EBADF/ENODEV/EACCES/EINVAL before
    // ENOMEM), and BEFORE the window routing + allocation so no resource is
    // consumed once the process is at its mapping limit. Covers the ANON,
    // file-private, and /dev/shm-window paths — every real VMA — but not the
    // blink MAP_FIXED path (returned above). See WASIX_MMAN_MAX_MAP_COUNT.
    if (wasix_map_count_would_exceed()) {
        errno = ENOMEM;
        return MAP_FAILED;
    }

    // firebox#R23: if mlockall(MCL_FUTURE) is in effect and the caller lacks
    // CAP_IPC_LOCK, every new mapping must be locked into memory; if locking it
    // would exceed RLIMIT_MEMLOCK the mapping fails EAGAIN (mmap/18-1, whose
    // header quotes "[EAGAIN] The mapping could not be locked in memory, if
    // required by mlockall(), due to a lack of resources"). Root/CAP_IPC_LOCK
    // bypasses. INERT for every ordinary mmap (fires only under MCL_FUTURE AND
    // unprivileged), so it cannot regress any root-run mmap test. Placed after
    // the fd-precondition + map-count gates (POSIX errno precedence) and before
    // any allocation / window routing, so no resource is consumed on denial.
    // The success path deliberately does NOT mutate g_locked_bytes: the
    // observable POSIX contract for 18-1 is the EAGAIN denial, and cumulative
    // MCL_FUTURE lock accounting is advisory + unexercised by the corpus, while
    // mutating it here would be inconsistent with mmap's early-returning window
    // and fixed-mapping success paths (explicit mlock()/munlock() own the count).
    if ((wasix_mlock_mode() & MCL_FUTURE) && !wasix_has_ipc_lock()) {
        rlim_t lim = wasix_memlock_limit();
        if ((rlim_t)length > lim - (rlim_t)wasix_locked_bytes()) {
            errno = EAGAIN;
            return MAP_FAILED;
        }
    }

    // firebox#61X (Stage-1 1b-libc) + #ZBK: a MAP_SHARED mapping of a /dev/shm
    // object OR a regular file is a cross-process shared-memory region. Route it to
    // the host shared window — raw stores/loads are then coherent across processes
    // and across fork() (fork/16-1; getpid/1-1; the pthread PROCESS_SHARED
    // cond+mutex cluster) — instead of the private aligned_alloc+pread emulation
    // below. Gated tightly so the legacy path keeps every case the window must NOT
    // take over:
    //   * MAP_SHARED only — a MAP_PRIVATE mapping (e.g. fork/16-1's second object,
    //     mmap/7-2/7-3) is copy-on-write and must stay a private copy.
    //   * regular files AND /dev/shm objects (both are S_ISREG), both through the
    //     host page cache (shm_file MAP, below): the host seeds the segment, and
    //     writes it back to the object at msync, the last munmap, exec and exit.
    //   * offset 0 only — the host maps the object from its start; a non-zero
    //     offset into a windowed object is the §7.4 segment-sizing follow-up, so
    //     it falls through to the legacy path rather than mis-map.
    // Without a shared window, keep the legacy file mapping path. Other
    // failures from the descriptor route must reach the caller.
    if ((flags & MAP_SHARED) != 0 && (flags & MAP_ANON) == 0 && fd >= 0 &&
        offset == 0) {
        struct stat shm_st;
        const int fstat_ok = (fstat(fd, &shm_st) == 0);
        // A /dev/shm object is also S_ISREG, so the writeback/re-seed discriminator
        // is `is_regfile && !is_shm` (a plain regular file, not a /dev/shm object).
        const int is_shm = fstat_ok && __wasix_is_shm_inode(shm_st.st_ino);
        const int is_regfile = fstat_ok && S_ISREG(shm_st.st_mode);
        // #ZBK: a regular file routes to the window ONLY when it is WRITABLE (has
        // PROT_WRITE). A sub-RW-protection regular-file MAP_SHARED (PROT_READ-only
        // or PROT_NONE — mmap/6-1/6-2/6-3) must keep the legacy aligned_alloc path
        // so its #SAH host-mprotect enforcement faults a disallowed access — the
        // shared window is shared host pages and cannot carry a per-mapping sub-RW
        // protection. A read-only regular file also has nothing to make
        // cross-fork-coherent (it cannot be written through), so excluding it loses
        // no coherence. /dev/shm keeps its prot-agnostic #V12 routing unchanged
        // (no /dev/shm conformance test needs sub-RW enforcement).
        if (fstat_ok && (is_shm || (is_regfile && (prot & PROT_WRITE) != 0))) {
            // firebox#F4A-partialpage/#QAB: POSIX mmap(2) maps WHOLE pages — "the
            // implementation shall include, in any mapping operation, any partial
            // page specified by [pa, pa+len)" and "shall always zero-fill any
            // partial page at the end of an object" (mmap/11-4/5/6). The
            // window-routed path must therefore back the FULL POSIX page(s) the
            // mapping touches — round the mapped length up to the POSIX system page
            // (WASIX_MMAN_SYS_PAGE_SIZE, 4096 since #QAB), exactly as the legacy
            // aligned_alloc path does via `body_len` below. Without this, a sub-page
            // mapping (a `length` not a multiple of 4096) maps only `length` bytes
            // at the window; a conformant read of the zero-fill tail
            // [length, round_up(length, 4096)) runs off the mapped OS object and
            // HeapAccessOutOfBounds-traps. This window path is NEVER host-protected
            // (it requires is_shm or a WRITABLE regular file — never a sub-RW
            // enforced mapping), so the 4096 POSIX page is exactly the right unit:
            // no host-page isolation is needed here (that is the private
            // aligned_alloc path's concern). The host's own `page_round_up` rounds
            // to the host page (>= 4096), a superset of the guest's 4096 round, so
            // the OS object always covers the guest tail. The fresh OS object is
            // zero-filled, so the rounded tail reads as zero (11-5's assertion) for
            // free. `length` is kept for the fd seed below: only the bytes actually
            // in the object are seeded; the rounded tail stays the object's zeros.
            uint64_t shm_map_len = (uint64_t)length;
            {
                const uint64_t mask = (uint64_t)(WASIX_MMAN_SYS_PAGE_SIZE - 1);
                if ((shm_map_len & mask) != 0) {
                    uint64_t rounded;
                    if (__builtin_add_overflow(
                            shm_map_len,
                            (uint64_t)WASIX_MMAN_SYS_PAGE_SIZE - (shm_map_len & mask),
                            &rounded)) {
                        errno = ENOMEM;
                        return MAP_FAILED;
                    }
                    shm_map_len = rounded;
                }
            }
            // firebox#GAG/#RST: the object goes through the HOST page cache. The
            // old guest-managed window re-seeded a regular file's segment on
            // every map (discarding a live mapper's unsynced stores), reached the
            // file only by its own pwrite at msync/munmap (so a process that
            // exited, _exited, execed or died by a signal without munmap lost its
            // stores), and sized a /dev/shm object by its first mapper's seed.
            // With shm_file the host binds the segment to the object, seeds it
            // itself, writes it back whenever a mapper goes, and read()/write()/
            // ftruncate() see and update the same bytes.
            //
            // firebox#RST B1: ENOTSUP is "this memory has no window" (the
            // browser, a small wasm64 memory) — the mapping becomes the private
            // copy below, as it always was there, so sem_open and shm_open+mmap
            // keep working. Any other error is a real failure with a window
            // present and is mmap's answer.
            uint64_t file_off = 0;
            const int32_t map_error =
                __wasilibc_shm_file(WASIX_SHM_FILE_MAP, fd, shm_map_len, &file_off);
            if (map_error == 0) {
                wasix_window_adopt((uintptr_t)file_off, 0, fd, prot, length,
                                   (size_t)shm_map_len, &shm_st, is_shm, is_regfile,
                                   /*host_cached=*/1);
                return (void *)(uintptr_t)file_off;
            }
            if (map_error != ENOTSUP) {
                errno = map_error;
                return MAP_FAILED;
            }
            // else: no window — fall through to the private emulation below.
        }
    }

    // firebox#QAB: decide the backing discriminant FIRST — whether the mapping
    // must ENFORCE a sub-RW protection (missing PROT_WRITE) — because since #QAB it
    // also selects the page granularity the body is rounded to:
    //   - An ENFORCED mapping is host-protected via region::protect, which operates
    //     at HOST-page granularity. Its body must round to the host-page superset
    //     (WASIX_MMAN_HOST_PAGE_SIZE, 64 KiB) so the protection covers only whole
    //     host pages of THIS mapping and cannot touch a neighbour. Rounding it to
    //     the 4096 POSIX page would leave the body's tail sharing a host page with
    //     the next allocation — an Inv-0 corruption hazard.
    //   - An ORDINARY read-write mapping only needs the POSIX partial-page
    //     zero-fill extent (WASIX_MMAN_SYS_PAGE_SIZE, 4096 since #QAB).
    // NB: an enforced mapping's layout is therefore byte-identical to pre-#QAB
    // (still 64 KiB body + 64 KiB prefix); only ordinary RW mappings change (body
    // now rounds to the faithful 4096 POSIX page instead of 64 KiB).
    const int enforce = wasix_prot_needs_enforcement(prot);
    const size_t body_page = enforce ? WASIX_MMAN_HOST_PAGE_SIZE
                                     : WASIX_MMAN_SYS_PAGE_SIZE;

    // firebox#7C5/#QAB: POSIX mmap(2) maps WHOLE pages. "The system shall always
    // zero-fill any partial page at the end of an object" (mmap/11-4/5/6): when
    // `length` is not a multiple of the page size, the bytes from the file/object
    // end up to the end of the last mapped page are accessible and read as zero.
    // The malloc-backed body must therefore span round_up(length, body_page), not
    // just `length`, or a conformant program reading those trailing bytes runs
    // off the end of the allocation (a heap OOB). `body_page` is the 4096 POSIX
    // page for an ordinary mapping; for an enforced mapping it is the 64 KiB host
    // page, a superset of the POSIX zero-fill extent that additionally guarantees
    // host-page isolation for region::protect. We record this rounded body length
    // so munmap()/msync() also operate on the true mapped extent.
    size_t body_len = length;
    {
        const size_t mask = body_page - 1;
        if ((body_len & mask) != 0) {
            size_t rounded;
            if (__builtin_add_overflow(body_len, body_page - (body_len & mask),
                                       &rounded)) {
                errno = ENOMEM;
                return MAP_FAILED;
            }
            body_len = rounded;
        }
    }

    // firebox#SAH/#QAB: the backing prefix follows the same `enforce` flag. An
    // enforceable mapping is backed by host pages that belong to it ALONE: the body
    // is host-page-isolated (WASIX_MMAN_HOST_PAGE_SIZE, the safe superset of any
    // host page — 4 KiB Linux / 16 KiB Apple Silicon; rounded above) so the later
    // region::protect cannot disturb a neighbour, and the header lives in a matching
    // host-page prefix. An ordinary read-write mapping keeps the compact 4 KiB
    // prefix (unchanged from pre-#SAH). EITHER WAY the header sits at
    // addr - WASIX_MMAN_PAGE_SIZE (uniform munmap()/msync() recovery); free()
    // takes addr - prefix.
    const size_t prefix = enforce ? WASIX_MMAN_HOST_PAGE_SIZE : WASIX_MMAN_PAGE_SIZE;

    // Compute allocation size: rounded body plus one full prefix page for the
    // header. Overflow-check before passing to aligned_alloc.
    size_t buf_len = 0;
    if (__builtin_add_overflow(body_len, prefix, &buf_len)) {
        errno = ENOMEM;
        return MAP_FAILED;
    }

    // firebox#7KH: anonymous mappings are VMA reservations, not heap — back them
    // from the reservation allocator (out of the dlmalloc arena) so GiB-scale ANON
    // churn at the grow ceiling never corrupts dlmalloc's free list. File-backed
    // mappings keep the aligned_alloc + pread emulation. `needs_zero` records
    // whether the backing is STALE: dlmalloc memory always is; a reservation is
    // stale only when reused from the free list (a fresh memory.grow extent is
    // zero by wasm semantics). (base + prefix) is the user-visible mapping; it is
    // prefix-aligned (== 64 KiB / host-page aligned for an enforceable mapping)
    // because both backings return a prefix-aligned base.
    const int reserve_backed = (flags & MAP_ANON) != 0;
    int needs_zero = 1;                       // dlmalloc memory is always stale
    void *base;
    if (reserve_backed) {
        base = wasix_res_alloc(prefix, buf_len, &needs_zero);
    } else {
        base = aligned_alloc(prefix, buf_len);
    }
    if (!base) {
        errno = ENOMEM;                       // POSIX mmap exhaustion errno
        return MAP_FAILED;
    }

    // User-visible mapping starts one prefix past the alloc base.
    addr = (char *)base + prefix;

    // Initialize the header — it sits exactly one WASIX_MMAN_PAGE_SIZE below the
    // user addr regardless of prefix, so munmap()/msync() recover it uniformly
    // (for the legacy 4 KiB prefix this is `base` itself; for the 64 KiB prefix
    // it is the last 4 KiB slot of the prefix page).
    struct map *map = (struct map *)((char *)addr - WASIX_MMAN_PAGE_SIZE);
    map->prot = prot;
    map->flags = flags;
    map->offset = offset;
    map->length = length;
    map->body_len = body_len;
    map->prefix = prefix;
    map->host_protected = 0;
    map->backing = reserve_backed ? WASIX_MMAN_BACKING_RESERVE
                                  : WASIX_MMAN_BACKING_DLMALLOC;
    map->nfrags = 0;                  // firebox#FWX: registered below, once seeded
    map->inflight = 0;
    map->dead_magic = 0;              // live (poisoned to WASIX_MMAN_DEAD in munmap)
    map->has_ino = 0;                 // firebox#MMP: set below for a file mapping

    // firebox#7C5/#7KH: zero the ENTIRE body up front so the partial-page tail
    // [filelen, body_len) reads as zero for a file-backed mapping (11-4/5/6) and
    // an ANON mapping is fully zero. SKIP the memset when the backing is a FRESH
    // reservation extent (needs_zero==0, already zero) AND the mapping is
    // PROT_NONE: a PROT_NONE map has no legal read (no guest mprotect() exists in
    // wasix-libc to make it readable; native additionally host-faults access), so
    // its zeroing is unobservable — and skipping it is exactly what preserves
    // Linux overcommit for t_vmfill's multi-GiB PROT_NONE fills (writing them
    // would commit ~3.75 GiB of host pages the test never reads). A file-backed
    // mapping (reserve_backed==0, needs_zero==1) is memset exactly as before; the
    // subsequent pread overwrites only the file bytes, leaving the tail zero.
    if (needs_zero && !(reserve_backed && prot == PROT_NONE))
        memset(addr, 0, body_len);

    // Initialize the main memory buffer with the contents of a file (the tail
    // past EOF stays zero from the memset above). ANON mappings are already
    // fully zeroed.
    if ((flags & MAP_ANON) == 0) {
        int new_fd = dup(fd);

        if (new_fd < 0) {
            // firebox#TTB: POSIX mmap returns MAP_FAILED on error, never NULL,
            // and must preserve dup()'s errno (e.g. EMFILE) rather than
            // overwriting it with EINVAL. (The fd-validity precondition above
            // already caught EBADF; this guards the remaining dup failures.)
            free(base);
            return MAP_FAILED;
        }

        map->fd = new_fd;

        // firebox#MMP: read the whole POSIX extent (the last page included, see
        // wasix_posix_extent); anything past the file end remains zero from the
        // memset. A short file (pread returns 0 before the extent is exhausted)
        // leaves the remainder zero too.
        char *body = (char *)addr;
        size_t to_read = wasix_posix_extent(length);
        off_t roff = offset;
        while (to_read > 0) {
            const ssize_t nread = pread(fd, body, to_read, roff);
            if (nread < 0) {
                if (errno == EINTR)
                    continue;
                // firebox#TTB: a pread failure must not leak the backing
                // allocation or the dup'd fd. Release both, preserve errno,
                // and return MAP_FAILED per POSIX.
                close(new_fd);
                free(base);
                return MAP_FAILED;
            }
            if (nread == 0)
                break;
            to_read -= (size_t)nread;
            roff += nread;
            body += (size_t)nread;
        }

        // firebox#MMP: the file is not the newest copy of its bytes while this
        // process holds a live shared mapping of it — see wasix_seed_from_live_copy.
        {
            struct stat id;
            if (fstat(fd, &id) == 0) {
                map->has_ino = 1;
                map->dev = id.st_dev;
                map->ino = id.st_ino;
                wasix_seed_from_live_copy((char *)addr, offset, wasix_posix_extent(length),
                                          id.st_dev, id.st_ino, map);
            }
        }

        // firebox#TV9: POSIX mmap(2) — "The st_atime field of the mapped file
        // ... shall be marked for update by the first read or write reference
        // to the mapped region" (mmap/13-1). On this no-MMU target a reference
        // through the mapping touches malloc-backed memory and cannot trap, so
        // there is no page fault to hang the atime mark on. The faithful
        // approximation is the eager pread just performed: it IS a genuine read
        // of the backing file's bytes that backs every later in-memory read
        // reference, so we mark st_atime here — at map time — which POSIX
        // explicitly permits ("[st_atime] may be marked for update at any time
        // between the mmap() call and the corresponding munmap() call"). Mark
        // atime ONLY (UTIME_OMIT on mtime): a read reference must never bump
        // st_mtime, and a MAP_PRIVATE mapping still reads the underlying file so
        // its atime is marked too (no conformance test asserts atime-unchanged;
        // only 13-1/14-1 touch timestamps in the whole mmap/munmap/msync
        // corpus). Best-effort — a mapping whose fd lacks FD_FILESTAT_SET_TIMES
        // still maps successfully (POSIX requires the field be "marked", not
        // that mmap fail), so the result is deliberately discarded.
        {
            const struct timespec atime_now[2] = {
                { .tv_sec = 0, .tv_nsec = UTIME_NOW },   // st_atime -> now
                { .tv_sec = 0, .tv_nsec = UTIME_OMIT },  // st_mtime untouched
            };
            (void)futimens(fd, atime_now);
        }
    } else {
        map->fd = -1;
    }

    // firebox#SAH: apply the host protection AFTER seeding. The memset + pread
    // above are HOST writes into the body, which must land while it is still RW;
    // only now do we restrict it so a later GUEST access that violates `prot`
    // faults on the host → guest SIGSEGV (mmap/6-1/6-2/6-3). A -1 return (browser
    // js/v8 where the capability is absent, or an unexpected host error) leaves
    // the body RW — the mapping is never FAILED for lack of enforcement, which is
    // exactly the no-MMU degradation the browser profile declares via
    // sysconf(_SC_MEMORY_PROTECTION) == -1. For an enforced mapping `body_len` is
    // WASIX_MMAN_HOST_PAGE_SIZE-aligned (64 KiB — the `enforce` path rounded it to
    // the host page above, NOT the 4096 POSIX page), so the host mprotect covers
    // whole host pages that back ONLY this mapping and never a neighbour.
    if (enforce &&
        __wasilibc_mprotect_host((uintptr_t)addr, body_len, prot) == 0) {
        map->host_protected = 1;
    }

    // firebox#FWX: record the mapping in the fragment table, so munmap/msync can
    // find it from ANY address inside it. firebox#9VY-24: the fragment counts
    // toward vm.max_map_count (wasix_frag_add_locked); its removal balances it.
    // A table that cannot grow fails the mapping ENOMEM, releasing what was built.
    wasix_vma_lock_acquire();
    if (wasix_frags_reserve(1) != 0) {
        wasix_vma_lock_release();
        map->inflight = 1;   // no fragment was ever added: finalize directly
        struct wasix_piece whole = { (uintptr_t)addr,
                                     (uintptr_t)addr + wasix_map_extent(map), map };
        const int held_fd = map->fd;
        map->fd = -1;        // nothing to write back; the fd is closed below
        wasix_piece_release(&whole);
        if (held_fd >= 0) close(held_fd);
        errno = ENOMEM;
        return MAP_FAILED;
    }
    wasix_frag_add_locked((uintptr_t)addr, (uintptr_t)addr + wasix_map_extent(map), map);
    wasix_vma_lock_release();
    return addr;
}

int munmap(void *addr, size_t length) {
    // firebox#61X: a window mapping (addr >= SHM_BASE, above the grow-capped
    // heap) has no malloc header — the host owns the segment via the inode
    // registry — so guest munmap is a successful no-op, like a fixed mapping.
    // MUST precede the standard EINVAL precondition below, which rejects
    // addr >= memory_size (the window lives above the grow-capped memory size)
    // and would otherwise mis-reject a genuine window unmap.
    //
    // firebox#V12 gap #3 (wasm64 width-generalization): reachable on wasm64 too.
    // Before #F4A no window was installed on wasm64, so this guard was #if-out and
    // a window munmap fell through to the standard EINVAL block below → "munmap():
    // Invalid argument" (shm_open/28-1, 28-3 UNRESOLVED). Under the #F4A window
    // core the wasm64 window lives at the SAME SHM_BASE in the conformance posture
    // (see WASIX_SHM_WINDOW_BASE), so the guard is now correct — and REQUIRED — on
    // both widths: it turns the window unmap into the no-op it is and releases the
    // gap-#3 inode-lifetime pin (below).
    //
    // firebox#61X-regfix (conformance wave-1): the no-op MUST still enforce the
    // geometry-INDEPENDENT POSIX munmap EINVAL preconditions. The window's
    // numeric range [SHM_BASE, 4 GiB) overlaps wild addresses a conformant
    // program must be told are invalid — munmap/8-1 calls munmap((void *)-1, 1),
    // and (void *)-1 == 0xFFFFFFFF falls inside the window range yet is not a
    // valid mapping. Before #61X that request reached the precondition block
    // below and returned EINVAL (addr >= memory_size); the unconditional no-op
    // regressed it to Success. A genuine window pointer is host-page-aligned
    // (the host slot allocator rounds slot_offset to the host page, >= 4096, and
    // SHM_BASE is 64 KiB-aligned) and the unmap length is non-zero, so the
    // alignment + length checks pass a real unmap through to the no-op while
    // rejecting the malformed request. We deliberately do NOT apply the
    // memory_size / malloc-header geometry checks here — the whole reason this
    // guard exists is that the window legitimately lives above memory_size, and
    // the host (not the guest free-list) owns the segment lifetime.
    if ((uintptr_t)addr >= WASIX_SHM_WINDOW_BASE) {
        uintptr_t wa = (uintptr_t)addr;
        if (length == 0 ||                              // empty range
            (wa & (WASIX_MMAN_PAGE_SIZE - 1)) != 0) {   // addr not page-aligned
            errno = EINVAL;
            return -1;
        }
        // firebox#ZBK: flush a regular-file MAP_SHARED window back to its backing
        // file BEFORE releasing the pin (the release closes the held fd this
        // write-back pwrites to). Whole-mapping flush (SIZE_MAX cap — a window
        // munmap unmaps the entire mapping). A /dev/shm entry is a no-op
        // (writeback=0) — the window IS the shared object, nothing to flush.
        wasix_window_writeback(wa, (size_t)-1);
        // firebox#V12 gap #3: release the held /dev/shm inode-lifetime pin for
        // this window mapping (see wasix_window_register). A no-op if none held.
        size_t mapped_len = 0;
        const int had_entry = wasix_window_release(wa, &mapped_len, NULL);
        // firebox#MMP: and UNMAP it — POSIX munmap: "further references to these
        // pages shall result in the generation of a SIGSEGV signal" (munmap/1-1,
        // 1-2). The host returns the slot's pages to the inaccessible reservation
        // (shm_file RELEASE), so a later access faults and is delivered as SIGSEGV. Only
        // when the range covers the whole mapping and no other mapping of the same
        // slot remains in this process: the host unmaps a slot whole (its pages are
        // host pages, larger than the 4096 POSIX page), so a partial munmap keeps
        // the pages mapped, as it always has — a lost fault, never a forged one. A
        // mapping the registry could not track (no entry) is left alone for the
        // same reason. A host refusal (the browser has no window, so it never gets
        // here) leaves the old no-op behaviour.
        if (had_entry && wasix_posix_extent(length) >= wasix_posix_extent(mapped_len) &&
            !wasix_window_addr_live(wa)) {
            uint64_t release_at = (uint64_t)wa;
            (void)__wasilibc_shm_file(WASIX_SHM_FILE_RELEASE, -1, 0, &release_at);
        }
        // firebox#9VY-24: balance the window-map increment (vm.max_map_count).
        wasix_map_count_dec();
        return 0;
    }

    // POSIX/Linux munmap EINVAL preconditions: addr must be a multiple of the
    // page size, len must be non-zero, and the range must not wrap the address
    // space (Linux: `len > TASK_SIZE || addr > TASK_SIZE - len`). munmap/3-1,
    // 8-1 and 9-1 hold these. Nothing else is an error.
    //
    // firebox#FWX: munmap unmaps ANY page-aligned range, as on Linux — a
    // sub-range of one mapping (head, tail or middle), a range spanning several
    // mappings, and a range with unmapped holes in it, all return 0 and leave
    // every page outside the range mapped with its contents and protection. A
    // range that covers nothing mman mapped (never mapped, already unmapped,
    // above the linear-memory size) is a successful no-op, as on Linux. Before
    // #FWX only a whole mapping at its exact start could be unmapped, and the
    // mapping was found by reading a header one page below `addr` — any other
    // range read user bytes as a header and failed EINVAL (or, for a fixed
    // mapping, dropped all of it). Mappings are now found through the fixed
    // registry and the private fragment table, never by reading below addr.
    uintptr_t a = (uintptr_t)addr, b;
    if (length == 0 ||                                       // empty range
        (a & (WASIX_MMAN_SYS_PAGE_SIZE - 1)) != 0 ||         // addr not page-aligned
        __builtin_add_overflow(a, (uintptr_t)length, &b) ||  // range wraps
        __builtin_add_overflow(b, (uintptr_t)WASIX_MMAN_SYS_PAGE_SIZE - 1, &b)) {
        errno = EINVAL;
        return -1;
    }
    b &= ~(uintptr_t)(WASIX_MMAN_SYS_PAGE_SIZE - 1);

    // Everything mapped in [a, b) is taken out of both registries under ONE lock
    // hold, so the unmap is atomic: no other thread can map into a piece this
    // call has freed and then have this call unmap it as well. The fixed registry
    // is trimmed in place (bookkeeping, plus the owned pieces' return to the hole
    // list). The private pieces are collected — into a stack buffer, or one from
    // the reservation allocator when many fragments overlap — and released after
    // the lock is dropped: write-back, the host-protection lift, the hole-list
    // return and the backing's final release run syscalls and free(). Every
    // shortfall (a split slot, the piece buffer) is ENOMEM before anything has
    // changed — the Linux answer when a split would exceed the mapping limit.
    struct wasix_piece stack_pcs[8];
    struct wasix_piece *pcs = stack_pcs;
    size_t pcs_bytes = 0, n = 0, k = 0;
    wasix_vma_lock_acquire();
    for (size_t i = 0; i < g_frag_count; i++)
        if (g_frags[i].start < b && a < g_frags[i].end) n++;
    if (wasix_frags_would_split_locked(a, b, 0) && wasix_frags_reserve(1) != 0) {
        wasix_vma_lock_release();
        errno = ENOMEM;
        return -1;
    }
    if (n > sizeof stack_pcs / sizeof stack_pcs[0]) {
        int needs_zero;
        pcs_bytes = (n * sizeof(struct wasix_piece) + WASIX_MMAN_PAGE_SIZE - 1) &
                    ~(size_t)(WASIX_MMAN_PAGE_SIZE - 1);
        pcs = wasix_res_alloc(WASIX_MMAN_PAGE_SIZE, pcs_bytes, &needs_zero);
        if (pcs == NULL) {
            wasix_vma_lock_release();
            errno = ENOMEM;
            return -1;
        }
    }
    if (wasix_fixed_unmap_locked(a, b) != 0) {
        wasix_vma_lock_release();
        if (pcs_bytes) {
            wasix_res_lock_acquire();
            wasix_res_free_insert((uintptr_t)pcs, pcs_bytes);
            wasix_res_lock_release();
        }
        errno = ENOMEM;
        return -1;
    }
    // Each take removes that fragment's overlap with [a, b), so exactly n succeed.
    while (k < n && wasix_frags_take_locked(a, b, &pcs[k], 0)) k++;
    wasix_vma_lock_release();
    for (size_t i = 0; i < k; i++) wasix_piece_release(&pcs[i]);
    if (pcs_bytes) {
        wasix_res_lock_acquire();
        wasix_res_free_insert((uintptr_t)pcs, pcs_bytes);
        wasix_res_lock_release();
    }
    return 0;
}

int msync (void *addr, size_t length, int flags) {
    // firebox#R23: POSIX msync EBUSY — "MS_INVALIDATE was specified and a memory
    // lock exists for the specified address range." mlockall(MCL_CURRENT) locks
    // ALL current pages, so an MS_INVALIDATE of any mapped page then fails EBUSY
    // (mlockall/3-6 on a /dev/shm page, 3-7 on a file page). Placed FIRST — before
    // the window and fixed no-ops below — because 3-6's page is a /dev/shm
    // MAP_SHARED window mapping, which would otherwise take the window no-op and
    // return 0 (a FAIL). Safe: only MS_INVALIDATE callers with MCL_CURRENT active
    // are affected. munmap()'s internal msync uses MS_SYNC (no MS_INVALIDATE); the
    // standalone msync/* tests never call mlockall, so g_mlock_mode==0 for them ->
    // no spurious EBUSY. (A per-range lock model is possible but the coarse
    // MCL_CURRENT-active flag is faithful for the corpus, and window mappings carry
    // no struct map header to hang per-range state on.)
    if ((flags & MS_INVALIDATE) && (wasix_mlock_mode() & MCL_CURRENT)) {
        errno = EBUSY;
        return -1;
    }
    // firebox#796: fixed mappings (blink wasm64 linear mapping) have no
    // header — the bytes already live in linear memory. firebox#MMP: a written-back
    // one (a writable MAP_SHARED file mapping) flushes the requested range to its
    // file; every other fixed mapping has no file to flush and msync is a no-op
    // success.
    wasix_vma_lock_acquire();
    const int in_fixed = wasix_fixed_contains((uintptr_t)addr);
    int fixed_rc = 0;
    if (in_fixed) {
        uintptr_t ma = (uintptr_t)addr, mb;
        if (__builtin_add_overflow(ma, length, &mb)) mb = UINTPTR_MAX;
        for (int i = 0; i < g_fixed_count; i++) {
            const uintptr_t fa = g_fixed_maps[i].addr;
            const uintptr_t fb = fa + g_fixed_maps[i].len;
            if (fa < mb && ma < fb &&
                wasix_fixed_writeback_locked(i, fa > ma ? fa : ma, fb < mb ? fb : mb) != 0)
                fixed_rc = -1;
        }
    }
    const int fixed_errno = errno;
    wasix_vma_lock_release();
    if (in_fixed) {
        if (fixed_rc != 0) errno = fixed_errno;
        return fixed_rc;
    }
    // firebox#61X: a window mapping IS the live host-backed shared memory — the
    // bytes already are the shared object, there is nothing to flush — so msync
    // is a no-op success (like a fixed mapping). Precedes the header recovery
    // below (a window addr has no malloc header one page beneath it).
    //
    // firebox#61X-regfix (conformance wave-1): same bug class as munmap() above
    // — the no-op must not swallow the geometry-independent POSIX EINVAL. A
    // non-page-aligned addr in the window's numeric range is still a malformed
    // request; a genuine window pointer is host-page-aligned, so this only
    // rejects an invalid call (and never a real shared-window flush).
    //
    // firebox#V12 gap #3 (wasm64 width-generalization): width-agnostic like the
    // munmap() guard above — the #F4A window core installs the window on wasm64
    // at the same SHM_BASE in the conformance posture, so a wasm64 msync of a
    // window pointer must take this no-op path, not the header recovery below.
    if ((uintptr_t)addr >= WASIX_SHM_WINDOW_BASE) {
        if (((uintptr_t)addr & (WASIX_MMAN_PAGE_SIZE - 1)) != 0) {
            errno = EINVAL;
            return -1;
        }
        // firebox#ZBK: a regular-file MAP_SHARED window flushes its live bytes to
        // the backing file on msync (POSIX: MS_SYNC writes the mapping's changes
        // through to the underlying object). Bound the flush to the caller's
        // requested `length` (Linux msync flushes the requested range; the entry
        // caps it to the mapping length). A /dev/shm entry stays the pure no-op
        // (writeback=0) — the window IS the shared object, there is nothing to
        // flush, exactly as the #61X path already was.
        wasix_window_writeback((uintptr_t)addr, length);
        return 0;
    }
    // firebox#FWX: find the private mapping through the fragment table — any
    // address inside it, not only its first byte, and never by reading a header
    // below `addr` (which, for a fragment left by a partial munmap, is user data
    // or freed memory). An address no mapping covers is ENOMEM, as on Linux
    // ("the addresses in the range are not mapped"); so is a range running past
    // the fragment into whatever follows it.
    uintptr_t a = (uintptr_t)addr, frag_end = 0;
    wasix_vma_lock_acquire();
    struct map *map = wasix_frags_pin_locked(a, &frag_end);
    wasix_vma_lock_release();
    if (map == NULL) {
        errno = ENOMEM;
        return -1;
    }
    if (length > frag_end - a) {
        wasix_map_unpin(map);
        errno = ENOMEM;
        return -1;
    }
    // firebox#467: a PROT_READ-only mapping has nothing to flush and returns 0
    // (the pre-#467 check was inverted and rejected every legitimate caller —
    // see work/tasks/467-* and the firebox-diff corpus `file-mmap-write-read`).
    // firebox#7C5: a MAP_PRIVATE mapping never writes back (mmap/7-2: "If the
    // mapping was made with MAP_PRIVATE, msync() has no effect on the underlying
    // file"). wasix_map_writeback applies both rules and the ANON no-op, and
    // never writes past the mapping's length.
    int rc = wasix_map_writeback(map, a, a + length);
    int e = errno;
    wasix_map_unpin(map);
    if (rc != 0) errno = e;
    return rc;
}

// firebox#25D: does [a, b) overlap a HOLE mman itself tracks (a munmap'd owned
// range, or an unused tail of a grow)? Those are the only in-bounds pages mman can
// PROVE are unmapped. Takes g_res_lock.
static int wasix_res_overlaps_hole(uintptr_t a, uintptr_t b) {
    int hit = 0;
    wasix_res_lock_acquire();
    for (struct wasix_res_node *n = g_res_free_head; n != NULL; n = n->next) {
        uintptr_t nbase = (uintptr_t)n;
        if (nbase >= b) break;                    // address-ordered
        if (nbase + n->len > a) { hit = 1; break; }
    }
    wasix_res_lock_release();
    return hit;
}

// firebox#25D: is some page of [addr, addr+len) provably NOT mapped? len != 0.
// Out of linear memory (wasix_range_unmapped), or overlapping a tracked hole.
// Static data, the stack, the malloc heap, live mappings and shm windows are all
// in-bounds and not holes, so they count as mapped. Not detectable: an in-bounds
// page mman never owned and never freed (the no-MMU gap mlock already documents).
static int wasix_pages_unmapped(const void *addr, size_t len) {
    uintptr_t a = (uintptr_t)addr, end;
    if (wasix_range_unmapped(addr, len)) return 1;
    end = a + len;                                // no overflow: checked above
    return wasix_res_overlaps_hole(a & ~(uintptr_t)(WASIX_MMAN_PAGE_SIZE - 1),
                                   (end + (WASIX_MMAN_PAGE_SIZE - 1)) &
                                       ~(uintptr_t)(WASIX_MMAN_PAGE_SIZE - 1));
}

// firebox#25D: mincore reports, per page, whether it is resident. WASM linear
// memory is committed and there is no swap and no populate step, so every MAPPED
// page is resident: the state Linux reports for a process after
// mlockall(MCL_CURRENT|MCL_FUTURE), which is the same model as mlock above. Only
// bit 0 is set; the other bits are clear. Linux order: EINVAL (addr not
// page-aligned), ENOMEM (length wraps, then any page unmapped), len == 0 -> 0,
// EFAULT (vec).
//
// EFAULT: a store outside linear memory would TRAP the guest, so a vec that
// escapes [0, memory.size) (or is NULL) is EFAULT here, which is what Linux
// answers for an unwritable vec. Also detected: a vec inside a tracked hole. Not
// detectable: an in-bounds vec on a PROT_READ/PROT_NONE page (the store succeeds,
// or faults in the host where enforcement exists), or one in an untracked hole.
int mincore(void *addr, size_t length, unsigned char *vec) {
    if (((uintptr_t)addr & (WASIX_MMAN_PAGE_SIZE - 1)) != 0) {
        errno = EINVAL;
        return -1;
    }
    size_t span;
    if (__builtin_add_overflow(length, (size_t)WASIX_MMAN_PAGE_SIZE - 1, &span)) {
        errno = ENOMEM;
        return -1;
    }
    span &= ~((size_t)WASIX_MMAN_PAGE_SIZE - 1);
    if (span == 0) {
        return 0;
    }
    if (wasix_pages_unmapped(addr, span)) {
        errno = ENOMEM;
        return -1;
    }
    size_t npages = span / WASIX_MMAN_PAGE_SIZE;
    if (vec == NULL || wasix_pages_unmapped(vec, npages)) {
        errno = EFAULT;
        return -1;
    }
    for (size_t i = 0; i < npages; i++) {
        vec[i] = 1;
    }
    return 0;
}

// madvise: hint the kernel about future memory access patterns.
//
// WebAssembly has no MMU, no page caching, and the userspace emulated
// mmap in this file is malloc()-backed rather than page-backed, so all
// advice values are semantically meaningless. POSIX permits madvise to
// be a no-op as long as it validates arguments and returns 0.
//
// We accept all standard POSIX advice values and reject unknown ones
// with EINVAL.
int madvise(void *addr, size_t length, int advice) {
    (void)addr;
    (void)length;

    switch (advice) {
    case POSIX_MADV_NORMAL:
    case POSIX_MADV_RANDOM:
    case POSIX_MADV_SEQUENTIAL:
    case POSIX_MADV_WILLNEED:
    case POSIX_MADV_DONTNEED:
        return 0;
    default:
        // Linux-specific advice values (MADV_FREE, MADV_REMOVE,
        // MADV_DONTFORK, etc.) would land here. Returning EINVAL is
        // conservative; programs that probe for support get a clear
        // negative answer rather than a silent succeed.
        errno = EINVAL;
        return -1;
    }
}

// posix_madvise: same as madvise but with the POSIX-namespaced advice
// constants. The Linux ABI happens to assign identical values to
// MADV_*=POSIX_MADV_*, so the underlying behavior is identical here.
int posix_madvise(void *addr, size_t length, int advice) {
    (void)addr;
    (void)length;

    switch (advice) {
    case POSIX_MADV_NORMAL:
    case POSIX_MADV_RANDOM:
    case POSIX_MADV_SEQUENTIAL:
    case POSIX_MADV_WILLNEED:
    case POSIX_MADV_DONTNEED:
        return 0;
    default:
        return EINVAL;  // posix_madvise returns errno directly, not via errno
    }
}

// firebox#R23: mlock/munlock/mlockall/munlockall — see the process memory-lock
// accounting note above g_mlock_mode. WASM linear memory is all-resident and
// unswappable, so the RESIDENCY guarantee is a genuine no-op; the observable
// POSIX surface a portable program can depend on is the FAILURE gate
// (EPERM/EAGAIN under CAP_IPC_LOCK + RLIMIT_MEMLOCK, ENOMEM for an unmapped
// range, EINVAL for bad mlockall flags), which we reproduce byte-for-byte. This
// is the mman-layer sibling of the fs check_access root-bypass — the privilege
// gate is real even though nothing is physically pinned.

// mlock: errno precedence ENOMEM (range) -> root no-op -> EPERM/EAGAIN -> 0.
int mlock(const void *addr, size_t len) {
    // Address-validity, not permission: Linux returns ENOMEM to root too, so it
    // precedes the CAP_IPC_LOCK bypass (mlock/8-1 unmapped LONG_MAX range).
    if (wasix_range_unmapped(addr, len)) {
        errno = ENOMEM;
        return -1;
    }
    // Root/CAP_IPC_LOCK: the lock is trivially satisfied -> no-op success
    // (mlock/5-1 valid range; mlock/10-1 unaligned — Linux rounds down, does not
    // require alignment, so we succeed rather than EINVAL).
    if (wasix_has_ipc_lock()) {
        return 0;
    }
    rlim_t lim = wasix_memlock_limit();
    // rlim_cur == 0 + unprivileged -> EPERM (mlock/12-1: setrlimit(MEMLOCK,{0,0})
    // + seteuid(nonroot)).
    if (lim == 0) {
        errno = EPERM;
        return -1;
    }
    // Over a non-zero limit -> EAGAIN (lack of lockable resources).
    if ((rlim_t)len > lim - (rlim_t)wasix_locked_bytes()) {
        errno = EAGAIN;
        return -1;
    }
    __atomic_fetch_add(&g_locked_bytes, len, __ATOMIC_SEQ_CST);
    return 0;
}

// munlock: unmapped range -> ENOMEM (munlock/10-1); otherwise a successful
// unlock (munlock/7-1, 11-1). Unlocking needs no privilege on Linux.
int munlock(const void *addr, size_t len) {
    if (wasix_range_unmapped(addr, len)) {
        errno = ENOMEM;
        return -1;
    }
    // Advisory accounting, clamped at 0 (never underflow the counter).
    size_t locked = wasix_locked_bytes();
    if (locked >= len) {
        __atomic_fetch_sub(&g_locked_bytes, len, __ATOMIC_SEQ_CST);
    } else {
        __atomic_store_n(&g_locked_bytes, 0, __ATOMIC_SEQ_CST);
    }
    return 0;
}

// mlockall: EINVAL (bad flags) -> root no-op -> EPERM/ENOMEM -> 0.
int mlockall(int flags) {
    // flags==0 (mlockall/13-1) or bits outside MCL_CURRENT|MCL_FUTURE
    // (mlockall/13-2) -> EINVAL, before the mode is recorded.
    if (flags == 0 || (flags & ~(MCL_CURRENT | MCL_FUTURE))) {
        errno = EINVAL;
        return -1;
    }
    // Record the mode for the mmap MCL_FUTURE gate + the msync MCL_CURRENT EBUSY
    // facet. Per-process (rides proc_fork's private-memory copy) and reset each
    // process, so a failed mlockall in one conformance binary cannot leak into
    // another (each test is its own process).
    __atomic_store_n(&g_mlock_mode, flags, __ATOMIC_SEQ_CST);
    // Root/CAP_IPC_LOCK -> no-op success (mlockall/8-1; mmap/18-1's root
    // mlockall(MCL_FUTURE) that arms the mmap gate).
    if (wasix_has_ipc_lock()) {
        return 0;
    }
    rlim_t lim = wasix_memlock_limit();
    // rlim_cur == 0 + unprivileged -> EPERM (mlockall/15-1 + speculative/15-1).
    if (lim == 0) {
        errno = EPERM;
        return -1;
    }
    // MCL_CURRENT over the limit -> ENOMEM (lack of lockable resources).
    if ((flags & MCL_CURRENT) && (rlim_t)wasix_locked_bytes() > lim) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

// munlockall: always succeeds (munlockall/5-1). Clears the mode + accounting.
int munlockall(void) {
    __atomic_store_n(&g_mlock_mode, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&g_locked_bytes, 0, __ATOMIC_SEQ_CST);
    return 0;
}
