// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <common/time.h>

#include <sys/select.h>

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

  // Execute poll().
  __wasi_size_t nevents;
  __wasi_event_t events[nsubscriptions];
  __wasi_errno_t error =
      __wasi_poll_oneoff(subscriptions, events, nsubscriptions, &nevents);
  if (error != 0) {
    // WASI's poll requires at least one subscription, or else it returns
    // `EINVAL`. Since a `pselect` with nothing to wait for is valid in POSIX,
    // return `ENOTSUP` to indicate that we don't support that case.
    //
    // Wasm has no signal handling, so if none of the user-provided `pollfd`
    // elements, nor the timeout, led us to producing even one subscription
    // to wait for, there would be no way for the poll to wake up. WASI
    // returns `EINVAL` in this case, but for users of `poll`, `ENOTSUP` is
    // more likely to be understood.
    if (nsubscriptions == 0)
      errno = ENOTSUP;
    else
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
