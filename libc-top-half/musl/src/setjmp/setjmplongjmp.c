#ifndef __wasilibc_unmodified_upstream
#include <setjmp.h>

#  ifdef __wasm_exception_handling__
/*
 * function prototypes
 */
void __wasm_setjmp(void *env, unsigned int label, void *func_invocation_id);
unsigned int __wasm_setjmp_test(void *env, void *func_invocation_id);
void __wasm_longjmp(void *env, int val);

/*
 * this is a temorary storage used by the communication between
 * __wasm_sjlj_longjmp and WebAssemblyLowerEmscriptenEHSjL-generated
 * logic.
 * ideally, this can be replaced with multivalue.
 */
struct arg {
		void *env;
		int val;
};

/*
 * jmp_buf should have large enough size and alignment to contain
 * this structure.
 */
struct jmp_buf_impl {
        void *func_invocation_id;
        unsigned int label;

        struct arg arg;
};

void
__wasm_setjmp(void *env, unsigned int label, void *func_invocation_id)
{
        struct jmp_buf_impl *jb = (struct jmp_buf_impl *)env;
        if (label == 0) { /* ABI contract */
                __builtin_trap();
        }
        if (func_invocation_id == 0) { /* sanity check */
                __builtin_trap();
        }
        jb->func_invocation_id = func_invocation_id;
        jb->label = label;
}

unsigned int
__wasm_setjmp_test(void *env, void *func_invocation_id)
{
        struct jmp_buf_impl *jb = (struct jmp_buf_impl *)env;
        if (jb->label == 0) { /* ABI contract */
                __builtin_trap();
        }
        if (func_invocation_id == 0) { /* sanity check */
                __builtin_trap();
        }
        if (jb->func_invocation_id == func_invocation_id) {
                return jb->label;
        }
        return 0;
}

void
__wasm_longjmp(void *env, int val)
{
        struct jmp_buf_impl *jb = (struct jmp_buf_impl *)env;
        struct arg *arg = (struct arg *)&jb->arg;
        /*
         * C standard says:
         * The longjmp function cannot cause the setjmp macro to return
         * the value 0; if val is 0, the setjmp macro returns the value 1.
         */
        if (val == 0) {
                val = 1;
        }
        arg->env = env;
        arg->val = val;
        __builtin_wasm_throw(1, arg); /* 1 == C_LONGJMP */
}

#  endif /* __wasm_exception_handling__ */

/* ------------------------------------------------------------------------
 * The C-VISIBLE setjmp/longjmp, on BOTH shelves. (firebox#EHSJ)
 *
 * These used to exist only on the non-EH shelf. On the EH shelf libc itself
 * was compiled with `-mllvm -wasm-enable-sjlj`, so LLVM's
 * WebAssemblyLowerEmscriptenEHSjLj pass rewrote every `setjmp` call site in
 * libc into `__wasm_setjmp` + a `try_table` catching `__c_longjmp`, and there
 * was no `setjmp` SYMBOL to link against at all.
 *
 * That coupled two unrelated things: EH-for-C++-exceptions (which the shelf
 * genuinely needs) and EH-for-setjmp/longjmp (which it does not). The cost was
 * a single `try_table` in sigsetjmp_wasix.o -- and `wasm-opt --asyncify`
 * cannot process a module containing one, which took the whole EH libc
 * provider off the asyncify path.
 *
 * So libc's own setjmp/longjmp now go through the WASIX host-call mechanism
 * (`stack_checkpoint` / `stack_restore`) on both shelves, exactly as the
 * non-EH shelf always did. The `__wasm_*` helpers above STAY: the toolchain we
 * ship to consumers (wasixcc) still passes `-mllvm --wasm-enable-sjlj`, so a
 * consumer's own setjmp call sites are still SJLJ-lowered and still resolve
 * `__wasm_setjmp` / `__wasm_longjmp` / the `__c_longjmp` tag out of libc.
 * Removing them would break every such consumer link.
 *
 * The two mechanisms must never meet on one jmp_buf. Where BOTH halves are
 * libc code the pairing is automatic: time/timer_create.c captures and
 * restores its own `jb`, and sigsetjmp/siglongjmp use the consumer's STORAGE
 * but run libc code on both ends.
 *
 * ⛔ vfork.c IS THE EXCEPTION, and reading it as one of the automatic cases is
 * what regressed firebox#EHR. `vfork()` is a MACRO (unistd.h) whose `setjmp`
 * half expands in the CONSUMER's translation unit, where wasixcc's
 * `-mllvm --wasm-enable-sjlj` lowers it to `__wasm_setjmp`. libc only ever
 * supplies the RESTORE half, so `__vfork_restore` must call `__wasm_longjmp`
 * directly -- it does. Do not "simplify" it back to the C-visible `longjmp`.
 *
 * ⚠️ And the C-visible pair below is not a universal fallback: `stack_checkpoint`
 * needs asyncify instrumentation, so on a plain wasixcc-built EH guest
 * `__wasilibc_setjmp` TRAPS (MEASURED 2026-09-09, firebox#XAE). On this shelf
 * the C-visible pair is usable only by guests that carry that instrumentation.
 * ------------------------------------------------------------------------ */

#include <wasi/libc.h>

/* On the EH shelf <setjmp.h> still declares the 156-byte musl-shaped jmp_buf
 * rather than __wasi_stack_snapshot_t, because LLVM's SJLJ lowering in
 * consumer TUs writes its own 16-byte `struct jmp_buf_impl` into that same
 * storage. Only the size has to hold; wasm linear-memory accesses carry an
 * alignment HINT, never a trap, so the 4-vs-8 alignment gap is not a fault on
 * this target. Assert the size so a header change cannot silently truncate a
 * snapshot. */
_Static_assert(sizeof(jmp_buf) >= sizeof(__wasi_stack_snapshot_t),
               "jmp_buf must be able to hold a WASIX stack snapshot");

_Noreturn void longjmp (jmp_buf buf, int val) {
    __wasilibc_longjmp((__wasi_stack_snapshot_t *)buf, val);
}

int setjmp (jmp_buf buf) {
    return __wasilibc_setjmp((__wasi_stack_snapshot_t *)buf);
}

/* POSIX _setjmp/_longjmp are the no-signal-mask variants. musl's setjmp does
 * not touch the mask either (that is sigsetjmp's job, see
 * src/signal/sigsetjmp_wasix.c), so plain aliases are the faithful mapping --
 * exactly what musl does on every other arch.
 *
 * These now exist on BOTH shelves. The old note here said the EH shelf needed
 * no aliases because setjmp was not a symbol there; that premise is gone with
 * the flag. (firebox #P47, superseded by #EHSJ)
 */
weak_alias(setjmp, _setjmp);
weak_alias(longjmp, _longjmp);

/* WASIX sigsetjmp is defined in src/signal/sigsetjmp_wasix.c so it
 * can share the TLS saved-mask slot with siglongjmp. See issue #37. */

#endif
