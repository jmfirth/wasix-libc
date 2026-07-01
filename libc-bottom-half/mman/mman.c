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
// firebox#7C5: PAGESIZE (the 64 KiB WebAssembly page = the POSIX page size
// sysconf(_SC_PAGE_SIZE) reports) for the offset-alignment / MAP_FIXED-addr /
// partial-page-zero-fill semantics. <limits.h> exposes PAGESIZE on wasm via
// arch/wasm{32,64}/bits/limits.h → <__macro_PAGESIZE.h>.
#include <limits.h>
// firebox#TTB: the POSIX mmap(2) precondition errnos (EBADF/EACCES/EOVERFLOW)
// are derived from the file descriptor's WASI rights and the offset/length
// arithmetic. <wasi/api.h> provides __wasi_fd_fdstat_get + the
// __WASI_RIGHTS_FD_{READ,WRITE} / __WASI_FILETYPE_REGULAR_FILE constants.
#include <wasi/api.h>
// firebox#61X (Stage-1 1b-libc): struct stat / fstat / st_ino to recognise a
// /dev/shm fd at mmap time and key the host shared-window mapping.
#include <sys/stat.h>

// firebox#61X: the host shared-memory-window import (defined in
// libc-bottom-half/sources/__wasixlibc_firebox.c) and the process-local
// /dev/shm inode set (defined in libc-top-half/musl/src/mman/shm_open.c). A
// MAP_SHARED mapping of a /dev/shm object routes through these to the host
// window instead of the private aligned_alloc+pread emulation, making raw
// cross-process MAP_SHARED store/load coherent (fork/16-1) and sharing the
// named-semaphore byte region. Both fall back gracefully (never mis-share).
// firebox#V12 (Stage-1 1c gap #1): shm_map also reports whether THIS call first
// created the segment (*ret_created), so the guest seeds the fresh window from
// the fd's bytes exactly once — see the routing block below.
extern int32_t __wasilibc_shm_map(uint64_t inode, uint64_t len, uint64_t *ret_offset,
                                  uint32_t *ret_created);
extern int __wasix_is_shm_inode(ino_t ino);

// firebox#61X: the base of the cross-process shared window in the wasm32 guest
// address space. A pointer at or above this was returned by mmap() routing a
// /dev/shm MAP_SHARED to the host window — it lives ABOVE the grow-capped heap,
// has NO malloc header, and the host owns its segment lifetime (the inode
// registry). munmap()/msync() must therefore treat it as a successful no-op
// (NOT recover a header / free() it). MUST MATCH wasmer shm_registry::SHM_BASE
// (= WASM32_MAX_BYTES - SHM_WINDOW_BYTES = 4 GiB - 256 MiB). wasm32 ONLY: on
// wasm64 no window is installed (shm_map returns Inval), so a high wasm64
// address is an ordinary mapping that must keep the normal munmap/msync path.
#if !defined(__wasm64__)
#define WASIX_SHM_WINDOW_BASE ((uintptr_t)0xF0000000u)
#endif

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
// munmap()/msync() recover the header via
// `(struct map *)((char *)addr - WASIX_MMAN_PAGE_SIZE)`. free() takes the
// base pointer (= addr - page), which is exactly what aligned_alloc
// returned. aligned_alloc lives in
// libc-top-half/musl/src/malloc/mallocng/aligned_alloc.c — it does NOT
// require the length argument to be a multiple of the alignment, so we
// don't round up explicitly.

#define WASIX_MMAN_PAGE_SIZE ((size_t)4096)

// firebox#7C5: the POSIX *page size* — the granularity POSIX mmap(2) defines
// `off`/`addr` alignment against and that `sysconf(_SC_PAGE_SIZE)` /
// `getpagesize()` report — is the WebAssembly linear-memory page, 64 KiB
// (`<__macro_PAGESIZE.h>`: `#define PAGESIZE 0x10000`). This is DISTINCT from
// WASIX_MMAN_PAGE_SIZE (4096), which is purely the allocator-side alignment of
// the header-prefix page (an implementation detail of the malloc-backed
// emulation, chosen for Blink's x86 page-table packing — see the header note
// above). The two must not be conflated:
//   - WASIX_MMAN_SYS_PAGE_SIZE governs POSIX *semantics* — the offset-multiple
//     EINVAL (mmap/11-1), the MAP_FIXED addr-alignment EINVAL (mmap/9-1), and
//     the partial-page zero-fill extent past EOF (mmap/11-4/5/6). These are the
//     values the conformance tests query via sysconf(_SC_PAGE_SIZE) and that a
//     Linux program sees.
//   - WASIX_MMAN_PAGE_SIZE governs the *backing allocation* layout only.
// Blink's wasm64 linear mapping is itself 64 KiB-granular (map.c:
// FLAG_pagesize = sysconf(_SC_PAGESIZE) = 0x10000, and ReserveVirtual rejects a
// non-FLAG_pagesize-aligned `virt`), so every MAP_FIXED address Blink hands us
// is already 64 KiB-aligned — the #796 path passes the new alignment gate by
// construction and is not regressed.
#define WASIX_MMAN_SYS_PAGE_SIZE ((size_t)PAGESIZE)

struct map {
    int prot;
    int flags;
    off_t offset;
    size_t length;     // user-requested mapping length (what munmap() must match)
    size_t body_len;   // allocated user-visible body = round_up(length, sys page)
    int fd;
};

// firebox#796: MAP_FIXED support for blink's wasm64 linear mapping.
//
// Blink built for wasm64 has CAN_64BIT=1 → HasLinearMapping() true → it places
// guest memory at host offset == guest VA (ToHost(va)=va+kSkew, kSkew==0) via
// mmap(addr, ..., MAP_FIXED). The malloc/aligned_alloc path below cannot honor a
// fixed address, so a fixed request is handled specially: grow the wasm linear
// memory to cover [addr, addr+len), fill it (file pread or zero), and return
// addr. This makes the guest's emulated address space coincide with linear-
// memory offsets — the whole point of the wasm64 port (collapses the software
// MMU). NOTE (#796 PoC): correctness for arbitrary workloads still requires
// blink's own malloc heap to be disjoint from the guest vaspace (a layout
// redesign — see task 796); small static ELFs whose fixed segments fall in
// otherwise-unused linear memory work today. munmap of a fixed mapping is a
// no-op (wasm memory can't shrink; blink tracks guest maps itself). A small
// registry records fixed ranges so munmap/msync distinguish them from the
// malloc-backed mappings (whose header lives one page below the user addr).
#define WASIX_MMAN_FIXED_MAX 4096
static struct { uintptr_t addr; size_t len; } g_fixed_maps[WASIX_MMAN_FIXED_MAX];
static int g_fixed_count;

#if !defined(__wasm64__)
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
#define WASIX_MMAN_WINDOW_MAX 4096
static struct { uintptr_t addr; int fd; } g_window_maps[WASIX_MMAN_WINDOW_MAX];
static int g_window_count;

static void wasix_window_register(uintptr_t addr, int fd) {
    if (g_window_count < WASIX_MMAN_WINDOW_MAX) {
        g_window_maps[g_window_count].addr = addr;
        g_window_maps[g_window_count].fd = fd;
        g_window_count++;
    } else {
        // Registry full — cannot track the held fd for later close, so don't
        // leak it. The inode-pin is best-effort; drop it rather than leak.
        close(fd);
    }
}

// Close + drop the held /dev/shm fd for a window mapping at `addr` (releasing the
// inode-lifetime pin). No-op if `addr` was not a window mapping we pinned.
static void wasix_window_release(uintptr_t addr) {
    for (int i = 0; i < g_window_count; i++) {
        if (g_window_maps[i].addr == addr) {
            const int fd = g_window_maps[i].fd;
            g_window_maps[i] = g_window_maps[--g_window_count];
            if (fd >= 0) close(fd);
            return;
        }
    }
}
#endif

static int wasix_fixed_is_registered(uintptr_t addr) {
    for (int i = 0; i < g_fixed_count; i++) {
        if (g_fixed_maps[i].addr == addr) return 1;
    }
    return 0;
}

static int wasix_fixed_unregister(uintptr_t addr) {
    for (int i = 0; i < g_fixed_count; i++) {
        if (g_fixed_maps[i].addr == addr) {
            g_fixed_maps[i] = g_fixed_maps[--g_fixed_count];
            return 1;
        }
    }
    return 0;
}

static void *wasix_mmap_fixed(void *addr, size_t length, int flags, int fd,
                              off_t offset) {
    uintptr_t target = (uintptr_t)addr;
    size_t need;
    if (__builtin_add_overflow(target, length, &need)) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    // Grow the single wasm linear memory to cover [target, target+length).
    size_t have = (size_t)__builtin_wasm_memory_size(0) * (size_t)65536;
    if (need > have) {
        size_t grow_pages = (need - have + 65535) / 65536;
        if (__builtin_wasm_memory_grow(0, grow_pages) == (size_t)-1) {
            errno = ENOMEM;
            return MAP_FAILED;
        }
    }
    if ((flags & MAP_ANON) == 0) {
        char *body = (char *)addr;
        size_t rem = length;
        off_t off = offset;
        while (rem > 0) {
            const ssize_t n = pread(fd, body, rem, off);
            if (n < 0) {
                if (errno == EINTR) continue;
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
        memset(addr, 0, length);
    }
    if (!wasix_fixed_is_registered(target) && g_fixed_count < WASIX_MMAN_FIXED_MAX) {
        g_fixed_maps[g_fixed_count].addr = target;
        g_fixed_maps[g_fixed_count].len = length;
        g_fixed_count++;
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
        errno = (int)error;
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

void *mmap(void *addr, size_t length, int prot, int flags,
           int fd, off_t offset) {
    // firebox#796: a fixed-address request (blink's wasm64 linear mapping) is
    // placed AT addr rather than malloc'd. Blink's MAP_FIXED maps to the host's
    // MAP_FIXED (0x10); its MAP_DEMAND ("place here but don't clobber existing")
    // maps to MAP_FIXED_NOREPLACE (0x100000) — both mean "the mapping MUST land
    // at addr", which is exactly the linear-mapping requirement. (We don't honor
    // the NOREPLACE "fail if occupied" semantics for the PoC; blink's loader
    // targets free guest VA.) PROT_EXEC is allowed (blink maps executable guest
    // segments PROT_READ|PROT_EXEC; on wasm the page is data either way — the JIT
    // executes via the host, not guest page perms).
    int fixed_req = (flags & MAP_FIXED) != 0;
#ifdef MAP_FIXED_NOREPLACE
    fixed_req = fixed_req || (flags & MAP_FIXED_NOREPLACE) != 0;
#endif
    if (fixed_req && addr != NULL && length != 0) {
        // firebox#7C5: POSIX mmap(2) — "If MAP_FIXED is set ... and addr is not
        // a multiple of the page size ... mmap() shall fail [EINVAL]"
        // (mmap/9-1). The page size is the system page (64 KiB), not the 4096
        // allocator-prefix granularity. Blink's wasm64 linear mapping always
        // passes 64 KiB-aligned addresses (FLAG_pagesize == 0x10000), so the
        // #796 path is unaffected — this rejects only the genuinely-illegal
        // unaligned fixed request a portable program would also be denied on
        // Linux.
        if (((uintptr_t)addr & (WASIX_MMAN_SYS_PAGE_SIZE - 1)) != 0) {
            errno = EINVAL;
            return MAP_FAILED;
        }
        return wasix_mmap_fixed(addr, length, flags, fd, offset);
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

    // firebox#7C5: POSIX mmap(2) — `off` must be a multiple of the page size;
    // otherwise EINVAL (mmap/11-1). Mirrors the musl top-half OFF_MASK check
    // (the unused-on-wasm path) and the Linux do_mmap() `offset & ~PAGE_MASK`
    // gate, against the *system* page size (64 KiB), which is what
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

    // firebox#61X (Stage-1 1b-libc): a MAP_SHARED mapping of a /dev/shm object
    // is a cross-process shared-memory region. Route it to the host shared
    // window — raw stores/loads are then coherent across processes (fork/16-1)
    // and musl's named semaphores share their byte region — instead of the
    // private aligned_alloc+pread emulation below. Gated tightly so the legacy
    // path keeps every case the window must NOT take over:
    //   * MAP_SHARED only — a /dev/shm object mmap'd MAP_PRIVATE (e.g.
    //     fork/16-1's second object) must stay a private copy.
    //   * /dev/shm objects only (__wasix_is_shm_inode, keyed on the stable
    //     st_ino) — a regular file's MAP_SHARED needs write-back to its backing
    //     file, which the anonymous host window does NOT provide; regular files
    //     keep the legacy path (the separate, still-open #7C5 writeback gap).
    //   * offset 0 only — the host maps the object from its start; a non-zero
    //     offset into a windowed object is the §7.4 segment-sizing follow-up, so
    //     it falls through to the legacy path rather than mis-map.
    // Any host failure (no window reserved: wasm64 / non-Static heap / the
    // browser js/v8 backends, the §4-B3 known-gap; or an OS map error) falls
    // through to the legacy emulation below — never silently wrong.
    if ((flags & MAP_SHARED) != 0 && (flags & MAP_ANON) == 0 && fd >= 0 &&
        offset == 0) {
        struct stat shm_st;
        if (fstat(fd, &shm_st) == 0 && __wasix_is_shm_inode(shm_st.st_ino)) {
            uint64_t shm_off = 0;
            uint32_t shm_created = 0;
            if (__wasilibc_shm_map((uint64_t)shm_st.st_ino, (uint64_t)length,
                                   &shm_off, &shm_created) == 0) {
                // firebox#V12 gap #1: the host window is a FRESH zero-filled OS
                // object; the fd's current bytes (e.g. the `value` musl write()s
                // into a named sem before mmap, or the byte mmap/1-2 write()s)
                // are NOT in it. Seed the window from the fd — but ONLY on the
                // first map of this inode (shm_created). A later mapper (a
                // proc_fork child re-aliasing the window, or a sibling sem_open of
                // the same name) inherits the LIVE shared value and must NOT
                // re-seed, or it would clobber another process's mutation.
                //
                // CRITICAL — the seed MUST reach the window via RAW STORES, not a
                // syscall write and not memcpy. The window lives ABOVE the
                // grow-capped memory size (SHM_BASE), in the wasm32 Static-heap
                // elided-bounds region: only Cranelift-elided raw loads/stores
                // reach it (this is exactly why fork/16-1's raw MAP_SHARED store
                // works). A pread() straight into the window is a HOST memory
                // write bounds-checked against current_length (capped at SHM_BASE)
                // -> HeapOutOfBounds -> the window stays zero. And memcpy() lowers
                // to `memory.copy` (built -mbulk-memory), which bounds-TRAPS past
                // memory.size. So: pread into a HEAP/stack bounce buffer (below
                // SHM_BASE — bounds-OK), then copy into the window with a volatile
                // byte-store loop (individual i32.store8, never coalesced into
                // memory.copy — the one form guaranteed to be Cranelift-elided).
                if (shm_created) {
                    volatile unsigned char *win =
                        (volatile unsigned char *)(uintptr_t)shm_off;
                    unsigned char seedbuf[512];
                    size_t done = 0;
                    while (done < length) {
                        size_t want = length - done;
                        if (want > sizeof seedbuf) want = sizeof seedbuf;
                        const ssize_t nread = pread(fd, seedbuf, want, (off_t)done);
                        if (nread < 0) {
                            if (errno == EINTR) continue;
                            // Best-effort seed: the window is already mapped; a
                            // seed failure leaves the remainder zero (the OS object
                            // is zero-filled) rather than failing the map.
                            break;
                        }
                        if (nread == 0) break;   // short file — remainder stays zero
                        for (ssize_t i = 0; i < nread; i++)
                            win[done + (size_t)i] = seedbuf[i];   // raw i32.store8 -> window
                        done += (size_t)nread;
                    }
                }
                // firebox#V12 gap #3 (inode-lifetime pinning): dup + HOLD the fd so
                // the /dev/shm inode stays referenced as long as this window mapping
                // lives — exactly as a real mmap(MAP_SHARED, fd) keeps the file's
                // inode alive after the fd is closed. Without this the VFS recycles
                // the st_ino on the next create, defeating musl sem_open's dedup
                // (sem_unlink/6-1). Released in munmap() below. Best-effort: a dup
                // failure just forgoes the pin (falls back to the recyclable state).
                // wasm32 only — the window (and its registry) does not exist on
                // wasm64, where shm_map returned Inval and we never reach here.
#if !defined(__wasm64__)
                {
                    const int held = dup(fd);
                    if (held >= 0)
                        wasix_window_register((uintptr_t)shm_off, held);
                }
#endif
                return (void *)(uintptr_t)shm_off;
            }
            // else: fall through to the private emulation below.
        }
    }

    // firebox#7C5: POSIX mmap(2) maps WHOLE pages. "The system shall always
    // zero-fill any partial page at the end of an object" (mmap/11-4/5/6): when
    // `length` is not a multiple of the page size, the bytes from the file/object
    // end up to the end of the last mapped page are accessible and read as zero.
    // The malloc-backed body must therefore span round_up(length, sys page), not
    // just `length`, or a conformant program reading those trailing bytes runs
    // off the end of the allocation (a heap OOB). We record this rounded body
    // length so munmap()/msync() also operate on the true mapped extent.
    size_t body_len = length;
    {
        const size_t mask = WASIX_MMAN_SYS_PAGE_SIZE - 1;
        if ((body_len & mask) != 0) {
            size_t rounded;
            if (__builtin_add_overflow(body_len, WASIX_MMAN_SYS_PAGE_SIZE - (body_len & mask),
                                       &rounded)) {
                errno = ENOMEM;
                return MAP_FAILED;
            }
            body_len = rounded;
        }
    }

    // Compute allocation size: rounded body plus one full prefix page for the
    // header. Overflow-check before passing to aligned_alloc.
    size_t buf_len = 0;
    if (__builtin_add_overflow(body_len, WASIX_MMAN_PAGE_SIZE, &buf_len)) {
        errno = ENOMEM;
        return MAP_FAILED;
    }

    // Allocate page-aligned backing memory. The returned base IS the
    // header location and (base + WASIX_MMAN_PAGE_SIZE) is the user-
    // visible page-aligned mapping.
    void *base = aligned_alloc(WASIX_MMAN_PAGE_SIZE, buf_len);
    if (!base) {
        errno = ENOMEM;
        return MAP_FAILED;
    }

    // Initialize the header at the front of the prefix page.
    struct map *map = (struct map *)base;
    map->prot = prot;
    map->flags = flags;
    map->offset = offset;
    map->length = length;
    map->body_len = body_len;

    // User-visible mapping starts one page past the header. Page-aligned
    // by construction (base is page-aligned per aligned_alloc contract).
    addr = (char *)base + WASIX_MMAN_PAGE_SIZE;

    // firebox#7C5: zero the ENTIRE body up front (aligned_alloc does not zero).
    // This guarantees the partial-page tail [filelen, body_len) reads as zero
    // for a file-backed mapping (11-4/5/6) and that an ANON mapping is fully
    // zero — the subsequent pread overwrites only the bytes actually present in
    // the file, leaving the tail zero-filled.
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

        // Read at most `length` bytes (the user-requested extent); anything past
        // the file end remains zero from the memset. A short file (pread returns
        // 0 before `length` is exhausted) leaves the remainder zero too.
        char *body = (char *)addr;
        size_t to_read = length;
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

    return addr;
}

int munmap(void *addr, size_t length) {
    // firebox#796: a fixed mapping (blink wasm64 linear mapping) has no
    // malloc backing — the wasm linear memory can't shrink, and blink tracks
    // the guest's maps itself, so just drop the registry entry and succeed.
    if (wasix_fixed_unregister((uintptr_t)addr)) {
        return 0;
    }

#if !defined(__wasm64__)
    // firebox#61X: a window mapping (addr >= SHM_BASE, above the grow-capped
    // heap) has no malloc header — the host owns the segment via the inode
    // registry — so guest munmap is a successful no-op, like a fixed mapping.
    // MUST precede the standard EINVAL precondition below, which rejects
    // addr >= memory_size (the window lives above the grow-capped memory size)
    // and would otherwise mis-reject a genuine window unmap.
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
        // firebox#V12 gap #3: release the held /dev/shm inode-lifetime pin for
        // this window mapping (see wasix_window_register). A no-op if none held.
        wasix_window_release(wa);
        return 0;
    }
#endif

    // firebox#TTB: POSIX munmap EINVAL preconditions, validated BEFORE we
    // recover and dereference the header one page below addr — otherwise an
    // invalid addr (e.g. (void *)-1 in munmap/8-1) computes a wild `base` and
    // the `map->length` read traps with an out-of-bounds memory access instead
    // of returning EINVAL. POSIX: addr must be a multiple of the page size, len
    // must be non-zero, and [addr, addr+len) must lie within the process
    // address space.
    uintptr_t a = (uintptr_t)addr;
    size_t mem_bytes = (size_t)__builtin_wasm_memory_size(0) * (size_t)65536;
    if (length == 0 ||                                  // empty range
        (a & (WASIX_MMAN_PAGE_SIZE - 1)) != 0 ||        // addr not page-aligned
        a < WASIX_MMAN_PAGE_SIZE ||                     // no room for the header
        a >= mem_bytes ||                               // addr past linear memory
        length > mem_bytes - a) {                       // range escapes memory
        errno = EINVAL;
        return -1;
    }

    // Recover the header that lives one page below the user address.
    // Symmetric to the mmap return: addr == base + WASIX_MMAN_PAGE_SIZE.
    void *base = (char *)addr - WASIX_MMAN_PAGE_SIZE;
    struct map *map = (struct map *)base;

    // We don't support partial munmapping.
    if (map->length != length) {
        errno = EINVAL;
        return -1;
    }

    // Write the data back to the backing file and close the file handle.
    //
    // firebox#7C5: a MAP_PRIVATE mapping is COPY-ON-WRITE — POSIX mandates that
    // modifications through a MAP_PRIVATE mapping are NEVER carried through to
    // the underlying file (mmap/7-2, munmap/4-1). The pre-#7C5 code flushed any
    // PROT_WRITE file-backed mapping on unmap, leaking private edits into the
    // file. Gate the writeback on MAP_SHARED (i.e. NOT MAP_PRIVATE): only a
    // shared, writable mapping syncs back. msync() carries the same guard, so
    // even though this calls msync() the gate is enforced in both places.
    if (map->fd > 0) {
        if ((map->prot & PROT_WRITE) != 0 &&
            (map->flags & MAP_PRIVATE) == 0) {
            msync(addr, length, MS_SYNC);
        }

        close(map->fd);
    }

    // Release the memory. free() must see the same pointer aligned_alloc
    // returned (== base), not the offset user pointer.
    free(base);

    // Success!
    return 0;
}

int msync (void *addr, size_t length, int flags) {
    // firebox#796: fixed mappings (blink wasm64 linear mapping) have no
    // header and no separate backing file to flush — the bytes already live in
    // linear memory. Treat msync as a no-op success.
    if (wasix_fixed_is_registered((uintptr_t)addr)) {
        return 0;
    }
#if !defined(__wasm64__)
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
    if ((uintptr_t)addr >= WASIX_SHM_WINDOW_BASE) {
        if (((uintptr_t)addr & (WASIX_MMAN_PAGE_SIZE - 1)) != 0) {
            errno = EINVAL;
            return -1;
        }
        return 0;
    }
#endif
    // Header recovery: see munmap() for layout. addr is page-aligned by
    // construction; the header sits exactly one page below it.
    struct map *map = (struct map *)((char *)addr - WASIX_MMAN_PAGE_SIZE);
    size_t map_flags = map->flags;
    off_t map_offset = map->offset;
    size_t map_length = map->length;
    int fd = map->fd;

    if (length > map_length) {
        errno = EINVAL;
        return -1;
    }

    // firebox#467: msync is semantically meaningful for PROT_WRITE
    // mappings — that is the case where the user has written through the
    // mapping and wants those bytes flushed to the backing fd. A
    // PROT_READ-only mapping has nothing to flush; treat as a no-op
    // (return 0) per POSIX (msync on a read-only mapping is permitted
    // and returns 0). The previous check was inverted: it accepted
    // exactly the no-op case and rejected every legitimate caller,
    // silently dropping the explicit-flush idiom every editor / DB /
    // package manager uses. See work/tasks/467-* and
    // crates/firebox-diff differential corpus `file-mmap-write-read`.
    if ((map->prot & PROT_WRITE) == 0) {
        return 0;
    }

    // firebox#7C5: a MAP_PRIVATE mapping is copy-on-write — msync() must NOT
    // flush a private mapping's modifications to the backing file (mmap/7-2,
    // which calls msync(MS_SYNC) explicitly on a MAP_PRIVATE map and then
    // re-reads the file to confirm it was not mutated). POSIX: "If the mapping
    // was made with MAP_PRIVATE, msync() has no effect on the underlying file."
    // The previous code flushed any PROT_WRITE mapping, leaking private writes.
    // A read-only or private mapping is a successful no-op flush.
    if ((map_flags & MAP_PRIVATE) != 0) {
        return 0;
    }

    if ((map_flags & MAP_ANON) == 0) {
        char *body = (char *)addr;

        while (length > 0) {
            const ssize_t nwrite = pwrite(fd, body, length, map_offset);

            if (nwrite > 0) {
                length -= (size_t)nwrite;
                map_offset += (size_t)nwrite;
                body += (size_t)nwrite;
            } else if (errno == EINTR) {
                continue;
            } else {
                return -1;
            }
        }
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
