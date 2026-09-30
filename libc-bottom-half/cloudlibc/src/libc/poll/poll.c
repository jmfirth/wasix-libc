// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <common/cancel.h>
#include <wasi/api.h>
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <wasi/libc.h>

// firebox#XD4 — does this entry ask for input or output readiness at all?
static bool wants_io(const struct pollfd *pollfd) {
  return (pollfd->events & (POLLRDNORM | POLLIN | POLLWRNORM | POLLOUT)) != 0;
}

int poll(struct pollfd *fds, size_t nfds, int timeout) {
  // firebox#XD4 — an entry whose `events` asks for NEITHER input nor output
  // (events == 0 — the idiom for parking an entry in a pollfd array — or a
  // POLLPRI-only request) used to fail the WHOLE call with ENOSYS. Linux polls
  // it for the conditions that are always reported: POLLHUP, POLLERR and
  // POLLNVAL, and nothing else.
  //
  // WASI has no "status only" subscription, so such an entry gets a STATUS
  // subscription on its readable side (its writable side for a write-only fd)
  // and only error/hangup results are kept for it. A status subscription that
  // merely reports READY is not reportable; that entry is DEMOTED (left out
  // for the rest of this call) and the poll re-issued with the time that
  // remains, so readiness nobody asked for can neither wake the caller nor
  // spin it.
  bool demoted[nfds ? nfds : 1];
  memset(demoted, 0, sizeof demoted);
  struct timespec started;
  if (timeout > 0)
    clock_gettime(CLOCK_MONOTONIC, &started);

  for (;;) {
  int remaining = timeout;
  if (timeout > 0) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long long elapsed = (long long)(now.tv_sec - started.tv_sec) * 1000 +
                        (now.tv_nsec - started.tv_nsec) / 1000000;
    remaining = elapsed >= timeout ? 0 : (int)(timeout - elapsed);
  }
  // Construct events for poll().
  size_t maxevents = 2 * nfds + 1;
  __wasi_subscription_t subscriptions[maxevents];
  size_t nsubscriptions = 0;
  for (size_t i = 0; i < nfds; ++i) {
    struct pollfd *pollfd = &fds[i];
    if (pollfd->fd < 0)
      continue;
    if (!wants_io(pollfd)) {
      if (demoted[i])
        continue;
      // firebox#XD4 — the STATUS subscription. A bad fd is answered by the
      // host per entry (EBADF event -> POLLNVAL), so it needs no special case.
      __wasi_eventtype_t tag = __WASI_EVENTTYPE_FD_READ;
      __wasi_fdstat_t st;
      if (__wasi_fd_fdstat_get(pollfd->fd, &st) == 0 &&
          (st.fs_rights_base & __WASI_RIGHTS_FD_READ) == 0 &&
          (st.fs_rights_base & __WASI_RIGHTS_FD_WRITE) != 0)
        tag = __WASI_EVENTTYPE_FD_WRITE;
      __wasi_subscription_t *subscription = &subscriptions[nsubscriptions++];
      *subscription = (__wasi_subscription_t){
          .userdata = (uintptr_t)pollfd,
          .u.tag = tag,
      };
      if (tag == __WASI_EVENTTYPE_FD_READ)
        subscription->u.u.fd_read.file_descriptor = pollfd->fd;
      else
        subscription->u.u.fd_write.file_descriptor = pollfd->fd;
      continue;
    }
    // POLLIN and POLLRDNORM are distinct on musl/wasix (0x001 vs 0x040)
    // but equivalent for TCP sockets, pipes, and regular files.  Callers
    // that set POLLIN (curl, Rust std) must see the same behaviour as
    // callers that set POLLRDNORM (POSIX equivalence).
    if ((pollfd->events & (POLLRDNORM | POLLIN)) != 0) {
      __wasi_subscription_t *subscription = &subscriptions[nsubscriptions++];
      *subscription = (__wasi_subscription_t){
          .userdata = (uintptr_t)pollfd,
          .u.tag = __WASI_EVENTTYPE_FD_READ,
          .u.u.fd_read.file_descriptor = pollfd->fd,
      };
    }
    if ((pollfd->events & (POLLWRNORM | POLLOUT)) != 0) {
      __wasi_subscription_t *subscription = &subscriptions[nsubscriptions++];
      *subscription = (__wasi_subscription_t){
          .userdata = (uintptr_t)pollfd,
          .u.tag = __WASI_EVENTTYPE_FD_WRITE,
          .u.u.fd_write.file_descriptor = pollfd->fd,
      };
    }
  }

  // Create extra event for the timeout.
  if (remaining >= 0) {
    // firebox#RDN — WASI relative zero is an immediate readiness check.
    __wasi_subscription_t *subscription = &subscriptions[nsubscriptions++];
    *subscription = (__wasi_subscription_t){
        .u.tag = __WASI_EVENTTYPE_CLOCK,
        .u.u.clock.id = __WASI_CLOCKID_REALTIME,
        .u.u.clock.timeout = (__wasi_timestamp_t)(remaining * 1000000LL),
    };
  }

  // firebox#RDN — a descriptor-free infinite wait must not emit an
  // expired CLOCK. Use the largest representable relative deadline, as
  // kernels clamp unbounded waits; real signals still interrupt this wait.
  if (nsubscriptions == 0) {
    __wasi_subscription_t *subscription = &subscriptions[nsubscriptions++];
    *subscription = (__wasi_subscription_t){
        .u.tag = __WASI_EVENTTYPE_CLOCK,
        .u.u.clock.id = __WASI_CLOCKID_MONOTONIC,
        .u.u.clock.timeout = UINT64_MAX,
    };
  }

  // Execute poll().
  __wasi_size_t nevents;
  __wasi_event_t events[nsubscriptions];
  // firebox#TWX — POSIX XSH 2.9.5 cancellation point. Observe an
  // already-pending cancel BEFORE parking in the host await.
  __cloudlibc_testcancel();

  __wasi_errno_t error =
      __wasi_poll_oneoff(subscriptions, events, nsubscriptions, &nevents);
  if (error != 0) {
    // firebox#TWX — a cancel that arrived while we were parked. Keyed on
    // EINTR (musl's `__syscall_cp_c` rule, pthread_cancel.c:92) so a COMPLETED
    // call never discards what it already consumed. Never returns if a cancel
    // is pending and enabled.
    __cloudlibc_testcancel_if_intr(error);
    // `nsubscriptions` is now never 0, so the host's empty-list `EINVAL` is
    // unreachable from here and there is nothing left to relabel. Report what
    // the runtime reported — in particular `EINTR`, which is how an indefinite
    // wait ends when a signal handler runs (signal(7): poll is never
    // restarted).
    errno = __wasilibc_errno_from_wasi(error);
    return -1;
  }

  // Clear revents fields.
  for (size_t i = 0; i < nfds; ++i) {
    struct pollfd *pollfd = &fds[i];
    pollfd->revents = 0;
  }

  // Set revents fields.
  bool clock_fired = false;
  bool newly_demoted = false;
  for (size_t i = 0; i < nevents; ++i) {
    const __wasi_event_t *event = &events[i];
    if (event->type == __WASI_EVENTTYPE_CLOCK)
      clock_fired = true;
    if (event->type == __WASI_EVENTTYPE_FD_READ ||
        event->type == __WASI_EVENTTYPE_FD_WRITE) {
      struct pollfd *pollfd = (struct pollfd *)(uintptr_t)event->userdata;
      if (!wants_io(pollfd) && event->error == 0 &&
          (event->fd_readwrite.flags & __WASI_EVENTRWFLAGS_FD_READWRITE_HANGUP) == 0) {
        // firebox#XD4 — a status subscription that only reports READY:
        // nothing the caller asked about. Demote it and poll again.
        demoted[pollfd - fds] = true;
        newly_demoted = true;
        continue;
      }
      if (!wants_io(pollfd) && event->error == 0) {
        // firebox#XD4 — status entry with a hangup: report only POLLHUP.
        pollfd->revents |= POLLHUP;
        continue;
      }
      if (event->error == __WASI_ERRNO_BADF) {
        // Invalid file descriptor.
        pollfd->revents |= POLLNVAL;
      } else if (event->error == __WASI_ERRNO_PIPE) {
        // Hangup on write side of pipe.
        pollfd->revents |= POLLHUP;
      } else if (event->error != 0) {
        // Another error occurred.
        pollfd->revents |= POLLERR;
      } else {
        // Data can be read or written.
        if (event->type == __WASI_EVENTTYPE_FD_READ) {
            pollfd->revents |= POLLRDNORM | POLLIN;
            if (event->fd_readwrite.flags & __WASI_EVENTRWFLAGS_FD_READWRITE_HANGUP) {
              pollfd->revents |= POLLHUP;
            }
        } else if (event->type == __WASI_EVENTTYPE_FD_WRITE) {
            pollfd->revents |= POLLWRNORM | POLLOUT;
            if (event->fd_readwrite.flags & __WASI_EVENTRWFLAGS_FD_READWRITE_HANGUP) {
              pollfd->revents |= POLLHUP;
            }
        }
      }
    }
  }

  // Return the number of events with a non-zero revents value.
  int retval = 0;
  for (size_t i = 0; i < nfds; ++i) {
    struct pollfd *pollfd = &fds[i];
    // POLLHUP contradicts with POLLWRNORM.
    if ((pollfd->revents & POLLHUP) != 0)
      pollfd->revents &= ~POLLWRNORM;
    if (pollfd->revents != 0)
      ++retval;
  }
  // firebox#XD4 — only a demotion with nothing to report and time left makes
  // another round; anything reportable, or the timeout, ends the call.
  if (retval == 0 && newly_demoted && !clock_fired)
    continue;
  return retval;
  }
}
