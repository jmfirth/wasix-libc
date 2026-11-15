/* firebox#5X0: __FBX_THREAD_LOCAL */
#include <features.h>
#ifndef __wasilibc_unmodified_upstream
#include <setjmp.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>

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
 * Mask storage (firebox#QY4 L1): WITH THE CONTEXT, never beside it. This used
 * to be one thread-local slot keyed by the last jmp_buf recorded and cleared
 * by its first restore, so a second siglongjmp to the same buffer restored
 * nothing (the shell's top-level `sigsetjmp` + `siglongjmp` on every Ctrl-C:
 * the second SIGINT stayed blocked and was never delivered) and a sigsetjmp
 * on any other buffer evicted the first buffer's mask. musl and glibc keep
 * the mask in the jmp_buf and restore it on EVERY return of sigsetjmp after a
 * jump; so does this, in the one place each shelf has for it:
 *
 *  - EH shelf: jmp_buf is the musl-shaped struct, and its __fl / __ss fields
 *    hold a buffer-specific flag and mask. The private EH capture state and
 *    the 24-byte stack snapshot both end before __fl.
 *  - stack-snapshot shelf: jmp_buf IS the 24-byte __wasi_stack_snapshot_t and
 *    the host writes all of it, so there is no room, and growing it would
 *    break every consumer already compiled against 24 bytes (a reminted
 *    libc.so writing past them corrupts whatever the consumer put next). But
 *    on this shelf a jump REWINDS sigsetjmp's own wasm frame, locals
 *    included, to what they were at the capture: a mask held in a local is
 *    the context's own copy, restored as often as the context is jumped to.
 *    That is what musl's __sigsetjmp_tail does, with the frame for storage.
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
#include "wasm_setjmp.h"
#include <wasi/api.h>

/* The musl-shaped jmp_buf this shelf's <setjmp.h> declares; `__fl` says
 * whether `__ss` holds a mask to restore, as in musl. */
_Static_assert(sizeof(((struct __jmp_buf_tag *)0)->__ss) >= sizeof(sigset_t),
               "jmp_buf's __ss must hold a sigset_t");
_Static_assert(offsetof(struct __jmp_buf_tag, __fl) >= sizeof(__wasi_stack_snapshot_t),
               "jmp_buf's __fl must lie past the stack snapshot");

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
 * the pass to rewrite.
 *
 * It runs before the capture and not on a jump's return, so the buffer keeps
 * the mask of the sigsetjmp that recorded it until a sigsetjmp on the SAME
 * buffer records again. */
__attribute__((noinline))
void __firebox_sigsetjmp_record(void *buf, int savesigs) {
    struct __jmp_buf_tag *jb = buf;
    jb->__fl = 0;
    ((struct __wasm_jmp_buf *)buf)->mask_pending = 0;
    if (savesigs &&
        pthread_sigmask(SIG_SETMASK, NULL, (sigset_t *)(void *)jb->__ss) == 0) {
        jb->__fl = __wasm_sigjmp_cookie(buf);
        ((struct __wasm_jmp_buf *)buf)->mask_pending = __wasm_sigjmp_pending(buf);
    }
}

/* The mask-restore half, split out of siglongjmp's body by firebox#SGG so the
 * consumer-frame macro can run it before the jump. Restoring the mask BEFORE
 * transferring control is the order the inlined version had and the order
 * POSIX requires: by the time the sigsetjmp call site returns non-zero, the
 * mask must already be the one that was saved. It leaves the buffer as it
 * found it: every later siglongjmp to it restores the same mask. */
__attribute__((noinline))
void __firebox_siglongjmp_restore(void *buf) {
    struct __jmp_buf_tag *jb = buf;
    if (jb->__fl == __wasm_sigjmp_cookie(buf))
        pthread_sigmask(SIG_SETMASK, (const sigset_t *)(const void *)jb->__ss, NULL);
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

#else /* the stack-snapshot shelf */

#if !defined(__OPTIMIZE__)
#error "sigsetjmp requires an optimized build: the saved mask must live in a wasm local"
#endif

/* The signals a mask can hold here (1.._NSIG-1) fit one 64-bit word; the
 * mask travels in a scalar, never in memory, because only the frame's
 * LOCALS are part of what the jump rewinds. The shadow-stack bytes of this
 * frame are not: they are whatever ran there since. */
_Static_assert(_NSIG - 1 <= 64, "a signal mask must fit one 64-bit local");

static uint64_t __fbx_mask_word(const sigset_t *set) {
    const unsigned long *w = (const unsigned long *)(const void *)set;
    if (sizeof(unsigned long) >= sizeof(uint64_t))
        return (uint64_t)w[0];
    return (uint64_t)w[0] | ((uint64_t)w[1] << 32);
}

static void __fbx_mask_set(sigset_t *set, uint64_t mask) {
    unsigned long *w = (unsigned long *)(void *)set;
    sigemptyset(set);
    if (sizeof(unsigned long) >= sizeof(uint64_t)) {
        w[0] = (unsigned long)mask;
    } else {
        w[0] = (unsigned long)mask;
        w[1] = (unsigned long)(mask >> 32);
    }
}

int sigsetjmp(jmp_buf buf, int savesigs) {
    uint64_t saved = 0;
    if (savesigs) {
        sigset_t now;
        sigemptyset(&now);
        if (pthread_sigmask(SIG_SETMASK, NULL, &now) != 0)
            savesigs = 0;
        else
            saved = __fbx_mask_word(&now);
    }
    int ret = setjmp(buf);
    if (ret != 0 && savesigs) {
        /* A jump's return: `saved` and `savesigs` are the values this
         * capture had, whatever buffer or jump came in between. */
        sigset_t set;
        __fbx_mask_set(&set, saved);
        pthread_sigmask(SIG_SETMASK, &set, NULL);
    }
    return ret;
}

_Noreturn void siglongjmp(jmp_buf buf, int val);
_Noreturn void siglongjmp(jmp_buf buf, int val) {
    longjmp(buf, val);
}

#endif /* __wasm_exception_handling__ */

#endif
