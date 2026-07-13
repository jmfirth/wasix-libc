#ifndef __wasilibc___macro_PAGESIZE_h
#define __wasilibc___macro_PAGESIZE_h

/*
 * firebox#QAB: PAGESIZE is the POSIX *page size* — the value
 * sysconf(_SC_PAGESIZE) / getpagesize() report and that POSIX mmap(2) defines
 * addr/offset alignment against. Firebox reports 4096, matching real Linux on
 * every target a guest is built for (x86-64, arm64 4 KiB, ...). A program that
 * mmap()s at a 4096-aligned (but not 64 KiB-aligned) address — which Linux
 * accepts — must be accepted by Firebox too; reporting 64 KiB here (as this
 * port historically did, firebox#7C5) is anti-faithful (Invariant 2) and
 * breaks real programs whose 4096-aligned MAP_FIXED addresses Linux honors.
 *
 * This POSIX page size is DISTINCT from the WebAssembly *linear-memory page*
 * (64 KiB), which is the fixed granularity of `memory.grow` / `memory.size`.
 * That value is NOT a POSIX page size; it lives in the consumers that speak to
 * the wasm engine — sbrk's `memory.grow` quantum (libc-bottom-half/sources/
 * sbrk.c), preopens' addressable-bytes bound (preopens.c), and mman's
 * host-page region::protect isolation (mman.c, WASIX_MMAN_HOST_PAGE_SIZE) —
 * each of which carries its own 64 KiB constant so this POSIX-page macro can
 * be faithful without disturbing the wasm-page arithmetic. Changing this macro
 * has ZERO upstream-interop cost (Invariant 8): the wasix import namespace is
 * `memory.grow`@64 KiB, below the libc page abstraction, and upstream artifacts
 * static-link their own libc.
 *
 * PAGESIZE 4096 is within the range of an `int`, so the deprecated
 * `getpagesize()` (which returns `int`) reports it without truncation. POSIX
 * has deprecated `getpagesize` in favor of `sysconf(_SC_PAGESIZE)`.
 */
#define PAGESIZE (0x1000)

#endif
