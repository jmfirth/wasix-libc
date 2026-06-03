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

// firebox#796: optional ceiling for the malloc arena. Default = no cap.
//
// An embedder that shares the single wasm linear memory between its own malloc
// heap AND a separately-managed high region — e.g. blink's wasm64 linear-mapping
// mode, where the guest's address space occupies linear [kSkew, ...) and is
// grown directly via memory.grow — sets this to confine sbrk below that region.
// Without it, once the high region has grown the memory, sbrk's "fits in
// already-addressable memory" branch (below) would walk brk_ptr straight into
// it. A pure ABI addition: left at UINTPTR_MAX it is a no-op, so other wasm
// programs are unaffected.
uintptr_t __wasilibc_sbrk_max = (uintptr_t)-1;

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

    // firebox#796: refuse to grow the heap past the embedder-set ceiling. This
    // keeps the malloc arena disjoint from a high linear-memory region the
    // embedder owns (blink's kSkew-relocated guest). dlmalloc treats the -1 as
    // OOM and returns NULL for that allocation, rather than corrupting the
    // guest region. No-op when unset (UINTPTR_MAX).
    if ((uintptr_t)new_brk > __wasilibc_sbrk_max) {
        errno = ENOMEM;
        return (void *)-1;
    }

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
