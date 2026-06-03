/*
 * Thread pointer access for wasm64.
 *
 * WHY this matches arch/wasm32 verbatim (firebox#796):
 * The original wasm64 hand-port read the thread pointer with inline asm
 * `global.get __wasilibc_pthread_self`. But __wasilibc_pthread_self is a
 * _Thread_local DATA variable (defined in src/thread/pthread_self.c), not a
 * wasm global. lld resolves the R_WASM_GLOBAL_INDEX_LEB reloc against that
 * data symbol by writing its TLS *offset* (e.g. 53752) as the global index,
 * yielding `global.get 53752` against a module with 8 globals → validation
 * failure ("global index out of bounds") at instantiation. The `int val`
 * also truncated the 64-bit thread pointer. wasm64 was scaffolded but never
 * linked end-to-end, so this stayed latent. The TLS variable is read the
 * same way on both widths (the compiler emits a __tls_base-relative load),
 * so the wasm32 form is correct and width-agnostic here.
 */
extern _Thread_local struct __pthread *__wasilibc_pthread_self;

static inline uintptr_t __get_tp() {
  return (uintptr_t)__wasilibc_pthread_self;
}

static inline void __set_tp(uintptr_t p) {
  __wasilibc_pthread_self = (struct __pthread *)p;
}
