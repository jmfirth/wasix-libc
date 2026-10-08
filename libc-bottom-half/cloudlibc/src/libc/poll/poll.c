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
  return (pollfd->events & (POLLRDNORM | POLLIN | POLLWRNORM | POLLOUT | POLLRDHUP)) != 0;
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
  // merely reports READY is not reportable; that entry is DEMOTED and the poll
  // re-issued with the time that remains, so readiness nobody asked for can
  // neither wake the caller nor spin it.
  //
  // Review F3: demotion is for ONE WAIT SLICE, not for the rest of the call.
  // A demoted entry is still owed POLLHUP/POLLERR, which Linux reports however
  // long the call waits; dropped for good, a pipe that held data and then lost
  // its writer slept out the whole timeout (or forever). While any entry is
  // demoted, each wait is capped at a slice and the status entries are probed
  // again when it ends. The slice doubles from 1 ms to 64 ms, so a hangup is
  // seen within one slice and an idle wait costs at most ~30 wake-ups a
  // second. The cap is the cost of WASI having no hangup-only interest.
  bool demoted[nfds ? nfds : 1];
  memset(demoted, 0, sizeof demoted);
  bool any_demoted = false;
  // Review F3 (round 2): the LAST wait is capped by the timeout, not by a
  // slice, and demoted entries sit it out. Before such a wait may end the call
  // with 0, every demoted entry gets one more probe, so no path returns 0
  // without having asked about them after the last of the time ran out.
  bool final_probe_done = false;
  int slice = 1;
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
    if ((pollfd->events & (POLLRDNORM | POLLIN | POLLRDHUP)) != 0) {
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

  // Review F3: with a demoted entry the wait is one slice long, after which
  // the status entries are probed again. `sliced` records that the CLOCK below
  // is the slice, not the caller's timeout.
  int wait = remaining;
  bool sliced = false;
  if (any_demoted && (wait < 0 || wait > slice)) {
    wait = slice;
    sliced = true;
  }

  // Create extra event for the timeout.
  if (wait >= 0) {
    // firebox#RDN — WASI relative zero is an immediate readiness check.
    __wasi_subscription_t *subscription = &subscriptions[nsubscriptions++];
    *subscription = (__wasi_subscription_t){
        .u.tag = __WASI_EVENTTYPE_CLOCK,
        .u.u.clock.id = __WASI_CLOCKID_REALTIME,
        .u.u.clock.timeout = (__wasi_timestamp_t)(wait * 1000000LL),
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
      if (event->fd_readwrite.flags & __WASI_EVENTRWFLAGS_FD_READWRITE_ERROR)
        pollfd->revents |= POLLERR;
      if (!wants_io(pollfd) && event->error == 0 &&
          (event->fd_readwrite.flags & __WASI_EVENTRWFLAGS_FD_READWRITE_ERROR) == 0 &&
          (event->fd_readwrite.flags & __WASI_EVENTRWFLAGS_FD_READWRITE_HANGUP) == 0) {
        // firebox#XD4 — a status subscription that only reports READY:
        // nothing the caller asked about. Demote it and poll again.
        demoted[pollfd - fds] = true;
        newly_demoted = true;
        continue;
      }
      if (!wants_io(pollfd) && event->error == 0) {
        // firebox#XD4 — status entry with a hangup: report only POLLHUP.
        if (event->fd_readwrite.flags & __WASI_EVENTRWFLAGS_FD_READWRITE_HANGUP)
          pollfd->revents |= POLLHUP;
        if (event->fd_readwrite.flags & __WASI_EVENTRWFLAGS_FD_READWRITE_READ_CLOSED)
          pollfd->revents |= pollfd->events & POLLRDHUP;
        continue;
      }
      if (event->error == __WASI_ERRNO_BADF) {
        // Invalid file descriptor.
        pollfd->revents |= POLLNVAL;
      } else if (event->error == __WASI_ERRNO_PIPE) {
        // Linux reports an error when a pipe has no reader.
        pollfd->revents |= POLLERR;
      } else if (event->error != 0) {
        // Another error occurred.
        pollfd->revents |= POLLERR;
      } else {
        // Data can be read or written.
        if (event->type == __WASI_EVENTTYPE_FD_READ) {
            // A read-ready event is readable whatever else it carries. Do
            // NOT infer "pipe at EOF, so not readable" from `nbytes == 0`
            // with HANGUP: the runtime reports exactly that for a regular
            // file and for /dev/zero, with or without data, and Linux polls
            // those POLLIN always. Dropping POLLIN there turns `poll()` on
            // a file into a bare POLLHUP and the caller never reads it
            // (firebox#DSR review). A widowed, drained pipe therefore still
            // reports POLLIN beside POLLHUP, as it did before this change;
            // the read returns 0.
            pollfd->revents |= pollfd->events & (POLLRDNORM | POLLIN);
            if (event->fd_readwrite.flags & __WASI_EVENTRWFLAGS_FD_READWRITE_READ_CLOSED)
              pollfd->revents |= pollfd->events & POLLRDHUP;
            if (event->fd_readwrite.flags & __WASI_EVENTRWFLAGS_FD_READWRITE_HANGUP) {
              pollfd->revents |= POLLHUP;
            }
        } else if (event->type == __WASI_EVENTTYPE_FD_WRITE) {
            pollfd->revents |= pollfd->events & (POLLWRNORM | POLLOUT);
            if (event->fd_readwrite.flags & __WASI_EVENTRWFLAGS_FD_READWRITE_READ_CLOSED)
              pollfd->revents |= pollfd->events & POLLRDHUP;
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
    // Socket hangup and writability coexist on Linux. Pipe write errors
    // arrive as ERR above and do not acquire writable readiness here.
    if (pollfd->revents != 0)
      ++retval;
  }
  // firebox#XD4 — only a demotion with nothing to report and time left makes
  // another round; anything reportable, or the timeout, ends the call.
  if (retval == 0 && newly_demoted && !clock_fired) {
    any_demoted = true;
    continue;
  }
  // Review F3: the slice ended with nothing to report. Re-arm every demoted
  // entry so the next round probes it for a hangup, and lengthen the slice.
  if (retval == 0 && clock_fired && sliced) {
    memset(demoted, 0, sizeof demoted);
    any_demoted = false;
    if (slice < 64)
      slice *= 2;
    continue;
  }
  // Review F3 (round 2): the timeout ended a wait that some entries sat out.
  // Probe them once more with no time left (`remaining` is 0, so the wait is a
  // zero-length readiness check) before reporting nothing.
  if (retval == 0 && clock_fired && any_demoted && !final_probe_done) {
    memset(demoted, 0, sizeof demoted);
    any_demoted = false;
    final_probe_done = true;
    continue;
  }
  return retval;
  }
}
