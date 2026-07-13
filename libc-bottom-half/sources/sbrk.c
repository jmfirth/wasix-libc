#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>

// firebox#QAB: the WebAssembly linear-memory page — the FIXED 64 KiB granularity
// of `memory.grow` / `memory.size`, which is NOT the POSIX page size. sbrk grows
// the heap by calling `__builtin_wasm_memory_grow` (which counts 64 KiB wasm
// pages) and converts page counts to byte addresses via
// `memory_size * WASM_PAGE_SIZE`. This is deliberately decoupled from PAGESIZE
// (the POSIX page, 4096 since #QAB — see <__macro_PAGESIZE.h>): the wasm engine
// only ever grows memory in 64 KiB units, so using the 4096 POSIX page here would
// under-count every grow and mis-compute every byte offset by 16×. dlmalloc's own
// MORECORE granularity is the POSIX page (4096) and is unaffected — sbrk accepts
// any increment and rounds only the actual memory.grow up to this wasm page.
#define WASM_PAGE_SIZE ((uintptr_t)0x10000)

// Linker-defined: marks the start of the heap region in linear memory.
// wasm-ld synthesizes this symbol pointing past the data section. For both
// growable and fixed-size memory layouts, [&__heap_base, memory_size * WASM_PAGE_SIZE)
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

// firebox#853: end of the region sbrk OWNS — the addressable memory captured
// at first call, plus pages sbrk itself obtained via memory.grow. sbrk must
// never walk brk_ptr into pages it did not obtain: the HOST also calls
// memory.grow at runtime (e.g. the wasmer dynamic linker reserving
// per-side-module TLS arenas at dlopen, or side-module memory regions), and
// those pages are NOT heap. The previous "fits in addressable memory" check
// (`new_brk <= memory.size * WASM_PAGE_SIZE`) walked brk_ptr straight through such
// foreign regions, so dlmalloc chunks overlapped the dynamic linker's TLS
// arenas — every thread-spawn's TLS-image write then clobbered live heap
// (the firebox#852/#853 in-guest rustc/icu4x corruption: garbage vtable
// funcrefs, "len is 0" metadata decodes, OOB traps). This is the dynamic
// generalization of the #796 static ceiling (`__wasilibc_sbrk_max` confines
// sbrk below ONE region known at startup; `owned_end` confines it to regions
// sbrk provably obtained, whenever foreign grows happen). On Linux, brk()
// never hands out mmap'd ranges because the kernel arbitrates both
// allocators; in wasm the host's grows are invisible to the guest, so sbrk
// must self-limit and go DISCONTIGUOUS past foreign regions (dlmalloc
// supports non-contiguous MORECORE via add_segment).
static uintptr_t owned_end = 0;

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
// Firebox patch (wasix-libc 0021, ownership model since firebox#853): walks a
// static brk_ptr through the region sbrk OWNS (initial memory captured at
// first call + pages obtained by sbrk's own memory.grow calls) instead of
// through all addressable memory. memory.grow is invoked when brk_ptr would
// exceed the owned region. The original 0021 motivation is preserved: for
// binaries declaring fixed-size shared memory (`(memory N N shared)`),
// memory.grow always fails, and the initial owned region [&__heap_base,
// memory.size * WASM_PAGE_SIZE) — captured before anything else can grow — is what
// makes malloc work there at all.
void *sbrk(intptr_t increment) {
    // Lazy init: first call sets brk to __heap_base and captures the initial
    // addressable region as sbrk-owned. This runs from libc startup
    // (preopen-init constructor's first malloc), before any dlopen or other
    // runtime host-side grow can have happened, so everything addressable at
    // this point is link/load-time layout: data, stacks, and the initial
    // heap arena.
    if (brk_ptr == NULL) {
        brk_ptr = &__heap_base;
        owned_end = __builtin_wasm_memory_size(0) * WASM_PAGE_SIZE;
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
    // The upstream sbrk aborted on non-page-aligned increments because its
    // brk-from-memory.grow model required them. Our static-brk model only
    // needs page alignment when actually invoking memory.grow (computed via
    // `pages_needed`), not for the brk_ptr advance itself, so non-page-aligned
    // increments are now permitted.

    unsigned char *new_brk = brk_ptr + increment;

    // firebox#842: detect 32-bit pointer overflow at the wasm32 4 GiB ceiling.
    // `increment > 0` here (the <= 0 cases returned above), so a `new_brk` that
    // lands BELOW `brk_ptr` can only mean the add wrapped past 2^32. Without this
    // guard the wrapped (small) `new_brk` slips under BOTH the `__wasilibc_sbrk_max`
    // check and the owned-region fast-path below, so sbrk hands dlmalloc a chunk
    // that straddles 2^32 — dlmalloc then writes chunk metadata / the caller
    // writes payload past the addressable end → "out of bounds memory access" in
    // dlmalloc + low-memory corruption, instead of a clean OOM. This is the
    // mechanism behind firebox#674's "rustc.wasm dlmalloc OOB at the 4 GiB
    // ceiling" (heavy compiles — e.g. cryptography/ICU4X — that genuinely reach
    // 4 GiB crash mid-build instead of failing gracefully; the durable
    // compile-it answer is wasm64, but on wasm32 the failure must at least be a
    // clean OOM). Returning -1 lets dlmalloc report allocation failure normally.
    // On memory64 `brk_ptr` is 64-bit and never reaches 2^64, so this is a
    // harmless no-op there.
    if ((uintptr_t)new_brk < (uintptr_t)brk_ptr) {
        errno = ENOMEM;
        return (void *)-1;
    }

    // firebox#796: refuse to grow the heap past the embedder-set ceiling. This
    // keeps the malloc arena disjoint from a high linear-memory region the
    // embedder owns (blink's kSkew-relocated guest). dlmalloc treats the -1 as
    // OOM and returns NULL for that allocation, rather than corrupting the
    // guest region. No-op when unset (UINTPTR_MAX).
    if ((uintptr_t)new_brk > __wasilibc_sbrk_max) {
        errno = ENOMEM;
        return (void *)-1;
    }

    // firebox#853: only walk within the sbrk-owned region — NOT all
    // addressable memory (foreign host-side grows are not heap; see
    // `owned_end` above).
    if ((uintptr_t)new_brk <= owned_end) {
        unsigned char *old_brk = brk_ptr;
        brk_ptr = new_brk;
        return old_brk;
    }

    // Need more pages. Grow enough to cover the shortfall past the owned
    // region.
    uintptr_t shortfall = (uintptr_t)new_brk - owned_end;
    uintptr_t pages_needed = (shortfall + WASM_PAGE_SIZE - 1) / WASM_PAGE_SIZE;
    uintptr_t old_pages = __builtin_wasm_memory_grow(0, pages_needed);
    if (old_pages == SIZE_MAX) {
        // Growth failed — fixed-size memory or genuinely out of memory.
        errno = ENOMEM;
        return (void *)-1;
    }
    uintptr_t grow_base = old_pages * WASM_PAGE_SIZE;

    if (grow_base == owned_end) {
        // Contiguous with the owned region (the common case: nobody else grew
        // since our last grow). Extend ownership and advance.
        owned_end = grow_base + pages_needed * WASM_PAGE_SIZE;
        unsigned char *old_brk = brk_ptr;
        brk_ptr = new_brk;
        return old_brk;
    }

    // firebox#853: a foreign grow (dynamic linker / embedder) happened since
    // we last extended — `[owned_end, grow_base)` belongs to someone else and
    // must NOT be handed to dlmalloc. Start a fresh, discontiguous heap
    // region at `grow_base` (dlmalloc treats the jump as a new segment). The
    // tail of the old region `[brk_ptr, owned_end)` is abandoned (at most a
    // page's worth on top of dlmalloc's own request rounding — dlmalloc never
    // assumes it owns memory sbrk didn't return).
    uintptr_t fresh_have = pages_needed * WASM_PAGE_SIZE;
    if ((uintptr_t)increment > fresh_have) {
        // The fresh region must hold the WHOLE increment by itself (the old
        // region's tail can't be combined across the foreign hole). Grow the
        // difference; require contiguity with our fresh region.
        uintptr_t more = ((uintptr_t)increment - fresh_have + WASM_PAGE_SIZE - 1) / WASM_PAGE_SIZE;
        uintptr_t op2 = __builtin_wasm_memory_grow(0, more);
        if (op2 == SIZE_MAX || op2 * WASM_PAGE_SIZE != grow_base + fresh_have) {
            // Out of memory, or yet another foreign grow interleaved between
            // our two grows (pathological). Fail this allocation cleanly —
            // dlmalloc reports OOM / retries; nothing is corrupted. The
            // already-grown pages are abandoned (zero-filled, demand-paged).
            errno = ENOMEM;
            return (void *)-1;
        }
        fresh_have += more * WASM_PAGE_SIZE;
    }
    brk_ptr = (unsigned char *)(grow_base + (uintptr_t)increment);
    owned_end = grow_base + fresh_have;
    return (void *)grow_base;
}
