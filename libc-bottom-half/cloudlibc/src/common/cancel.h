// firebox#TWX — the bottom half's cancellation-point bridge.
//
// POSIX XSH 2.9.5 makes read/write/readv/writev cancellation points. musl gets
// this for free because every blocking call funnels through `syscall_cp`, which
// brackets the raw syscall in one place. cloudlibc has NO such funnel: each
// function calls its `__wasi_*` import directly (MEASURED: `read.o` imports
// exactly `__wasi_fd_read`, `errno`, `__stack_pointer`). So the MECHANISM
// centralises here and the CALL SITES stay per-function — inventing a choke
// point would cost the same edits plus an indirection in a hot path.
//
// ⛔ THE WEAK DECLARATION IS LOAD-BEARING, NOT STYLE. `__testcancel` is defined
// in libc-top-half/musl/src/thread/pthread_cancel.c, which the single-threaded
// libc build does not compile. A strong reference from `read.o` breaks that
// build at LINK time — in a variant nobody is testing when they test this. Weak
// + address guard resolves to NULL and compiles out. This is the same idiom
// `time/clock_nanosleep.c` already uses, and it is the only other bottom-half
// file that touches cancellation.
//
// ⭐ The PTHREAD_CANCEL_DISABLE bracket is handled INSIDE `__testcancel`
// (pthread_cancel.c: `self->cancel && self->canceldisable != PTHREAD_CANCEL_DISABLE`).
// Do NOT re-check it at a call site — that duplicates a predicate into a second
// place where it can silently drift from the definition.
// ⚠️ That file contains TWO `__testcancel` definitions with DIFFERENT conditions;
// the second sits inside `#ifdef __wasilibc_unmodified_upstream` and is dead.
#ifndef COMMON_CANCEL_H
#define COMMON_CANCEL_H

// Self-contained: `__wasi_errno_t` and `__WASI_ERRNO_INTR` are used below.
#include <wasi/api.h>

__attribute__((__weak__)) void __testcancel(void);

// A cancellation point that has NOT blocked yet: observe an already-pending
// cancel rather than parking with it set.
static inline void __cloudlibc_testcancel(void) {
  if (&__testcancel != 0)
    __testcancel();
}

// After a call returned: act on a pending cancel ONLY when the call was
// interrupted without completing.
//
// ⛔ This condition is the whole correctness of the post-call test, and it is
// musl's own rule (`__syscall_cp_c`: `if (r==-EINTR && nr!=SYS_close && ...)`).
// An UNCONDITIONAL test here would discard bytes a completed `read` already
// consumed out of the pipe — data loss that no arm would report, because the
// thread is unwinding and nobody is left to notice the short count.
//
// The host side delivers exactly this: an interrupted await returns
// `Errno::Intr` (wasmer `syscalls/mod.rs`, `__asyncify`'s `SignalPoller::poll`
// → `Poll::Ready(Ok(Err(Errno::Intr)))`).
//
// ⛔ firebox#87F — COMPARE IN HOST SPACE, `__WASI_ERRNO_INTR` AND NOT `EINTR`.
// `error` is the code the host just handed back, untranslated; the parameter
// type says so. `EINTR` is the GUEST's name for interruption, and the two are
// the same integer only for as long as the guest numbering IS the WASI
// numbering. When step 4 moves E* onto Linux uapi, guest EINTR becomes 4 while
// WASI INTR stays 27, this comparison stops firing, and `pthread_cancel` on a
// blocking read silently stops working — no error, no errno, no trap, at all 19
// sites this header is inlined into (read, write, pread, pwrite, readv, writev,
// preadv, pwritev, recv, recvmsg ×2, send, sendmsg, accept, openat, poll,
// pselect, fsync, fdatasync). A fail-open in the cancellation path is the worst
// shape of bug this series can leave behind, which is why this one line is
// converted by itself.
//
// Compared, NOT translated: translating `error` to compare it against EINTR
// would be a function call in a path taken after every one of those calls, to
// reach the same answer.
static inline void __cloudlibc_testcancel_if_intr(__wasi_errno_t error) {
  if (error == __WASI_ERRNO_INTR && &__testcancel != 0)
    __testcancel();
}

#endif
