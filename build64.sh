#!/bin/bash
#
# firebox#796/#797: deterministic wasm64 WASI header regeneration (MAINTENANCE tool).
#
# This is NOT part of the per-build path. The committed
#   libc-bottom-half/headers/public/wasi/api_wasi.h
#   libc-bottom-half/headers/public/wasi/api_wasix.h
#   libc-bottom-half/sources/__wasilibc_real.c
#   libc-bottom-half/sources/__wasixlibc_real.c
# are #if-wrapped DUAL-TARGET (wasm32 in #else, wasm64 in #if defined(__wasm64__)),
# so a plain `make TARGET_ARCH=wasm32|wasm64` builds either sysroot from committed
# source with NO regen. Run THIS script only to refresh the wasm64 half after a witx
# change, then RE-APPLY the #if-wrap to those 4 files
# (see work/tasks/797-* + work/tasks/796-*/reports/2026-06-03-wasm64-sysroot-
# reproducibility-addendum.md).
#
# firebox#800: the 7 Firebox WASIX extension fns (fd_lock / fd_lock_range / fd_chmod /
# path_chmod / path_lchmod / path_mknod / fd_ioctl) NO LONGER live in the regenerated
# files — they were extracted to the committed, firebox-owned
# libc-bottom-half/sources/__wasixlibc_firebox.c + headers/public/wasi/api_firebox.h.
# So a regen + #if-wrap is now CLEAN: wasm64-overlay.sh is RETIRED (do not re-apply it).
# The router api.h below includes api_firebox.h so callers see the decls.
#
# DETERMINISM (vs the old `git reset --hard` + `git pull origin main`, which both
# discarded local state and injected upstream non-determinism):
#   - The WASI submodules are PINNED. We never reset/pull them.
#   - The pointer-width `$size -> usize` widening — so the wasix_64v1 import args
#     match the wasmer Memory64 host's `M::Offset` (e.g. sock_listen `$backlog` = i64
#     on wasm64; still i32 on wasm32, identical to the old u32) — lives in committed
#     patches under patches/wasm64/, applied to a CLEAN checkout of each submodule's
#     typenames and reverted on exit (clean tree, byte-repeatable output).
#
# REQUIREMENTS:
#   - CC must point at the wasi-sdk clang (Apple clang can't target wasm64).
#   - `sed` portability: the in-place edits below use the `-i.bak` backup-suffix
#     form (then drop the `.bak`), which is the one `sed -i` spelling accepted by
#     BOTH GNU sed and BSD/macOS sed. No GNU-sed shim is required (the prior
#     "must be GNU sed / link gsed onto PATH" workaround is gone — firebox#5RE);
#     run directly on macOS: CC=$WASI_SDK/bin/clang bash build64.sh

set -Eeuxo pipefail

export TARGET_ARCH=wasm64
export TARGET_OS=wasix

REPO="$(cd "$(dirname "$0")" && pwd)"
cd "$REPO"

WASIX_SUB=tools/wasix-headers/WASI
WASI_SUB=tools/wasi-headers/WASI
TN=phases/snapshot/witx/typenames.witx

# Always leave the pinned submodules clean, even on failure.
restore_submodules() {
  git -C "$WASIX_SUB" checkout -- "$TN" 2>/dev/null || true
  git -C "$WASI_SUB"  checkout -- "$TN" 2>/dev/null || true
  # firebox#824: also revert the epoll maxevents witx retype (see below).
  git -C "$WASIX_SUB" checkout -- phases/snapshot/witx/wasix_v1.witx 2>/dev/null || true
}
trap restore_submodules EXIT

# Apply $size->usize to a clean checkout of each pinned submodule's typenames.
git -C "$WASIX_SUB" checkout -- "$TN"
git -C "$WASI_SUB"  checkout -- "$TN"
git -C "$WASIX_SUB" apply "$REPO/patches/wasm64/wasix-typenames-size-usize.patch"
git -C "$WASI_SUB"  apply "$REPO/patches/wasm64/wasi-typenames-size-usize.patch"

# firebox#824: epoll_wait's $maxevents is witx-typed $size, so the $size->usize
# widening above would lower it to i64 — but the wasmer Memory64 HOST deliberately
# pins maxevents to a fixed i32 (an upstream-wasmer ABI choice the wasix Rust crate
# also honors). Retype just that one param to a fixed u32 so the generated wasix_64v1
# epoll_wait import matches the host (i32,i64,i32,i64,i64) instead of conflicting at
# (i32,i64,i64,...). Targets wasix_v1.witx (not typenames.witx), reverted below.
WASIX_WITX=phases/snapshot/witx/wasix_v1.witx
git -C "$WASIX_SUB" checkout -- "$WASIX_WITX"
git -C "$WASIX_SUB" apply "$REPO/patches/wasm64/wasix-epoll-maxevents-fixed-i32.patch"

# firebox#824 (WALL TAKE-2): $function_pointer is witx-typed `usize`, so the
# $size->usize widening lowers every function-TABLE index (call_dynamic/closure_*/
# reflect_signature/context_create) to i64 — but the wasmer Memory64 HOST hand-codes
# each as a fixed `u32`->i32 (a table index never needs 64 bits; same flavor as the
# epoll_wait.maxevents outlier above, and matching the wasix-0.13 crate fix in
# scripts/build-rust-stage0/run.sh). Without this, context_create's C binding (the
# one wasix-libc binding rustc.wasm actually links from this family) imports as
# (i64,i64) and the host rejects it ((i64,i32)). Retypes the 6 function-id PARAMS
# to u32; the closure_allocate RESULT stays $function_pointer (a real retptr).
# Same apply-on-clean / revert-in-trap idiom as the epoll patch.
git -C "$WASIX_SUB" apply "$REPO/patches/wasm64/wasix-function-pointer-fixed-u32.patch"

# --- wasix surface (feeds api_wasix.h / the wasix_64v1 imports) ---
cargo run --manifest-path tools/wasix-headers/Cargo.toml generate-libc --64bit
cp -f libc-bottom-half/headers/public/wasi/api.h libc-bottom-half/headers/public/wasi/api_wasix.h
sed -i.bak 's|__wasi__|__wasix__|g' libc-bottom-half/headers/public/wasi/api_wasix.h && rm -f libc-bottom-half/headers/public/wasi/api_wasix.h.bak
sed -i.bak 's|__wasi_api_h|__wasix_api_h|g' libc-bottom-half/headers/public/wasi/api_wasix.h && rm -f libc-bottom-half/headers/public/wasi/api_wasix.h.bak
cp -f libc-bottom-half/sources/__wasilibc_real.c libc-bottom-half/sources/__wasixlibc_real.c

# --- preview1 surface (feeds api_wasi.h) ---
mkdir -p build/temp
rsync -rtu --delete tools/wasix-headers/ build/temp
cp -r -f "$WASI_SUB"/phases/* build/temp/WASI/phases
mv -f build/temp/WASI/phases/snapshot/witx/wasi_snapshot_preview1.witx build/temp/WASI/phases/snapshot/witx/wasix_v1.witx
# Redundant now that $size itself is `usize` (these were the partial buf_len
# hand-fix the typename widening subsumes); kept as harmless belt-and-braces.
sed -i.bak 's|(field $buf_len $size)|(field $buf_len usize)|g' build/temp/WASI/phases/snapshot/witx/typenames.witx && rm -f build/temp/WASI/phases/snapshot/witx/typenames.witx.bak
sed -i.bak 's|(param $buf_len $size)|(param $buf_len usize)|g' build/temp/WASI/phases/snapshot/witx/wasix_v1.witx && rm -f build/temp/WASI/phases/snapshot/witx/wasix_v1.witx.bak
cargo clean --manifest-path build/temp/Cargo.toml
cargo run --manifest-path build/temp/Cargo.toml generate-libc --64bit
cp -f libc-bottom-half/headers/public/wasi/api.h libc-bottom-half/headers/public/wasi/api_wasi.h

# Router header (the generate step overwrites api.h; restore the include shim).
cat > libc-bottom-half/headers/public/wasi/api.h <<EOF
#include "api_wasi.h"
#include "api_wasix.h"
#include "api_firebox.h"
#include "api_poly.h"
EOF

# Regen-only: the libc build is a separate Makefile step (see the build recipes in
# work/tasks/796-* / the lifeline memory). NEXT after this regen: re-apply the
# #if-wrap to the 4 generated files (work/tasks/797).
echo "==> wasm64 WASI headers regenerated. NEXT: re-apply the #if-wrap (work/tasks/797)."
