#ifdef _REENTRANT
#include <stdatomic.h>
extern void __wasi_init_tp(void);
#endif
#include <wasi/api.h>
extern void __wasm_call_ctors(void);
extern int __main_void(void);
extern void __wasm_call_dtors(void);
extern void __wasi_init_signals(void);

__attribute__((export_name("_start")))
void _start(void) {
    // Commands should only be called once per instance. This simple check
    // ensures that the `_start` function isn't started more than once.
    //
    // We use `volatile` here to prevent the store to `started` from being
    // sunk past any subsequent code, and to prevent any compiler from
    // optimizing based on the knowledge that `_start` is the program
    // entrypoint.
#ifdef _REENTRANT
    static volatile _Atomic int started = 0;
    int expected = 0;
    if (!atomic_compare_exchange_strong(&started, &expected, 1)) {
	__builtin_trap();
    }
#else
    static volatile int started = 0;
    if (started != 0) {
	__builtin_trap();
    }
    started = 1;
#endif

#ifdef _REENTRANT
	__wasi_init_tp();
#endif

    /* firebox#R0A — signal state is established BEFORE constructors run,
     * because that is the order Linux has.
     *
     * On Linux the kernel installs the process signal mask (inherited, or
     * `attr->__mask` for POSIX_SPAWN_SETSIGMASK) and resets catchable
     * dispositions as part of `exec`, before the dynamic loader and before any
     * `.init_array` entry. A constructor therefore runs ON TOP of the final
     * signal state and its changes survive into `main`.
     *
     * With `__wasi_init_signals()` after `__wasm_call_ctors()` the opposite
     * held, and BOTH halves of it were destructive — MEASURED 2026-09-19 in a
     * spawned child whose constructor blocked SIGPIPE and installed a SIGUSR2
     * handler:
     *   - the `pthread_sigmask(SIG_SETMASK, ...)` that adopts the spawned mask
     *     (firebox#1QR) ERASED the constructor's block — observed mask was the
     *     requested {SIGTERM} alone, not {SIGTERM, SIGPIPE};
     *   - the inherited-disposition replay OVERWROTE the constructor's handler
     *     with the parent's SIG_IGN.
     * Neither is exotic: a constructor blocking SIGPIPE and a C++ static
     * initialiser installing a handler are ordinary Linux idioms, and both
     * failed silently — the guest saw a plausible mask, not an error.
     *
     * ⛔ Safe here and not merely earlier: `__wasi_init_tp()` above has already
     * established TLS, which is all `pthread_sigmask` and musl's
     * self-initialising malloc need. Data relocations are NOT a hazard either —
     * for a PIE main module the HOST applies them before `_start` is entered
     * (`__wasm_apply_data_relocs` / `__wasm_apply_tls_relocs` are called from
     * the wasix linker's main-module path and from `WasiEnv` instance init),
     * not from `__wasm_call_ctors`, so every pointer `__wasi_init_signals`
     * touches is already relocated. */
    __wasi_init_signals();

    // The linker synthesizes this to call constructors.
    __wasm_call_ctors();

    // Call `__main_void` which will either be the application's zero-argument
    // `__main_void` function or a libc routine which obtains the command-line
    // arguments and calls `__main_argv_argc`.
    int r = __main_void();

    // Call atexit functions, destructors, stdio cleanup, etc.
    __wasm_call_dtors();

    // If main exited successfully, just return, otherwise call
    // `__wasi_proc_exit`.
    if (r != 0) {
        __wasi_proc_exit2(r);

        // This case is only reachable with the setjmp/longjmp-based vfork.
        // If control ever returns here, it means the child continued
        // execution past the function calling vfork without an intervening
        // proc_exec or exit/_Exit, violating the required vfork semantics.
        // Such a state is undefined behaviour, so this path is correctly
        // marked unreachable.
        __builtin_unreachable();
    }
}
