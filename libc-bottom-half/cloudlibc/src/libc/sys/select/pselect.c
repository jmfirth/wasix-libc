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
  if (errorfds != NULL) {
    FD_ZERO(errorfds);
  }

  // Replace NULL pointers by the empty set.
  fd_set empty;
  FD_ZERO(&empty);
  if (readfds == NULL)
    readfds = &empty;
  if (writefds == NULL)
    writefds = &empty;

  // #D7S: fd_set is the POSIX BITMASK, so the sets are walked BY FD NUMBER over
  // [0, nfds) rather than over a stored fd list. POSIX examines only descriptors
  // below nfds; FD_SETSIZE bounds the array itself.
  const int fdlimit = nfds < FD_SETSIZE ? nfds : FD_SETSIZE;

  // Determine the maximum number of events. Counted first rather than derived
  // from a bound: sizing the VLA at 2*FD_SETSIZE+1 would put ~98KB on the stack
  // for every select() call regardless of how few fds were actually set.
  size_t nset = 0;
  for (int fd = 0; fd < fdlimit; ++fd) {
    if (FD_ISSET(fd, readfds))
      ++nset;
    if (FD_ISSET(fd, writefds))
      ++nset;
  }
  size_t maxevents = nset + 1;
  __wasi_subscription_t subscriptions[maxevents];
  size_t nsubscriptions = 0;

  // Convert the readfds set.
  for (int fd = 0; fd < fdlimit; ++fd) {
    if (FD_ISSET(fd, readfds)) {
      __wasi_subscription_t *subscription = &subscriptions[nsubscriptions++];
      *subscription = (__wasi_subscription_t){
          .userdata = (uintptr_t)(intptr_t)fd,
          .u.tag = __WASI_EVENTTYPE_FD_READ,
          .u.u.fd_read.file_descriptor = fd,
      };
    }
  }

  // Convert the writefds set.
  for (int fd = 0; fd < fdlimit; ++fd) {
    if (FD_ISSET(fd, writefds)) {
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
  //
  // #D7S: the return value is a COUNT OF READY DESCRIPTORS and must be counted,
  // not read back out of the sets — under the old list layout it was
  // `readfds->__nfds + writefds->__nfds`, which a caller building the set as a
  // raw bit vector saw as garbage (a one-fd pipe set measured n=15). A descriptor
  // ready for both reading and writing counts TWICE, which is what POSIX
  // specifies: the return is the number of bits set across all result sets, not
  // the number of distinct descriptors.
  FD_ZERO(readfds);
  FD_ZERO(writefds);
  int nready = 0;
  for (size_t i = 0; i < nevents; ++i) {
    const __wasi_event_t *event = &events[i];
    int fd = (int)(intptr_t)event->userdata;
    if (event->type == __WASI_EVENTTYPE_FD_READ) {
      if (!FD_ISSET(fd, readfds)) {
        FD_SET(fd, readfds);
        ++nready;
      }
    } else if (event->type == __WASI_EVENTTYPE_FD_WRITE) {
      if (!FD_ISSET(fd, writefds)) {
        FD_SET(fd, writefds);
        ++nready;
      }
    }
  }
  return nready;
}
