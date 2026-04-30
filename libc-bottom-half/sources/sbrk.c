#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <__macro_PAGESIZE.h>

// Linker-defined: marks the start of the heap region in linear memory.
// wasm-ld synthesizes this symbol pointing past the data section. For both
// growable and fixed-size memory layouts, [&__heap_base, memory_size * PAGESIZE)
// is the addressable region available for malloc to manage.
extern unsigned char __heap_base;

// Static break pointer. Initialized lazily on first sbrk call to &__heap_base.
//
// Thread safety: dlmalloc serializes its own sbrk() calls inside its allocator
// lock, so this static is safe in the threaded build for the
// allocator-internal call path. Direct C-level callers that hand out sbrk(0)
// concurrently with dlmalloc-driven sbrk(N) would race; that's the same
// contract upstream wasi-libc had (no thread safety on bare sbrk).
static unsigned char *brk_ptr = NULL;

// Bare-bones implementation of sbrk.
//
// Firebox patch (wasix-libc 0021): walks a static brk_ptr through linear memory
// instead of unconditionally calling memory.grow. memory.grow is now only
// invoked as a fallback when brk_ptr would exceed the currently addressable
// memory.size. This makes malloc work for binaries declaring fixed-size shared
// memory (`(memory N N shared)`), where memory.grow always fails and the
// upstream sbrk implementation makes every malloc return NULL — fatal for any
// C code that allocates, including the wasi-libc preopen-init constructor.
void *sbrk(intptr_t increment) {
    // Lazy init: first call sets brk to __heap_base.
    if (brk_ptr == NULL) {
        brk_ptr = &__heap_base;
    }

    // sbrk(0) returns the current break pointer.
    if (increment == 0) {
        return brk_ptr;
    }

    // We don't support shrinking. dlmalloc's failure-recovery paths
    // (malloc.c:4146 etc.) call MORECORE(-N) to release a partial allocation;
    // returning -1 with errno=ENOMEM is the correct response — dlmalloc treats
    // it as "couldn't release" and continues without releasing.
    if (increment < 0) {
        errno = ENOMEM;
        return (void *)-1;
    }

    // Note vs upstream: dlmalloc's sys_alloc (malloc.c:4109-4119) calls sbrk(0)
    // then page-aligns and may call sbrk with a non-page-aligned increment.
    // The upstream sbrk aborted on non-PAGESIZE increments because its
    // brk-from-memory.grow model required them. Our static-brk model only
    // needs page alignment when actually invoking memory.grow (computed via
    // `pages_needed`), not for the brk_ptr advance itself, so non-page-aligned
    // increments are now permitted.

    // Compute the new break and check whether it fits in already-addressable
    // memory.
    uintptr_t mem_end = __builtin_wasm_memory_size(0) * PAGESIZE;
    unsigned char *new_brk = brk_ptr + increment;
    if ((uintptr_t)new_brk <= mem_end) {
        // Fits — just advance brk_ptr and return the previous break.
        unsigned char *old_brk = brk_ptr;
        brk_ptr = new_brk;
        return old_brk;
    }

    // Doesn't fit — try to grow memory enough to cover the requested increment.
    uintptr_t shortfall = (uintptr_t)new_brk - mem_end;
    uintptr_t pages_needed = (shortfall + PAGESIZE - 1) / PAGESIZE;
    uintptr_t old_pages = __builtin_wasm_memory_grow(0, pages_needed);
    if (old_pages == SIZE_MAX) {
        // Growth failed — fixed-size memory or genuinely out of memory.
        errno = ENOMEM;
        return (void *)-1;
    }

    // Growth succeeded — advance brk_ptr.
    unsigned char *old_brk = brk_ptr;
    brk_ptr = new_brk;
    return old_brk;
}
