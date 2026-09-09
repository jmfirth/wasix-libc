/* firebox#5X0: __FBX_THREAD_LOCAL */
#include <features.h>
#define _GNU_SOURCE
#include <unistd.h>

#include <signal.h>
#ifdef __wasilibc_unmodified_upstream
#include "syscall.h"
#endif

#if defined(__wasilibc_unmodified_upstream) ||                                 \
    !defined(__wasm_exception_handling__)

pid_t vfork(void) {
#ifdef __wasilibc_unmodified_upstream
  /* vfork syscall cannot be made from C code */
#ifdef SYS_fork
  return syscall(SYS_fork);
#else
  return syscall(SYS_clone, SIGCHLD, 0);
#endif
#else
  return _fork_internal(0);
#endif
}

#elif defined(__wasm_exception_handling__)

#include <errno.h>

// The jump buffers used by setjmp/longjmp to restore the parent context
// __vfork_jump_free_index points to the index of a buffer that can be
// overwritten. The other buffer should not be modified as it contains the
// jmp_buf that can be used to longjmp back to the parent context after
// proc_exit or proc_exec
__FBX_THREAD_LOCAL jmp_buf __vfork_jump[2];
__FBX_THREAD_LOCAL int __vfork_jump_free_index = 0;
// The pid of the vforked process
static __FBX_THREAD_LOCAL pid_t __child_pid;

// setjmp/longjmp based vfork implementation
//
// This implementation of vfork uses the wasix proc_vfork syscall to create a
// new process that shares the address space with the parent until proc_exec* or
// proc_exit2 is called. However the syscalls can not cause us to jump back to
// the point where vfork was called, so we use setjmp/longjmp to simulate that
// behavior.
//
// This should work fine, given that the guarantees of setjmp/longjmp and vfork
// mostly align with each other. The only major caveat is that we must call the
// setjmp in the function that called vfork, so vfork must be a macro and not a
// real function. It expands to
// `__vfork_internal(setjmp(__vfork_jump[__vfork_jump_free_index]))`.
//
// proc_exit and proc_exec both return in the parent process after the child has
// exited or execed, so we need to longjmp back to the original context in those
// cases.
pid_t __vfork_internal(int setjmp_result) {
  if (setjmp_result == 0) {
    // Swaps our env with a shallow clone
    int ret = __wasi_proc_fork_env(&__child_pid);
    if (ret != 0) {
      // Fork failed
      errno = ret;
      return (pid_t)-1;
    }

    // If the vfork was successful swap the jump buffers
    __vfork_jump_free_index = 1 - __vfork_jump_free_index;

    // If the vfork succeeded we are now in the child

    // In the child vfork returns 0
    return (pid_t)0;
  } else {
    // In the parent vfork returns the child pid
    return __child_pid;
  }
}

/* Defined in src/setjmp/setjmplongjmp.c on this shelf. This is the SJLJ
 * half of the pair -- it throws the `__c_longjmp` tag that
 * WebAssemblyLowerEmscriptenEHSjLj's generated `try_table` catches. */
void __wasm_longjmp(void *env, int val);

// This function must be called in case proc_exit2 or proc_exec return without
// error
//
// ⛔ THIS MUST USE `__wasm_longjmp`, NOT THE C-VISIBLE `longjmp` (firebox#EHR
// regression, caught by the exec-argv-empty-element contract member).
//
// The buffer being restored here was NOT captured by libc. `vfork()` is a
// MACRO (unistd.h) that expands to
// `__vfork_internal(setjmp(__vfork_jump[...]))`, so the `setjmp` half compiles
// in the CONSUMER's translation unit -- and the toolchain we ship to consumers
// (wasixcc) passes `-mllvm --wasm-enable-sjlj`, which rewrites that call site
// into `__wasm_setjmp` plus a `try_table`. The jmp_buf therefore always holds
// an SJLJ `struct jmp_buf_impl`, never a `__wasi_stack_snapshot_t`.
//
// Calling the C-visible `longjmp` here routes into `__wasilibc_longjmp`
// (WASIX `stack_restore`), which reads that storage as a stack snapshot and
// traps -- `RuntimeError: unreachable`, no errno, no diagnostic. MEASURED
// 2026-09-09: the WASIX pair traps in a plain wasixcc-built EH guest even on a
// buffer it captured itself, because `stack_checkpoint` needs asyncify
// instrumentation these guests do not carry. So the WASIX mechanism is not an
// option on this shelf regardless of which half captured the buffer.
//
// This is NOT a reason to restore `-wasm-enable-sjlj` to libc's own CFLAGS.
// #EHSJ removed it so libc's C-visible setjmp/longjmp stopped emitting a
// `try_table` (which blocks `wasm-opt --asyncify`), and #EHR finished that
// because `weak_alias(setjmp, _setjmp)` is a non-call use of `@setjmp` the
// SJLJ pass refuses. Both of those stand. Naming the SJLJ helper directly is
// what lets vfork keep the mechanism its buffer is actually in without
// dragging the flag -- and without depending on a compile flag at all.
//
// No libc translation unit calls `vfork()`: the one that appears to,
// src/thread/clone.c, is inside `#if defined(__wasilibc_unmodified_upstream)`
// and compiles to an empty `clone.o` here. Consumers are the only callers, so
// the SJLJ capture above holds for every buffer that reaches this function.
_Noreturn void __vfork_restore() {
  // Longjmp back to the vfork call site in the parent
  __wasm_longjmp(__vfork_jump[1 - __vfork_jump_free_index], 1);
  __builtin_unreachable();
}

#endif /* defined(__wasilibc_unmodified_upstream) ||                           \
          !defined(__wasm_exception_handling__) */
