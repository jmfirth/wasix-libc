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
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>

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

struct map {
    int prot;
    int flags;
    off_t offset;
    size_t length;
    int fd;
};

void *mmap(void *addr, size_t length, int prot, int flags,
           int fd, off_t offset) {
    // Check for unsupported flags.
    if ((flags & (MAP_PRIVATE | MAP_SHARED)) == 0 ||
        (flags & MAP_FIXED) != 0 ||
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

    // Check for unsupported protection requests.
    if (prot == PROT_NONE ||
#ifdef PROT_EXEC
        (prot & PROT_EXEC) != 0 ||
#endif
        0)
    {
        errno = EINVAL;
        return MAP_FAILED;
    }

    //  To be consistent with POSIX.
    if (length == 0) {
        errno = EINVAL;
        return MAP_FAILED;
    }

    // Compute allocation size: user length plus one full prefix page for
    // the header. Overflow-check before passing to aligned_alloc.
    size_t buf_len = 0;
    if (__builtin_add_overflow(length, WASIX_MMAN_PAGE_SIZE, &buf_len)) {
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

    // User-visible mapping starts one page past the header. Page-aligned
    // by construction (base is page-aligned per aligned_alloc contract).
    addr = (char *)base + WASIX_MMAN_PAGE_SIZE;

    // Initialize the main memory buffer, either with the contents of a file,
    // or with zeros.
    if ((flags & MAP_ANON) == 0) {
        int new_fd = dup(fd);

        if (new_fd < 0) {
            errno = EINVAL;
            free(base);

            return NULL;
        }

        map->fd = new_fd;

        char *body = (char *)addr;
        while (length > 0) {
            const ssize_t nread = pread(fd, body, length, offset);
            if (nread < 0) {
                if (errno == EINTR)
                    continue;
                return MAP_FAILED;
            }
            if (nread == 0)
                break;
            length -= (size_t)nread;
            offset += (size_t)nread;
            body += (size_t)nread;
        }
    } else {
        map->fd = -1;
        memset(addr, 0, length);
    }

    return addr;
}

int munmap(void *addr, size_t length) {
    // Recover the header that lives one page below the user address.
    // Symmetric to the mmap return: addr == base + WASIX_MMAN_PAGE_SIZE.
    void *base = (char *)addr - WASIX_MMAN_PAGE_SIZE;
    struct map *map = (struct map *)base;

    // We don't support partial munmapping.
    if (map->length != length) {
        errno = EINVAL;
        return -1;
    }

    // Write the data back to the backing file and close
    // the file handle
    if (map->fd > 0) {
        if ((map->prot & PROT_WRITE) != 0) {
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
