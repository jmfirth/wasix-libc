/*
 * Firebox #384 — printf instrumentation for musl lock primitives.
 *
 * Why this exists
 * ===============
 * Predecessor task #382 attempted to dump bash's wasm-side stack from
 * the host-side futex_wait probe to identify which musl call site
 * holds the leaked lock. libunwind on aarch64 macOS returned 8 bogus
 * PCs (byte-identical across distinct stalling pids 7s apart, resolving
 * to LLVM compiler code unrelated to the call chain). The frame-dump
 * approach was falsified.
 *
 * This header gives wasix-libc's musl-side lock primitives the ability
 * to emit a per-call trace stream so that the LAST __lock without a
 * matching __unlock (or __tl_lock without __tl_unlock, or __wait
 * without __wake) before the bash futex_wait{expected=2} hang is
 * empirically visible in stderr output. The lock-taker identifies
 * itself by its call site tag.
 *
 * Gating — COMPILE-TIME ONLY
 * ==========================
 * Instrumentation is gated on the compile-time macro
 * __FIREBOX_TRACE_MUSL_LOCK__. When unset (the production default),
 * every site is a no-op the compiler eliminates entirely (zero text/
 * data footprint).
 *
 * Compile-time gating (vs runtime env-var gating) is deliberate:
 *
 *   getenv("FIREBOX_TRACE_MUSL_LOCK") would call __wasilibc_ensure_environ
 *   which calls malloc/calloc. Dlmalloc has its own internal mutex
 *   (a __lock site). So a runtime check inside __lock would recurse
 *   into __lock → infinite loop. Compile-time gating eliminates the
 *   risk entirely.
 *
 * Output discipline
 * =================
 * - write(2, ...) directly (no dprintf, no stdio) — avoids feedback
 *   loops if stdio FILE locks are themselves what's leaking.
 * - snprintf() into a fixed-size stack buffer. snprintf uses a
 *   FILE with .lock=-1, so it does not take stdio locks. Format
 *   specifiers are restricted to %s/%d/%p (no %a/%n/locale-sensitive
 *   forms) so vfprintf takes no locks.
 * - getpid() is a direct __wasi_proc_id syscall, no locks.
 *
 * Output format (one line per event)
 * ==================================
 * "[firebox-lock-trace] <op> pid=%d addr=%p val=%d site=<tag>\n"
 *
 * where <op> ∈ { lock_enter, lock_exit, lock_wait_pre, unlock_enter,
 *                unlock_exit, tl_lock_enter, tl_lock_exit, tl_lock_wait_pre,
 *                tl_unlock_enter, tl_unlock_exit, wait_enter, wait_exit,
 *                wake_enter, wake_exit }
 *
 * Reading the trace
 * =================
 * After a hung run with __FIREBOX_TRACE_MUSL_LOCK__ enabled, the last
 * <op>=lock_enter (or tl_lock_enter / wait_enter) on bash's pid before
 * the futex_wait{expected=2} hang point WITHOUT a matching <op>=*_exit
 * is the leak. The `addr` field cross-references with bash's symbol
 * table to identify the caller (e.g. atexit_lock, __thread_list_lock,
 * a bash-internal mutex address inside bash's .bss/.data).
 */
#ifndef FIREBOX_LOCK_TRACE_H
#define FIREBOX_LOCK_TRACE_H

#ifdef __FIREBOX_TRACE_MUSL_LOCK__

#include <unistd.h>   /* write, getpid */
#include <stdio.h>    /* snprintf */

/* Emit one trace line. `op` is the operation tag; `tag` is the call-site
 * identifier; `addr` is the lock address; `val` is the observed value at
 * the moment of trace (caller picks before/after).
 *
 * The hidden visibility annotation matches the rest of musl's internal
 * helpers; this keeps the symbol out of the public ABI. inline-with-
 * static-storage means each TU gets its own copy and the compiler is
 * free to inline at every site.
 */
static inline void firebox_lock_trace_emit(const char *op, const char *tag,
                                            volatile const void *addr, int val)
{
    char buf[256];
    int n = snprintf(buf, sizeof(buf),
        "[firebox-lock-trace] %s pid=%d addr=%p val=%d site=%s\n",
        op, (int)getpid(), (const void *)addr, val, tag);
    if (n > 0) {
        if ((size_t)n >= sizeof(buf)) n = sizeof(buf) - 1;
        /* Best-effort write; ignore short writes / errors — diagnostic
         * stream, not a correctness contract. */
        (void)write(2, buf, (size_t)n);
    }
}

#define FIREBOX_LOCK_TRACE(op, tag, addr, val) \
    firebox_lock_trace_emit((op), (tag), (addr), (val))

#else  /* __FIREBOX_TRACE_MUSL_LOCK__ not defined */

#define FIREBOX_LOCK_TRACE(op, tag, addr, val) ((void)0)

#endif

#endif /* FIREBOX_LOCK_TRACE_H */
