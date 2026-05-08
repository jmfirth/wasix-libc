// This file is a wrapper around malloc.c, which is the upstream source file.
// It sets configuration flags and controls which symbols are exported.
//
// firebox-trace alloc subsystem instrumentation lives in this file. See the
// block at the bottom (after `#include "malloc.c"`) for the machinery and
// the public wrappers below for the call sites. The Rust counterpart is in
// `crates/firebox-trace/src/lib.rs` in the firebox repo; subsystem IDs MUST
// match. Design: `docs/architecture/firebox-trace.md` (firebox repo).
//
// Activation: FIREBOX_TRACE=alloc (and optionally FIREBOX_TRACE_FILE=/path).
// Disabled-path cost: one atomic load + branch (negligible).

#include <stddef.h>
#include <malloc.h>
#include <time.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

// __WASILIBC_BUILDING_LIBC hides POSIX function declarations from the libc's
// own headers. Forward-declare what we need; the linker finds these in the
// other libc TUs (write.c, open.c) within the same libc.a.
extern long write(int fd, const void *buf, unsigned long count);
extern int open(const char *path, int flags, ...);

// Define configuration macros for dlmalloc.

// WebAssembly doesn't have mmap-style memory allocation.
#define HAVE_MMAP 0

// WebAssembly doesn't support shrinking linear memory.
#define MORECORE_CANNOT_TRIM 1

// Disable sanity checks to reduce code size.
#define ABORT __builtin_unreachable()

// If threads are enabled, enable support for threads.
#ifdef _REENTRANT
#define USE_LOCKS 1
#endif

// Make malloc deterministic.
#define LACKS_TIME_H 1

// Disable malloc statistics generation to reduce code size.
#define NO_MALLINFO 1
#define NO_MALLOC_STATS 1

// Align malloc regions to 16, to avoid unaligned SIMD accesses.
#define MALLOC_ALIGNMENT 16

// Declare errno values used by dlmalloc. We define them like this to avoid
// putting specific errno values in the ABI.
extern const int __ENOMEM;
#define ENOMEM __ENOMEM
extern const int __EINVAL;
#define EINVAL __EINVAL

// Define USE_DL_PREFIX so that we leave dlmalloc's names prefixed with 'dl'.
// We define them as "static", and we wrap them with public names below. This
// serves two purposes:
//
// One is to make it easy to control which symbols are exported; dlmalloc
// defines several non-standard functions and we wish to explicitly control
// which functions are part of our public-facing interface.
//
// The other is to protect against compilers optimizing based on the assumption
// that they know what functions with names like "malloc" do. Code in the
// implementation will call functions like "dlmalloc" and assume it can use
// the resulting pointers to access the metadata outside of the nominally
// allocated objects. However, if the function were named "malloc", compilers
// might see code like that and assume it has undefined behavior and can be
// optimized away. By using "dlmalloc" in the implementation, we don't need
// -fno-builtin to avoid this problem.
#define USE_DL_PREFIX 1
#define DLMALLOC_EXPORT static inline

// This isn't declared with DLMALLOC_EXPORT so make it static explicitly.
static size_t dlmalloc_usable_size(void*);

// Include the upstream dlmalloc's malloc.c.
#include "malloc.c"

// =============================================================================
// firebox-trace alloc subsystem.
//
// Compile-time gated: when FIREBOX_TRACE_ENABLED is undefined (the default
// for production wasix-libc builds), the entire trace machinery — env-var
// parsing, fd setup, format buffer, write call, and re-entrancy guard —
// compiles out completely and the FIREBOX_TRACE() macro becomes a no-op.
// libc.a in default builds carries zero allocator-instrumentation cost.
//
// Enable with `bash scripts/wasix-libc/build.sh --pic --trace` (which
// adds -DFIREBOX_TRACE_ENABLED to CFLAGS) for forensic builds. Subsystem
// IDs and the FIREBOX_TRACE macro are mirrored on the Rust side at
// `crates/firebox-trace/src/lib.rs` (firebox repo) — both files are read
// by the same FIREBOX_TRACE env var; if the bits drift, filtering breaks.
//
// CRITICAL (when enabled): firebox_trace_emit() must NOT call malloc/free.
// vsnprintf can call malloc internally for wide-format conversions; the
// in_emit guard short-circuits the recursive FIREBOX_TRACE call so we
// don't blow the stack. Output goes through raw write(2) which is atomic
// per-call and bypasses stdio buffering (which would itself malloc).
// =============================================================================

#ifdef FIREBOX_TRACE_ENABLED

#define FIREBOX_TRACE_TRAP       (1u << 0)
#define FIREBOX_TRACE_ALLOC      (1u << 1)
#define FIREBOX_TRACE_PANIC      (1u << 2)
#define FIREBOX_TRACE_SYSCALL    (1u << 3)
#define FIREBOX_TRACE_DYNLINK    (1u << 4)
#define FIREBOX_TRACE_THREAD     (1u << 5)
#define FIREBOX_TRACE_PROCMACRO  (1u << 6)
#define FIREBOX_TRACE_VFS        (1u << 7)
#define FIREBOX_TRACE_IMAGE      (1u << 8)
#define FIREBOX_TRACE_CACHE      (1u << 9)

#ifdef _REENTRANT
#define FBX_TLS __thread
#else
#define FBX_TLS  /* single-threaded build — no thread-local needed */
#endif

static unsigned int firebox_trace_mask = FIREBOX_TRACE_TRAP;
static int firebox_trace_fd = 2; // stderr
static unsigned long long firebox_trace_start_ns = 0;
static FBX_TLS int firebox_trace_in_emit = 0;
static FBX_TLS unsigned int firebox_trace_tid = 0;
static unsigned int firebox_trace_next_tid = 1;

static unsigned long long firebox_trace_now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (unsigned long long)ts.tv_sec * 1000000000ull
         + (unsigned long long)ts.tv_nsec;
}

static const char* firebox_trace_subsys_name(unsigned int subsys) {
    switch (subsys) {
        case FIREBOX_TRACE_TRAP:      return "trap";
        case FIREBOX_TRACE_ALLOC:     return "alloc";
        case FIREBOX_TRACE_PANIC:     return "panic";
        case FIREBOX_TRACE_SYSCALL:   return "syscall";
        case FIREBOX_TRACE_DYNLINK:   return "dynlink";
        case FIREBOX_TRACE_THREAD:    return "thread";
        case FIREBOX_TRACE_PROCMACRO: return "procmacro";
        case FIREBOX_TRACE_VFS:       return "vfs";
        case FIREBOX_TRACE_IMAGE:     return "image";
        case FIREBOX_TRACE_CACHE:     return "cache";
        default:                      return "unknown";
    }
}

static unsigned int firebox_trace_parse_token(const char* tok, size_t len) {
    #define M(name, bit) \
        if (len == sizeof(name)-1 && memcmp(tok, name, len) == 0) return (bit)
    M("trap",      FIREBOX_TRACE_TRAP);
    M("alloc",     FIREBOX_TRACE_ALLOC);
    M("panic",     FIREBOX_TRACE_PANIC);
    M("syscall",   FIREBOX_TRACE_SYSCALL);
    M("dynlink",   FIREBOX_TRACE_DYNLINK);
    M("thread",    FIREBOX_TRACE_THREAD);
    M("procmacro", FIREBOX_TRACE_PROCMACRO);
    M("vfs",       FIREBOX_TRACE_VFS);
    M("image",     FIREBOX_TRACE_IMAGE);
    M("cache",     FIREBOX_TRACE_CACHE);
    #undef M
    return 0;
}

__attribute__((constructor))
static void firebox_trace_init(void) {
    firebox_trace_start_ns = firebox_trace_now_ns();

    const char* env = getenv("FIREBOX_TRACE");
    if (env) {
        unsigned int mask = FIREBOX_TRACE_TRAP;
        const char* p = env;
        while (*p) {
            const char* end = p;
            while (*end && *end != ',') end++;
            mask |= firebox_trace_parse_token(p, (size_t)(end - p));
            p = (*end) ? end + 1 : end;
        }
        firebox_trace_mask = mask;
    }

    const char* path = getenv("FIREBOX_TRACE_FILE");
    if (path && *path) {
        int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) firebox_trace_fd = fd;
    }
}

static unsigned int firebox_trace_my_tid(void) {
    if (firebox_trace_tid == 0) {
        firebox_trace_tid = __atomic_fetch_add(&firebox_trace_next_tid, 1,
                                                __ATOMIC_RELAXED);
    }
    return firebox_trace_tid;
}

static void firebox_trace_emit(unsigned int subsys, const char* component,
                                const char* fmt, ...) {
    if (firebox_trace_in_emit) return;
    firebox_trace_in_emit = 1;

    char buf[512];
    unsigned long long now = firebox_trace_now_ns() - firebox_trace_start_ns;
    unsigned long long secs = now / 1000000000ull;
    unsigned long us = (unsigned long)((now / 1000ull) % 1000000ull);

    int n = snprintf(buf, sizeof(buf),
                     "[firebox-trace %s %s %07llu.%06lu t-%u] ",
                     firebox_trace_subsys_name(subsys), component,
                     secs, us, firebox_trace_my_tid());
    if (n < 0 || (size_t)n >= sizeof(buf)) {
        firebox_trace_in_emit = 0;
        return;
    }

    va_list ap;
    va_start(ap, fmt);
    int body = vsnprintf(buf + n, sizeof(buf) - (size_t)n, fmt, ap);
    va_end(ap);
    if (body < 0) { firebox_trace_in_emit = 0; return; }

    size_t total = (size_t)n + (size_t)body;
    if (total >= sizeof(buf)) total = sizeof(buf) - 1;
    if (total == 0 || buf[total - 1] != '\n') {
        if (total < sizeof(buf)) buf[total++] = '\n';
        else buf[sizeof(buf) - 1] = '\n';
    }

    // Raw write(2): atomic per-call up to PIPE_BUF, no stdio buffering
    // (which would itself malloc and re-enter us).
    (void)write(firebox_trace_fd, buf, total);

    firebox_trace_in_emit = 0;
}

#define FIREBOX_TRACE(subsys, component, ...) do { \
    if (__builtin_expect((firebox_trace_mask & (subsys)) != 0, 0)) { \
        firebox_trace_emit((subsys), (component), __VA_ARGS__); \
    } \
} while (0)

#else  // !FIREBOX_TRACE_ENABLED

// Production build: trace machinery elided.  Subsystem IDs are still defined
// at zero so any consumer that references them outside of FIREBOX_TRACE()
// expressions still compiles cleanly, but the macro itself drops all args
// without evaluation, including any side-effects in format args.  The
// optimizer eliminates the entire call.
#define FIREBOX_TRACE_TRAP       0u
#define FIREBOX_TRACE_ALLOC      0u
#define FIREBOX_TRACE_PANIC      0u
#define FIREBOX_TRACE_SYSCALL    0u
#define FIREBOX_TRACE_DYNLINK    0u
#define FIREBOX_TRACE_THREAD     0u
#define FIREBOX_TRACE_PROCMACRO  0u
#define FIREBOX_TRACE_VFS        0u
#define FIREBOX_TRACE_IMAGE      0u
#define FIREBOX_TRACE_CACHE      0u

#define FIREBOX_TRACE(subsys, component, ...) ((void)0)

#endif  // FIREBOX_TRACE_ENABLED

// =============================================================================
// Public allocator entry points — wrap the static dlmalloc/dlfree/etc and
// emit alloc-subsystem trace lines on every call.
//
// Note: clang's __builtin_return_address(0) is unsupported on non-Emscripten
// wasm32 targets, so we omit the caller PC. The `component` field
// distinguishes which entry point (malloc / free / calloc / etc.) was hit;
// size + ptr correlation against VecCache::drop chunk-corruption events is
// the load-bearing signal for #293 L9.
// =============================================================================

void *malloc(size_t size) {
    void *p = dlmalloc(size);
    FIREBOX_TRACE(FIREBOX_TRACE_ALLOC, "wasix-libc/malloc",
                  "size=%zu ptr=%p", size, p);
    return p;
}

void free(void *ptr) {
    FIREBOX_TRACE(FIREBOX_TRACE_ALLOC, "wasix-libc/free", "ptr=%p", ptr);
    dlfree(ptr);
}

void *calloc(size_t nmemb, size_t size) {
    void *p = dlcalloc(nmemb, size);
    FIREBOX_TRACE(FIREBOX_TRACE_ALLOC, "wasix-libc/calloc",
                  "nmemb=%zu size=%zu ptr=%p", nmemb, size, p);
    return p;
}

void *realloc(void *ptr, size_t size) {
    void *p = dlrealloc(ptr, size);
    FIREBOX_TRACE(FIREBOX_TRACE_ALLOC, "wasix-libc/realloc",
                  "old=%p size=%zu ptr=%p", ptr, size, p);
    return p;
}

int posix_memalign(void **memptr, size_t alignment, size_t size) {
    int rc = dlposix_memalign(memptr, alignment, size);
    FIREBOX_TRACE(FIREBOX_TRACE_ALLOC, "wasix-libc/posix_memalign",
                  "align=%zu size=%zu rc=%d ptr=%p",
                  alignment, size, rc,
                  (rc == 0 && memptr) ? *memptr : (void*)0);
    return rc;
}

void* aligned_alloc(size_t alignment, size_t bytes) {
    void *p = dlmemalign(alignment, bytes);
    FIREBOX_TRACE(FIREBOX_TRACE_ALLOC, "wasix-libc/aligned_alloc",
                  "align=%zu size=%zu ptr=%p", alignment, bytes, p);
    return p;
}

size_t malloc_usable_size(void *ptr) {
    return dlmalloc_usable_size(ptr);
}

// Define these to satisfy musl references.
void *__libc_malloc(size_t) __attribute__((alias("malloc")));
void __libc_free(void *) __attribute__((alias("free")));
void *__libc_calloc(size_t nmemb, size_t size) __attribute__((alias("calloc")));
