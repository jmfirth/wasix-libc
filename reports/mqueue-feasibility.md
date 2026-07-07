# POSIX mqueue (`build:no-posix-mqueue`) — feasibility verdict & authored subset

Task **#KS9** · branch `firebox-kg-mqueue` off `firebox-patches@7994aa2` · Suite-1 (Open POSIX).

## TL;DR

- **Root cause:** wasix-libc *has* musl's `src/mq/*.c` and `mqueue.h`, but the mq
  sources were excluded from the build (they call the Linux `mq_*` syscalls +
  the kernel `mqueue` fs, which WASIX lacks), so the whole family linked as
  `undefined symbol: mq_open`. `MQ_PRIO_MAX` was additionally `#ifdef
  __wasilibc_unmodified_upstream`-gated out of `<limits.h>`.
- **Fix authored (guest-only, faithful):** a real named-queue registry inside
  guest libc — priority-ordered storage, blocking send/receive over the
  `__timedwait`/`__wake` futex primitives (which already restore the Linux
  `futex==EINTR`/`ETIMEDOUT` contract on Firebox, #G13/#NSL), O_NONBLOCK→EAGAIN,
  timeouts→ETIMEDOUT, EMSGSIZE, mq_notify. Both widths compile clean.
- **Feasibility split (per width; 130 mqueue-family rows = 121 `build:no-posix-mqueue`
  + 9 `build:other` MQ_PRIO_MAX):**
  - **114 guest-feasible** (single-process, incl. single-mutator-fork and
    multi-thread) — authored now; expected to link + flip.
  - **16 cross-process** (genuine IPC rendezvous across a fork boundary) —
    **deferred to a host mq surface**. These now *link* (symbols present) but
    their cross-process assertions cannot be met by guest-only state; they must
    be **re-annotated**, not counted as regressions (see §4).

## 1. Why guest-only is faithful for single-process, and why cross-process is not

Firebox `fork()` is a faithful copy-on-write of guest linear memory. A
process-global static registry in guest libc is therefore:

- **Shared across threads** of one process → multi-threaded queue use is fully
  faithful (one mutex + per-queue futex seq-counters).
- **Copied, not shared, across `fork()`** → a child gets its own COPY of the
  registry as of fork time. This is *correct* for inheriting already-enqueued
  messages (a real kernel queue's pre-fork contents are visible to the child),
  but a queue *mutation* made by one side after fork is invisible to the other.

On Linux, cross-process mqueue coherence comes from the queue living **in the
kernel** (the `mqueue` fs), above any single process. The faithful Firebox analog
is the **host** (the wasmer runtime that owns the whole guest process tree) — the
kernel's role. So cross-process mqueue is **native-only-feasible / feasible-with-host-support**
(declared headroom, per the capability-profiles 4-tier model), **not
truly-impossible** and **not** something to fake in guest libc (Invariant 0:
partial-but-faithful > whole-but-fake).

## 2. What was authored

Model: a fixed table of named queues + a fixed descriptor table; `mqd_t` is a
descriptor index. A single `pthread_mutex` guards the registry. Blocking is the
classic futex condition-variable pattern — snapshot a per-queue sequence counter
under the lock, drop the lock, `__timedwait` on it; the counterpart bumps the
counter (`a_inc`) under the lock and `__wake`s it, so a wake racing the park is
not lost. `__timedwait` surfaces EINTR (signal) and ETIMEDOUT faithfully.

Semantics implemented to Linux errno/return:
`mq_open` (defaults maxmsg=10/msgsize=8192; O_CREAT/O_EXCL→EEXIST; !O_CREAT→ENOENT;
attr maxmsg/msgsize≤0→EINVAL; name>NAME_MAX→ENAMETOOLONG), `mq_close`/`mq_unlink`
(refcount + unlink-postpone-until-last-close), `mq_getattr`/`mq_setattr` (only
O_NONBLOCK mutable), `mq_timedsend`/`mq_timedreceive` (EMSGSIZE, prio≥MQ_PRIO_MAX
→EINVAL, O_NONBLOCK→EAGAIN, block→ETIMEDOUT/EINTR, priority-ordered FIFO-within-
priority), `mq_send`/`mq_receive` (musl's existing infinite-timeout wrappers),
`mq_notify` (SIGEV_SIGNAL/SIGEV_NONE/SIGEV_THREAD; EBUSY if registered; NULL
deregisters; EBADF on bad desc; fires once on empty→nonempty with no blocked
receiver, then deregisters).

### Files touched (all in the wasix-libc fork)

| File | Change |
|---|---|
| `libc-top-half/musl/src/mq/mq_impl.h` | **new** — private API + rationale header |
| `libc-top-half/musl/src/mq/mq_impl.c` | **new** — the registry + `__fbx_mq_*` bodies (whole file under `#ifndef __wasilibc_unmodified_upstream`) |
| `libc-top-half/musl/src/mq/mq_open.c` | delegate to `__fbx_mq_open` under wasix guard (upstream syscall body kept) |
| `libc-top-half/musl/src/mq/mq_close.c` | delegate to `__fbx_mq_close` |
| `libc-top-half/musl/src/mq/mq_setattr.c` | delegate to `__fbx_mq_getsetattr` (getattr already delegates to setattr) |
| `libc-top-half/musl/src/mq/mq_unlink.c` | delegate to `__fbx_mq_unlink` |
| `libc-top-half/musl/src/mq/mq_timedsend.c` | delegate to `__fbx_mq_timedsend` |
| `libc-top-half/musl/src/mq/mq_timedreceive.c` | delegate to `__fbx_mq_timedreceive` |
| `libc-top-half/musl/src/mq/mq_notify.c` | delegate to `__fbx_mq_notify` (netlink helper + body guarded upstream-only) |
| `libc-top-half/musl/include/limits.h` | unguard `MQ_PRIO_MAX 32768` (was `#ifdef __wasilibc_unmodified_upstream`) |
| `Makefile` | add `$(wildcard $(LIBC_TOP_HALF_MUSL_SRC_DIR)/mq/*.c)` to `LIBC_TOP_HALF_MUSL_SOURCES` |

`mq_getattr.c`, `mq_send.c`, `mq_receive.c` are **unchanged** (pure wrappers with
no syscalls — they work under both arms).

## 3. Compile-check (author-only; NO full build — orchestrator owns rebuild/rebaseline)

`clang -c` against the frozen wasm32 sysroot (`scripts/wasix-libc/sysroot-patched-pic`)
and wasm64 sysroot (`../firebox-forks/wasix-libc/sysroot`), threaded posture
(`-matomics -mbulk-memory -mmutable-globals -pthread`), musl-internal include layout.
**All 11 mq files compile clean on BOTH widths** (a stock control file, `qsort.c`,
compiled identically alongside to prove the invocation is sound). `MQ_PRIO_MAX==32768`
independently verified from the edited worktree `<limits.h>`. Remaining diagnostics
are pre-existing musl `endian.h` `-Wbitwise-op-parentheses` warnings, not from mq.

## 4. Cross-process rows to RE-ANNOTATE (needs a host mq surface — do NOT count as regressions)

These 16 stems × 2 widths = **32 baseline rows** will move BUILDFAIL→(FAIL|TIMEOUT)
once the symbols link. They are **not** flips and **not** regressions — they are a
distinct known-gap. Suggested detail label: **`runtime:cross-process-mqueue-needs-host`**.

| Stem | Cross-process reason |
|---|---|
| `mq_open/2-1`, `7-2`, `8-2`, `9-2` | child re-opens the queue BY NAME to rendezvous with parent's post-fork queue/messages |
| `mq_open/16-1` | O_CREAT\|O_EXCL race across two processes (both succeed in own copy) |
| `mq_close/2-1` | child opens parent's queue by name after fork |
| `mq_send/5-1` | child sends, parent receives (bidirectional on inherited desc) |
| `mq_receive/5-1` | parent receives what child sends |
| `mq_timedreceive/5-1` | blocks until the OTHER process enqueues |
| `mq_timedsend/5-1` | child fills+blocks, parent receives to unblock |
| `mq_unlink/2-1`, `2-2` | child holds a live reference to parent's queue (unlink-postpone across procs) |
| `mq_notify/2-1`, `9-1` | cross-process notify registration / EBUSY |
| `mq_notify/5-1` | parent registers, child blocks in receive (notify-suppression across procs) |
| `mqueues/send_rev_1` | fork; parent/child rendezvous over the queue |

### Host surface required (for the follow-up host task)

A minimal host-owned named-queue table addressable by all guest instances in a
process tree, with guest imports roughly:

- `mq_host_open(name, flags, mode, maxmsg, msgsize) -> handle|errno`
- `mq_host_close(handle)`, `mq_host_unlink(name)`
- `mq_host_timedsend(handle, ptr, len, prio, abstime) -> 0|errno` (blocking + EINTR)
- `mq_host_timedreceive(handle, ptr, len, prio_out, abstime) -> len|errno`
- `mq_host_getsetattr(handle, new, old)`
- `mq_host_notify(handle, sigevent/pid)` + a host→guest signal delivery path

i.e. the guest libc bodies authored here, re-pointed at host state instead of the
static registry. The guest registry stays as the fast path / single-process impl;
cross-process falls through to the host handle. This mirrors how Linux keeps the
queue in the kernel while the libc wrappers are thin.

## 5. Guest-feasible rows — confidence

**High confidence (pure single-process, no runtime-timing dependency; ~100/width):**
all `mq_open` error cases, `mq_getattr`/`mq_setattr`, `mq_close`/`mq_unlink` basics,
`mq_send`/`mq_receive` basic + priority + EMSGSIZE + O_NONBLOCK EAGAIN, `mq_notify`
single-process (1-1, 3-1, 4-1, 8-1), and the 9 `MQ_PRIO_MAX` range tests (bonus:
were `build:other`, fixed by the `<limits.h>` unguard).

**Doubtful — guest-feasible but flip depends on Firebox runtime behavior I could not
exercise here (no run allowed); ~14/width:**
`mq_send/5-2`, `mq_send/12-1`, `mq_receive/13-1` (EINTR-on-signal of a parked send/
receive), `mq_timedreceive/5-2`, `5-3`, `8-1`, `18-1`, `18-2`, `mq_timedsend/5-2`,
`5-3`, `16-1` (ETIMEDOUT precision + block-then-killed watchdog), `mq_timedsend/12-1`
+ `functional/mqueues/send_rev_2` (multi-thread), `fork/19-1` (child receives a
pre-fork message from its COW copy — depends on fork faithfully copying libc static
state). Firebox HAS the machinery for all of these (G13/#NSL EINTR restoration,
futex timeouts, COW fork), so these are *expected* to flip, but confirm at rebaseline.

## 6. forks.md §5 ledger row (orchestrator to add in MAIN repo — I must not edit MAIN)

> **wasix-libc — guest POSIX mqueue (`src/mq/*` + `mq_impl.{h,c}`, `limits.h` MQ_PRIO_MAX).**
> WHY: upstream mq is Linux-syscall/kernel-mqueue-fs only; WASIX lacks it, so the
> whole family was `undefined symbol`. Authored a faithful guest named-queue
> registry for the single-process subset (#KS9). RETIREMENT: retire the guest impl
> only if wasix gains a native mq syscall surface AND upstream wasix-libc adopts it;
> otherwise this is permanent fork carry. The **cross-process** subset is a
> SEPARATE follow-up (host mq surface) — file that host task before closing the
> capability-profiles mqueue tier.
