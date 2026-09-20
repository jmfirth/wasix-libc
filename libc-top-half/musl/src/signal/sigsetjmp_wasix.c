/* firebox#5X0: __FBX_THREAD_LOCAL */
#include <features.h>
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
 * firebox#EHSJ: libc is no longer built with `-mllvm -wasm-enable-sjlj`, so
 * that pass does not run over THIS file any more and the split is currently
 * inert here. It is kept because the hazard returns the moment anyone
 * reinstates the flag, and because the split costs nothing.
 *
 * Mask storage: thread-local slot keyed by jmp_buf pointer. Single-slot,
 * so nested sigsetjmp pairs within a thread will clobber the outer saved
 * mask — documented limitation, covers the bash/perl exception-unwind
 * pattern that motivated this feature.
 *
 * firebox#SGG: on the EXCEPTION-HANDLING shelf the two frame-sensitive halves
 * of this pair -- the capture and the jump -- cannot live in this file at all.
 * They must compile in the CALLER's translation unit, because that is the only
 * frame where WebAssemblyLowerEmscriptenEHSjLj can put the `try_table` landing
 * pad that `__wasm_longjmp`'s `__c_longjmp` throw unwinds to. <setjmp.h>
 * therefore makes sigsetjmp/siglongjmp MACROS on that shelf (the vfork()
 * shape, see <unistd.h>), and what stays here is the mask save and the mask
 * restore, which are frame-independent and are now exported for those macros
 * to call.
 *
 * The out-of-line `sigsetjmp` and `siglongjmp` below stay defined and exported
 * on every shelf. On the EH shelf they are NOT the working implementation and
 * no consumer reaches them through <setjmp.h> -- their `setjmp`/`longjmp` are
 * libc's own, which on this shelf are the WASIX stack-snapshot pair, and that
 * pair needs asyncify instrumentation this shelf does not carry (MEASURED: the
 * out-of-line arm fails 5/5, trapping `unreachable` in `__wasilibc_setjmp`).
 * They are kept because autoconf's AC_CHECK_FUNCS(sigsetjmp) is a LINK probe
 * that bypasses the macro and asks the linker for the symbol; deleting them
 * would make every such package conclude the platform has no sigsetjmp. They
 * are also still the real implementation on the non-EH shelves, where libc's
 * setjmp/longjmp ARE the stack-snapshot pair and do work.
 */

#ifdef __wasm_exception_handling__
/* Exported: <setjmp.h>'s consumer-frame macros are the callers. */
#define __FBX_SJ_HALF
#else
/* No macros on this shelf; the only callers are the two functions below. */
#define __FBX_SJ_HALF static
#endif

static __FBX_THREAD_LOCAL sigset_t __firebox_saved_mask;
static __FBX_THREAD_LOCAL void *__firebox_saved_env;
static __FBX_THREAD_LOCAL int __firebox_saved_valid;

/* Out-of-line recorder. The noinline attribute is load-bearing: if the
 * body were inlined back into sigsetjmp, the resulting non-trivial
 * control flow around `return setjmp(buf)` would re-trigger SJLJ
 * instrumentation and reintroduce the `.N` suffix refs. Same firebox#EHSJ
 * note as above: inert while the sjlj flag is off, kept as a guard.
 *
 * firebox#SGG: noinline is load-bearing a second way now. In a CONSUMER
 * compiled with `-mllvm --wasm-enable-sjlj`, this is the call that runs
 * immediately before the lowered `__wasm_setjmp`; keeping it a call rather
 * than inlined control flow is what keeps that call site simple enough for
 * the pass to rewrite. */
__attribute__((noinline))
__FBX_SJ_HALF void __firebox_sigsetjmp_record(void *buf, int savesigs) {
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

/* The mask-restore half, split out of siglongjmp's body by firebox#SGG so the
 * consumer-frame macro can run it before the jump. Restoring the mask BEFORE
 * transferring control is the order the inlined version had and the order
 * POSIX requires: by the time the sigsetjmp call site returns non-zero, the
 * mask must already be the one that was saved. */
__attribute__((noinline))
__FBX_SJ_HALF void __firebox_siglongjmp_restore(void *buf) {
    if (__firebox_saved_valid && __firebox_saved_env == buf) {
        pthread_sigmask(SIG_SETMASK, &__firebox_saved_mask, NULL);
        __firebox_saved_valid = 0;
    }
}

/* <setjmp.h> makes these two macros on the EH shelf; undefine them so the
 * out-of-line definitions that the AC_CHECK_FUNCS link probe needs can still
 * be written here. */
#undef sigsetjmp
#undef siglongjmp

int sigsetjmp(jmp_buf buf, int savesigs) {
    __firebox_sigsetjmp_record((void *)buf, savesigs);
    return setjmp(buf);
}

_Noreturn void siglongjmp(jmp_buf buf, int val);
_Noreturn void siglongjmp(jmp_buf buf, int val) {
    __firebox_siglongjmp_restore((void *)buf);
    longjmp(buf, val);
}

#endif
