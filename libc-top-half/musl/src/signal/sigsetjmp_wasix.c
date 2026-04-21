#ifndef __wasilibc_unmodified_upstream
#include <setjmp.h>
#include <signal.h>
#include <stddef.h>

/* WASIX sigsetjmp/siglongjmp with mask save/restore.
 *
 * Lives in its own TU (separate from setjmp/setjmplongjmp.c which defines
 * __wasm_setjmp / __wasm_setjmp_test / __wasm_longjmp). Co-locating these
 * functions with those helpers triggers LLVM 21.1.2's
 * WebAssemblyLowerEmscriptenEHSjLj pass to emit per-call-site
 * `__wasm_setjmp.N` suffixed refs that wasm-ld 21.1.2 does not resolve.
 * See issue #37 and docs/runtime-gotchas.md §8.
 *
 * Mask storage: thread-local slot keyed by jmp_buf pointer. Single-slot,
 * so nested sigsetjmp pairs within a thread will clobber the outer saved
 * mask — documented limitation, covers the bash/perl exception-unwind
 * pattern that motivated this feature.
 */

static __thread sigset_t __firebox_saved_mask;
static __thread void *__firebox_saved_env;
static __thread int __firebox_saved_valid;

/* Out-of-line recorder. The noinline attribute is load-bearing: if the
 * body were inlined back into sigsetjmp, the resulting non-trivial
 * control flow around `return setjmp(buf)` would re-trigger SJLJ
 * instrumentation and reintroduce the `.N` suffix refs. */
__attribute__((noinline))
static void __firebox_sigsetjmp_record(void *buf, int savesigs) {
    if (savesigs) {
        sigemptyset(&__firebox_saved_mask);
        pthread_sigmask(SIG_SETMASK, NULL, &__firebox_saved_mask);
        __firebox_saved_env = buf;
        __firebox_saved_valid = 1;
    } else if (__firebox_saved_env == buf) {
        /* savesigs=0 -> siglongjmp must NOT restore a mask for this env.
         * Invalidate the saved slot if it was keyed to this env. */
        __firebox_saved_valid = 0;
    }
}

int sigsetjmp(jmp_buf buf, int savesigs) {
    __firebox_sigsetjmp_record((void *)buf, savesigs);
    return setjmp(buf);
}

_Noreturn void siglongjmp(jmp_buf buf, int val);
_Noreturn void siglongjmp(jmp_buf buf, int val) {
    if (__firebox_saved_valid && __firebox_saved_env == (void *)buf) {
        pthread_sigmask(SIG_SETMASK, &__firebox_saved_mask, NULL);
        __firebox_saved_valid = 0;
    }
    longjmp(buf, val);
}

#endif
