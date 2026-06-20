# firebox#9PX — SA_ONSTACK frozen regression litmus

`sa_onstack_probe.c` is the frozen regression test for firebox#9PX (SA_ONSTACK
alternate-stack signal delivery). It is **stronger** than the Open POSIX
`sigaction/12-*` witnesses it was distilled from: those only assert that
`sigaltstack(NULL,&ss)` round-trips the registered `ss_sp`/`ss_size`, whereas
this probe also asserts the handler's **actual** stack pointer (the address of
a handler local) lies inside the registered alt-stack range — i.e. the wasm
shadow-stack pointer really switched — plus `SS_ONSTACK` reporting, the
mid-handler `EPERM`, and that a non-`SA_ONSTACK` handler does NOT switch.

## What it guards

If the `__wasm_signal` alt-stack switch (sigaction.c) or the per-thread
`sigaltstack(2)` state (sigaltstack.c / firebox_altstack.h) regress, this probe
exits non-zero with a specific failure code.

## Run (against a built sysroot)

```sh
CLANG=<wasi-sdk>/bin/clang
$CLANG --target=wasm32-wasip1 --sysroot=<sysroot> -O2 \
  -mthread-model posix -pthread -ftls-model=local-exec \
  -matomics -mbulk-memory -mmutable-globals -std=gnu11 -fno-builtin \
  -Wl,--shared-memory -Wl,--import-memory -Wl,--max-memory=4294967296 \
  sa_onstack_probe.c -o /tmp/sa_onstack_probe.wasm
firebox run /tmp/sa_onstack_probe.wasm    # PASS + exit 0 on a fixed sysroot
```

Validated 2026-06-19 on an isolated `/tmp` mirror-sysroot relink against the
inventory firebox binary: PASS on the patched sysroot, FAIL (rc=12, sp=0/size=0
round-trip) on the unpatched baseline — confirming it exercises the fix and is
not vacuous. The Open POSIX `sigaction/12-1..12-26` corpus is the broader
regression net (26 PASS patched / 26 FAIL baseline).
