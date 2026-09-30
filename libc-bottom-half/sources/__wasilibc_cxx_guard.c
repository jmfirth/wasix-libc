// firebox#ZVA — the "no handler" owner for C++ exceptions on wasm EH.
//
// On Linux an exception nothing catches never leaves `__cxa_throw`: the
// unwinder's phase-1 search finds no handler, `_Unwind_RaiseException`
// RETURNS, and libc++abi calls `std::__terminate` — the terminate handler runs,
// `std::current_exception()` is valid inside it, and the default handler prints
// "terminating due to uncaught exception of type ..." and `abort()`s (SIGABRT).
//
// Wasm EH has NO phase 1 (LLVM WasmEHPrepare.cpp): `_Unwind_RaiseException` is
// a bare `throw __cpp_exception` (libunwind Unwind-wasm.c), so it never
// returns and libc++abi's `failed_throw` is dead code. With no catching frame
// the exception escapes the module's entry point and the HOST reports it as an
// uncaught-exception trap — no terminate handler, no message, no SIGABRT.
//
// So the "no handler" decision has to be made by whoever owns the OUTERMOST
// frame, and that is libc: `__wasilibc_start_main` (ctors, main, dtors) and
// `__wasi_thread_start_C` (a thread's start routine) run guest code through
// `__wasilibc_cxx_guarded_call`, which catches `__cpp_exception` — and ONLY
// that tag, never `catch_all`, so a `__c_longjmp` passes straight through to
// its setjmp — restores the stack pointer the unwound frames never restored,
// and hands the payload (the `_Unwind_Exception *`) to the hook libc++abi
// registered in `__cxa_throw`/`__cxa_rethrow`/
// `__cxa_rethrow_primary_exception`. That hook reproduces libc++abi's own
// no-handler path: `__cxa_begin_catch` then `std::__terminate(handler)`.
// With no hook (a non-libc++abi thrower) the process `abort()`s.
//
// ⚠️ ONE STANDARD-PERMITTED DIVERGENCE, INHERENT AND NOT FIXABLE HERE: with no
// phase 1, destructors of the unwound frames RUN before terminate. Linux does
// not unwind at all when no handler exists; [except.handle]/9 leaves it
// implementation-defined whether the stack is unwound in that case, so both
// are conforming.
//
// ⛔ WHY THE GUARD IS LINKED ONLY WHEN libc++abi IS. Callers reference
// `__wasilibc_cxx_guarded_call` WEAKLY and fall back to a direct call. In a
// static link the guard's archive member is pulled only by the strong
// reference libc++abi's `__cxa_throw` makes to
// `__wasilibc_set_cxx_uncaught_hook` (defined in this same object), so a pure C
// program on an EH shelf carries NO `try_table`. That matters because
// `wasm-opt --asyncify` aborts on a `try_table` (Flatten.cpp), and asyncified C
// programs on the EH shelf are a measured working mode (firebox#EHCO/#EHSJ). A
// C++ program already carries libc++abi's own `try_table`s, so it loses
// nothing. The shared provider whole-archives libc and so always carries it.

#include <stdlib.h>

typedef void (*__wasilibc_cxx_uncaught_hook_t)(void *);

static __wasilibc_cxx_uncaught_hook_t __wasilibc_cxx_uncaught_hook;

// Registered by libc++abi immediately before every raise. Idempotent: every
// caller stores the same function, so a relaxed store is sufficient and a
// racing thread can only ever observe NULL or that function.
void __wasilibc_set_cxx_uncaught_hook(__wasilibc_cxx_uncaught_hook_t hook) {
    __atomic_store_n(&__wasilibc_cxx_uncaught_hook, hook, __ATOMIC_RELAXED);
}

// Reached from the guard's catch clause with the thrown payload.
__attribute__((__visibility__("hidden"), __used__, __noreturn__))
void __wasilibc_cxx_uncaught(void *payload) {
    __wasilibc_cxx_uncaught_hook_t hook =
        __atomic_load_n(&__wasilibc_cxx_uncaught_hook, __ATOMIC_RELAXED);
    if (hook)
        hook(payload);
    // The hook is libc++abi's terminate path and does not return; a hook
    // that did, or a `__cpp_exception` thrown with no libc++abi at all, still
    // dies the Linux way.
    abort();
}

#if defined(__wasm_exception_handling__) && !defined(__wasm64__)

// void __wasilibc_cxx_guarded_call(void (*fn)(void *), void *arg)
//
// Written in assembly because C has no way to catch a wasm exception, and a
// C++ translation unit would drag libc++abi's personality into libc.
//
// The tag: in a non-PIC object the code generator itself emits the
// `__cpp_exception` definition for a tag the module uses, but through the
// inline-asm parser it comes out LOCAL — a private tag no libc++abi throw
// would ever match. The explicit `.weak` makes it the same weak definition
// clang gives every catching C++ TU, which the linker merges into the one
// process-wide tag. A PIC object leaves it undefined, so a shared provider
// IMPORTS the tag rather than minting a private one (the rule the #HNX
// sentinel in firebox's build.sh enforces for `__c_longjmp`).
//
// EH model: exnref (`try_table`) is the default because it is the only model
// the shipped runtime validates; a legacy (`try`/`catch`) shelf opts out with
// `-D__WASILIBC_LEGACY_EH__` (Makefile-eh, EXNREF_EH=no).
__asm__(
    "\t.tabletype __indirect_function_table, funcref\n"
    "\t.globaltype __stack_pointer, i32\n"
    "\t.tagtype __cpp_exception i32\n"
#ifndef __PIC__
    "\t.weak __cpp_exception\n"
#endif
    "\t.functype __wasilibc_cxx_uncaught (i32) -> ()\n"
    "\t.section .text.__wasilibc_cxx_guarded_call,\"\",@\n"
    "\t.globl __wasilibc_cxx_guarded_call\n"
    "\t.type __wasilibc_cxx_guarded_call,@function\n"
    "__wasilibc_cxx_guarded_call:\n"
    "\t.functype __wasilibc_cxx_guarded_call (i32, i32) -> ()\n"
    "\t.local i32\n"
    // The frames a throw unwinds never run their epilogues, so the stack
    // pointer they moved is restored from here on the catch path.
    "\tglobal.get __stack_pointer\n"
    "\tlocal.set 2\n"
#ifdef __WASILIBC_LEGACY_EH__
    "\ttry\n"
    "\tlocal.get 1\n"
    "\tlocal.get 0\n"
    "\tcall_indirect __indirect_function_table, (i32) -> ()\n"
    "\tcatch __cpp_exception\n"
    "\tlocal.get 2\n"
    "\tglobal.set __stack_pointer\n"
    "\tcall __wasilibc_cxx_uncaught\n"
    "\tend_try\n"
#else
    "\tblock i32\n"
    "\ttry_table (catch __cpp_exception 0)\n"
    "\tlocal.get 1\n"
    "\tlocal.get 0\n"
    "\tcall_indirect __indirect_function_table, (i32) -> ()\n"
    "\treturn\n"
    "\tend_try_table\n"
    "\tunreachable\n"
    "\tend_block\n"
    // The payload (`_Unwind_Exception *`) is on the operand stack.
    "\tlocal.get 2\n"
    "\tglobal.set __stack_pointer\n"
    "\tcall __wasilibc_cxx_uncaught\n"
#endif
    "\tend_function\n"
    "\t.text\n");

#else

// No wasm EH on this shelf (or wasm64, whose C++ runtime is built without
// exceptions): nothing can throw `__cpp_exception`, so the guard is a plain
// call.
void __wasilibc_cxx_guarded_call(void (*fn)(void *), void *arg) {
    fn(arg);
}

#endif
