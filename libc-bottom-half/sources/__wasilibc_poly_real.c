#include <wasi/api.h>
#include <string.h>

#ifdef _REENTRANT
// firebox#796: the `wasi.thread-spawn` host import returns a thread id (a fixed
// i32 tid / -errno; see wasmer `thread_spawn<M> -> i32`), NOT a pointer-sized
// value. The arg (start_arg pointer) is `size_t` = pointer-width (i64 on wasm64,
// i32 on wasm32). The RETURN must be int32_t, else the wasm64 guest declares an
// i64 result while the host gives i32 → `Instance::new` "incompatible import
// type". int32_t == size_t on wasm32, so this is byte-identical there.
int32_t __imported_wasi_thread_spawn(size_t arg0) __attribute__((
    __import_module__("wasi"),
    __import_name__("thread-spawn")
));

size_t __wasi_thread_spawn(void* start_arg) {
    return __imported_wasi_thread_spawn((size_t) start_arg);
}
#endif