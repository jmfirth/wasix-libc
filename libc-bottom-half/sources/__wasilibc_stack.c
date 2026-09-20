#include <wasi/api.h>
#include <sys/types.h>
#include <stdlib.h>

void* __wasilibc_get_stack_pointer(void) {
  void* val;
#if defined(__wasm64__)
  __asm__(".globaltype __stack_pointer, i64\n"
          "global.get __stack_pointer\n"
          "local.set %0" : "=r" (val));
#else
  __asm__(".globaltype __stack_pointer, i32\n"
          "global.get __stack_pointer\n"
          "local.set %0" : "=r" (val));
#endif
  return val;
}

void __wasilibc_set_stack_pointer(void *val) {
#if defined(__wasm64__)
  __asm__(".globaltype __stack_pointer, i64\n"
          "local.get %0\n"
          "global.set __stack_pointer" : /* no outputs */ : "r" (val));
#else
  __asm__(".globaltype __stack_pointer, i32\n"
          "local.get %0\n"
          "global.set __stack_pointer" : /* no outputs */ : "r" (val));
#endif
}

/* firebox#5X0: `__wasm_init_tls` / `__tls_size` / `__tls_align` are synthesized
 * by wasm-ld ONLY under `--shared-memory`. The nothreads variants link without
 * it, so these three helpers turn into UNDEFINED references — and because the
 * shared `libc.so` is linked `--whole-archive --unresolved-symbols=import-dynamic`,
 * every one of them became an `env.*` IMPORT on a module that has no importer
 * to satisfy it (two of them GLOBAL imports, which the wasmer linker rejects
 * outright — see linker.rs `resolve_env_symbol`).
 *
 * There is nothing for them to do on a nothreads module either: libc's own
 * thread-locals degenerate to plain globals on that profile (see
 * __FBX_THREAD_LOCAL in features.h), so no TLS block is left to bootstrap.
 *
 * `__wasilibc_{get,set}_tls_base` go with them, and NOT merely because they are
 * unused: `__wasilibc_set_tls_base` is the module's single `global.set
 * __tls_base`, and wasm-ld emits `__tls_base` IMMUTABLE on a non-shared-memory
 * link — so its mere presence made the nothreads libc.so fail `wasm-tools
 * validate` with "global is immutable: cannot modify it with global.set".
 * That is firebox#3EC's blocker, removed at its root rather than worked around
 * by trying to make the global mutable (which #3EC showed leaks a wrong,
 * init=0 __tls_base into every static-fat consumer of the same archive). */
#ifndef __FIREBOX_NO_THREADS__

void __wasm_init_tls(size_t val);

void __wasilibc_init_tls(void *val) {
  __wasm_init_tls((size_t)val);
}

unsigned long long __wasilibc_tls_size(void) {
  size_t val;
#if defined(__wasm64__)
  __asm__(".globaltype __tls_size, i64, immutable\n"
          "global.get __tls_size\n"
          "local.set %0" : "=r" (val));
#else
  __asm__(".globaltype __tls_size, i32, immutable\n"
          "global.get __tls_size\n"
          "local.set %0" : "=r" (val));
#endif
  return (unsigned long long)val;
}

unsigned long long __wasilibc_tls_align(void) {
  size_t val;
#if defined(__wasm64__)
  __asm__(".globaltype __tls_align, i64, immutable\n"
          "global.get __tls_align\n"
          "local.set %0" : "=r" (val));
#else
  __asm__(".globaltype __tls_align, i32, immutable\n"
          "global.get __tls_align\n"
          "local.set %0" : "=r" (val));
#endif
  return (unsigned long long)val;
}

void* __wasilibc_get_tls_base(void) {
  void* val;
#if defined(__wasm64__)
  __asm__(".globaltype __tls_base, i64\n"
          "global.get __tls_base\n"
          "local.set %0" : "=r" (val));
#else
  __asm__(".globaltype __tls_base, i32\n"
          "global.get __tls_base\n"
          "local.set %0" : "=r" (val));
#endif
  return val;
}

void __wasilibc_set_tls_base(void *val) {
#if defined(__wasm64__)
  __asm__(".globaltype __tls_base, i64\n"
          "local.get %0\n"
          "global.set __tls_base" : /* no outputs */ : "r" (val));
#else
  __asm__(".globaltype __tls_base, i32\n"
          "local.get %0\n"
          "global.set __tls_base" : /* no outputs */ : "r" (val));
#endif
}

#endif /* !__FIREBOX_NO_THREADS__ */

/* firebox#SGG: the WASIX stack-snapshot setjmp/longjmp pair needs asyncify
 * instrumentation in the module that calls it. A guest that lacks it (every
 * exception-handling-shelf guest, because `wasm-opt --asyncify` is not run
 * over them) gets an HONEST refusal back from the host -- and both functions
 * below used to convert that refusal into a bare trap: `__builtin_trap()` in
 * one, and falling off the end of a `_Noreturn` function in the other. The
 * user saw `RuntimeError: unreachable` with no errno, no message and a frame
 * pointing at libc, which is indistinguishable from a libc bug.
 *
 * Invariant 0: an honest ENOSYS is faithful, a silent trap never is. setjmp
 * and longjmp have no error return in POSIX, so the faithful failure here is a
 * DIAGNOSED abort, not a recoverable one -- but it must name the capability
 * and the errno so the refusal is legible at the point it happens. */

/* Direct write to fd 2. This TU is in libc-bottom-half and is reachable from
 * setjmp, so it cannot use stdio: stdio may itself be mid-teardown, and
 * pulling FILE machinery in here would add a startup dependency to every
 * module that links setjmp. */
static void __fbx_stack_diag_write(const char *s, size_t n) {
  __wasi_ciovec_t iov;
  __wasi_size_t nwritten = 0;
  iov.buf = (const uint8_t *)s;
  iov.buf_len = (__typeof__(iov.buf_len))n;
  /* Best effort: if the diagnostic itself cannot be written there is nothing
   * further to fall back to, and the abort below is still better than a bare
   * trap. */
  (void)__wasi_fd_write(2, &iov, 1, &nwritten);
}

#define __FBX_STACK_DIAG_STR(s) __fbx_stack_diag_write((s), sizeof(s) - 1)

static void __fbx_stack_diag_tail(void) {
  __FBX_STACK_DIAG_STR(
      " -- the WASIX stack-snapshot setjmp/longjmp mechanism is unavailable in "
      "this module: it needs asyncify instrumentation the module was not built "
      "with. Aborting rather than trapping undiagnosed.\n");
}

/* Refusal WITH an errno from the host (stack_checkpoint returns one). */
static void __fbx_stack_diag_errno(const char *what, size_t what_len,
                                   __wasi_errno_t err) {
  char num[12];
  size_t i = sizeof num;
  unsigned v = (unsigned)err;

  __fbx_stack_diag_write(what, what_len);
  __FBX_STACK_DIAG_STR(": errno=");
  do {
    num[--i] = (char)('0' + (v % 10u));
    v /= 10u;
  } while (v != 0 && i != 0);
  __fbx_stack_diag_write(num + i, sizeof num - i);
  __fbx_stack_diag_tail();
}

/* Refusal with NO errno available. `stack_restore` is declared to return
 * nothing at all, so when it returns anyway there is no host error code to
 * report -- saying `errno=0` would be inventing one. */
static void __fbx_stack_diag_bare(const char *what, size_t what_len) {
  __fbx_stack_diag_write(what, what_len);
  __FBX_STACK_DIAG_STR(" returned instead of restoring the stack");
  __fbx_stack_diag_tail();
}

int __wasilibc_setjmp(__wasi_stack_snapshot_t * buf) {
  uint64_t ret = 0;
  __wasi_errno_t err = __wasi_stack_checkpoint(buf, &ret);
  if (err != 0) {
    __fbx_stack_diag_errno("setjmp: stack_checkpoint",
                           sizeof "setjmp: stack_checkpoint" - 1, err);
    abort();
  }
  return (int)ret;
}

/* `__wasi_stack_restore` is declared `_Noreturn`, so a plain call followed by
 * recovery code is dead to the optimizer -- it is entitled to delete anything
 * after it, which is exactly how the undiagnosed `unreachable at
 * __wasi_stack_restore` frame arose. Route the call through a volatile
 * function pointer whose type carries no `_Noreturn`: `_Noreturn` is a
 * function SPECIFIER in C11, not part of the function type, so the cast is
 * well defined and the indirect call is opaque enough that the diagnostic
 * below survives. */
static void (*const volatile __fbx_stack_restore)(
    const __wasi_stack_snapshot_t *, uint64_t) =
    (void (*)(const __wasi_stack_snapshot_t *, uint64_t))__wasi_stack_restore;

_Noreturn void __wasilibc_longjmp(__wasi_stack_snapshot_t * buf, int val) {
  if (val == 0) {
    val = 1;
  }
  __fbx_stack_restore(buf, (uint64_t)val);
  /* Reached only when the host refused the restore. */
  __fbx_stack_diag_bare("longjmp: stack_restore",
                        sizeof "longjmp: stack_restore" - 1);
  abort();
}
