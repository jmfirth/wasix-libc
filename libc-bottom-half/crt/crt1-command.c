// firebox#CTX — `_start` is a STUB. It carries no libc behaviour.
//
// crt1 is linked into every executable, so for a PIC thin main whatever it
// contains is frozen into the consumer's bytes and can only be fixed by a
// relink (#R0A needed exactly that). The startup sequence therefore lives in
// libc (`__wasilibc_start_main`, libc-bottom-half/sources/__wasilibc_start_main.c)
// — imported from the shared provider by a thin main, pulled from libc.a by a
// static one — and `_start` passes it the only two things the executable alone
// owns: its linker-synthesized `__wasm_call_ctors`, and the `__main_void` it
// resolved at link time (the program's own, or libc's argv wrapper).
extern void __wasm_call_ctors(void);
extern int __main_void(void);
extern void __wasilibc_start_main(void (*call_ctors)(void), int (*main_void)(void));

__attribute__((export_name("_start")))
void _start(void) {
    __wasilibc_start_main(__wasm_call_ctors, __main_void);
}
