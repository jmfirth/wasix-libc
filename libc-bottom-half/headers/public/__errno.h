#ifndef __wasilibc___errno_h
#define __wasilibc___errno_h

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__PIC__) && !defined(__PIE__) && !defined(__WASILIBC_BUILDING_LIBC)
/* firebox#7GZ: shared-library CONSUMER path -- reach errno through the
 * function, never through a data symbol of either storage class.
 *
 * WHY (the defect this closes, MEASURED in #7GZ/#D8T): this branch used to say
 * `extern int errno;` -- a NON-TLS data extern, which clang lowers to
 * R_WASM_GLOBAL_INDEX_LEB (GOT.mem.errno). That is correct only when the libc
 * providing errno is a shared object. But a sysroot shelf ships libc.a (TLS
 * errno) AND libc.so (the GOT-importable one) behind ONE libc++.a, and the
 * storage class here is keyed on a COMPILE flag (-fPIC non-PIE) while the
 * property it means -- "my libc provider is a shared object" -- is a LINK-time
 * fact. No value of that flag is correct for both consumers, so the archive was
 * UNSATISFIABLE, not merely misconfigured.
 *
 * What that cost a user (MEASURED, no Firebox-specific flag anywhere):
 * ordinary `clang++ -std=c++17 --sysroot=/opt/wasix-sysroot-pic` code calling
 * std::filesystem::file_size() on a missing path answered
 * `ec=58 "Not supported"` instead of `ec=44 ENOENT`. wasm-ld folds the GOT
 * entry to errno's TLS-block OFFSET read as an absolute address, so the guest
 * reads whatever junk sits at that low address -- 58 here, 0 in #D8T's probe.
 * Any oracle asserting `errno == 0` misses this.
 *
 * WHY NOT -fPIE on the consumer (Option A, REJECTED -- do not re-derive): it
 * fixes the static main and BREAKS the `-shared` C++ link outright:
 *   wasm-ld: error: libc++.a(string.cpp.o): relocation
 *     R_WASM_MEMORY_ADDR_TLS_SLEB cannot be used against non-TLS symbol `errno`
 * MEASURED 2×2 on real libc++ archives: -fPIC fails arm 1, -fPIE fails arm 2,
 * this form holds BOTH. `images/perl-dev/Fireboxfile:59,61,94` installs clang +
 * wasix-libc-dev-pic and sets CXX=clang++, so arm 2 is a shipped user command.
 *
 * WHY THIS IS POLARITY-AGNOSTIC: __errno_location() executes INSIDE the libc
 * that owns errno and returns the calling thread's own cell by construction --
 * the identical argument libc-bottom-half/mman/mman.c already carries for
 * firebox#6ZJ. MEASURED: it is defined in 21 of 21 shelf libc.a and exported
 * `T` from the shipped libc.so (2010 exports). The reloc becomes
 * R_WASM_FUNCTION_INDEX_LEB; no `errno` symbol is referenced at all.
 *
 * Cost: one indirect call per errno access on this path.
 *
 * ⛔ CO-MARKER, not GOT=0: scripts/lib/errno-reloc-gate.sh prints
 * `OK -- errno model=TLS; TLS=0 DATA=0 GOT=0` on a CORRECTLY fixed archive --
 * byte-identical in meaning to an archive that never mentions errno. The 98 GOT
 * relocs convert 1:1 into 98 __errno_location references, so the
 * __errno_location COUNT is the entire discriminator. Assert it, never a bare
 * GOT=0. Fixing that fail-open is firebox#JYR.
 *
 * RETIREMENT: retires when upstream wasi-libc/wasix-org stops keying errno's
 * storage class on a compile flag -- i.e. when <errno.h> uses the function form
 * unconditionally, as musl's own libc-top-half/musl/include/errno.h:16-17
 * already does behind __wasilibc_unmodified_upstream. This branch is then
 * deleted entirely, not re-pointed. */
int *__errno_location(void);
#define errno (*__errno_location())
#else

#if defined(__FIREBOX_NO_TLS_ERRNO__)
/* firebox#323: static wasix-libc variant — errno is built non-TLS to
 * match consumers (zeroperl, Ruby, coreutils) that compile without
 * -pthread. See errno.c for the full rationale. */
extern int errno;
#else
#ifdef __cplusplus
extern thread_local int errno;
#else
extern _Thread_local int errno;
#endif
#endif

/* C requires errno to be a macro; the object-like self-define satisfies
 * `#ifdef errno` without changing the lvalue. The PIC-consumer branch above
 * already defines errno as an expression, so this must NOT be reached there --
 * a second, non-identical #define is a hard redefinition error. */
#define errno errno

#endif

#ifdef __cplusplus
}
#endif

#endif
