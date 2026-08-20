// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <common/time.h>
#include <pthread.h>

#include <sys/select.h>

#include <signal.h>
#include <wasi/api.h>
#include <errno.h>
#include <stdbool.h>

int pselect(int nfds, fd_set *restrict readfds, fd_set *restrict writefds,
            fd_set *restrict errorfds, const struct timespec *restrict timeout,
            const sigset_t *sigmask) {
  // Negative file descriptor upperbound.
  if (nfds < 0) {
    errno = EINVAL;
    return -1;
  }

  // This implementation does not support polling for exceptional
  // conditions, such as out-of-band data on TCP sockets.  Return zero
  // entries rather than failing — callers like curl pass errorfds to
  // every select() call by convention, and ENOSYS breaks the API.
  if (errorfds != NULL && errorfds->__nfds > 0) {
    FD_ZERO(errorfds);
  }

  // Replace NULL pointers by the empty set.
  fd_set empty;
  FD_ZERO(&empty);
  if (readfds == NULL)
    readfds = &empty;
  if (writefds == NULL)
    writefds = &empty;

  // Determine the maximum number of events.
  size_t maxevents = readfds->__nfds + writefds->__nfds + 1;
  __wasi_subscription_t subscriptions[maxevents];
  size_t nsubscriptions = 0;

  // Convert the readfds set.
  for (size_t i = 0; i < readfds->__nfds; ++i) {
    int fd = readfds->__fds[i];
    if (fd < nfds) {
      __wasi_subscription_t *subscription = &subscriptions[nsubscriptions++];
      *subscription = (__wasi_subscription_t){
          .userdata = (uintptr_t)(intptr_t)fd,
          .u.tag = __WASI_EVENTTYPE_FD_READ,
          .u.u.fd_read.file_descriptor = fd,
      };
    }
  }

  // Convert the writefds set.
  for (size_t i = 0; i < writefds->__nfds; ++i) {
    int fd = writefds->__fds[i];
    if (fd < nfds) {
      __wasi_subscription_t *subscription = &subscriptions[nsubscriptions++];
      *subscription = (__wasi_subscription_t){
          .userdata = (uintptr_t)(intptr_t)fd,
          .u.tag = __WASI_EVENTTYPE_FD_WRITE,
          .u.u.fd_write.file_descriptor = fd,
      };
    }
  }

  // Create extra event for the timeout.
  if (timeout != NULL) {
    __wasi_subscription_t *subscription = &subscriptions[nsubscriptions++];
    *subscription = (__wasi_subscription_t){
        .u.tag = __WASI_EVENTTYPE_CLOCK,
        .u.u.clock.id = __WASI_CLOCKID_REALTIME,
    };
    if (!timespec_to_timestamp_clamp(timeout, &subscription->u.u.clock.timeout)) {
      errno = EINVAL;
      return -1;
    }
  }

  // firebox#B28 — WAIT INDEFINITELY when nothing else was subscribed.
  //
  // `pselect(0, NULL, NULL, NULL, NULL, mask)` — the canonical "park this
  // thread until a signal arrives" idiom — produces zero subscriptions.
  // `__wasi_poll_oneoff` refuses an empty subscription list with `EINVAL`, and
  // the code below used to relabel that as `ENOTSUP` and return -1 in 0 ms
  // (MEASURED against the shipping sysroot, deterministic 3/3). Linux blocks.
  //
  // The upstream justification for that refusal — "Wasm has no signal
  // handling, so there would be no way for the poll to wake up" — is FALSE for
  // firebox, which ships per-thread signal delivery. It is "upstream doesn't"
  // wearing a platform bound's clothes, which invariant 0 calls a violation
  // rather than a bound.
  //
  // We do NOT need a synthetic short timeout (the earlier 10 ms form) and we do
  // NOT need a host change: the WASI ABI already encodes an indefinite wait as
  // a CLOCK subscription whose timeout is 0. MEASURED 2026-08-19 against the
  // UNMODIFIED shipping sysroot + runtime, deterministic 3/3, with its negative
  // control in the same process:
  //   timeout=1e9  -> err=0  nevents=1 after 1003 ms      (control: alive)
  //   timeout=0, alarm(2) armed
  //                -> err=27 (EINTR) nevents=0 after 2003 ms, handler ran
  //   timeout=0, nothing armed
  //                -> never returned; killed by the 8 s wrapper alarm
  // So timeout==0 is an indefinite wait that a delivered signal interrupts with
  // EINTR — exactly the POSIX contract for `pselect` (signal(7): select/pselect
  // are never restarted, they fail EINTR regardless of SA_RESTART).
  //
  // `timespec_to_timestamp_clamp` never yields 0 (a zero-length caller timeout
  // is spelled 1, "immediate"), so this value is unambiguously ours.
  if (nsubscriptions == 0) {
    __wasi_subscription_t *subscription = &subscriptions[nsubscriptions++];
    *subscription = (__wasi_subscription_t){
        .u.tag = __WASI_EVENTTYPE_CLOCK,
        .u.u.clock.id = __WASI_CLOCKID_MONOTONIC,
        .u.u.clock.timeout = 0,  // 0 == wait indefinitely (firebox#B28)
    };
  }

  // firebox#B28 — APPLY THE CALLER'S SIGMASK AROUND THE WAIT.
  //
  // POSIX: pselect atomically replaces the calling thread's blocked-signal mask
  // with *sigmask for the duration of the wait, then restores it. Without this
  // the parameter is accepted and silently discarded, which is FAIL-OPEN: the
  // call returns, the program proceeds, and the race window the canonical
  // pattern exists to close (block SIGCHLD, test a flag, then pselect with the
  // signal unblocked) is simply left open, with no error and no symptom at the
  // call site. Invariant 0: an honest ENOSYS is faithful, a false success never
  // is; invariant 3: a fail-open defect admits no deferral.
  //
  // Concretely: a signal the caller blocked outside pselect stays blocked
  // during the wait, so `__wasm_signal()` (sigaction.c) finds it blocked, calls
  // `__wasm_pend_signal()` and returns WITHOUT running the user handler. Any
  // caller gating progress on a handler-set flag (ninja's `s_sigchld_received`,
  // and every self-pipe-trick loop) then waits forever or busy-loops.
  //
  // `pthread_sigmask`'s SIG_SETMASK path drains signals the new mask unblocks
  // (pthread_sigmask.c), so a signal that arrived while blocked OUTSIDE pselect
  // is re-raised and dispatched as soon as the mask is installed — the POSIX
  // "no missed wakeup" property. Restoring the saved mask on the way out drains
  // symmetrically.
  //
  // This restores commit ddd3b05, which landed 2026-05-01 23:41:36 and was
  // deleted 8 seconds later by 782b63d — a rebase/replay accident, not a
  // decision. 782b63d's own socket-fd work is wanted and is left intact.
  sigset_t saved_mask;
  bool mask_applied = false;
  if (sigmask != NULL) {
    if (pthread_sigmask(SIG_SETMASK, sigmask, &saved_mask) == 0) {
      mask_applied = true;
    }
  }

  // Execute poll().
  __wasi_size_t nevents;
  __wasi_event_t events[nsubscriptions];
  __wasi_errno_t error =
      __wasi_poll_oneoff(subscriptions, events, nsubscriptions, &nevents);

  // Restore the caller's mask before touching errno: pthread_sigmask's drain
  // can dispatch a handler, and a handler is entitled to clobber errno.
  if (mask_applied) {
    pthread_sigmask(SIG_SETMASK, &saved_mask, NULL);
  }

  if (error != 0) {
    // `nsubscriptions` is now never 0 (see the indefinite-wait block above), so
    // the host's empty-list `EINVAL` is unreachable from here and there is no
    // longer any case to relabel. Report what the runtime reported — in
    // particular `EINTR`, which is how an indefinite wait ends when a signal
    // handler runs.
    errno = error;
    return -1;
  }

  // Test for EBADF.
  for (size_t i = 0; i < nevents; ++i) {
    const __wasi_event_t *event = &events[i];
    if ((event->type == __WASI_EVENTTYPE_FD_READ ||
         event->type == __WASI_EVENTTYPE_FD_WRITE) &&
        event->error == __WASI_ERRNO_BADF) {
      errno = EBADF;
      return -1;
    }
  }

  // Build result sets from poll_oneoff events.
  FD_ZERO(readfds);
  FD_ZERO(writefds);
  for (size_t i = 0; i < nevents; ++i) {
    const __wasi_event_t *event = &events[i];
    if (event->type == __WASI_EVENTTYPE_FD_READ) {
      int fd = (int)(intptr_t)event->userdata;
      readfds->__fds[readfds->__nfds++] = fd;
    } else if (event->type == __WASI_EVENTTYPE_FD_WRITE) {
      int fd = (int)(intptr_t)event->userdata;
      writefds->__fds[writefds->__nfds++] = fd;
    }
  }
  return readfds->__nfds + writefds->__nfds;
}
