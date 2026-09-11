// SPDX-License-Identifier: BSD-2-Clause

#include <common/cancel.h>
#include <common/net.h>

#include <sys/socket.h>

#include <assert.h>
#include <wasi/api.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <wasi/libc.h>

int accept(int socket, struct sockaddr *restrict addr, socklen_t *restrict addrlen) {
  return accept4(socket, addr, addrlen, 0);
}

int accept4(int socket, struct sockaddr *restrict addr, socklen_t *restrict addrlen, int flags) {
  int ret = -1;
  
  if (flags & ~(SOCK_NONBLOCK | SOCK_CLOEXEC)) {
    errno = EINVAL;
    return -1;
  }

  __wasi_addr_port_t peer_addr;
  // firebox#TWX — POSIX XSH 2.9.5 cancellation point. Observe an
  // already-pending cancel BEFORE parking in the host await.
  __cloudlibc_testcancel();

  __wasi_errno_t error = __wasi_sock_accept_v2(socket, (flags & SOCK_NONBLOCK) ? __WASI_FDFLAGS_NONBLOCK : 0, &ret, &peer_addr);

  if (error != 0) {
    // firebox#TWX — a cancel that arrived while we were parked. Keyed on
    // EINTR (musl's `__syscall_cp_c` rule, pthread_cancel.c:92) so a COMPLETED
    // call never discards what it already consumed. Never returns if a cancel
    // is pending and enabled.
    __cloudlibc_testcancel_if_intr(error);
    errno = __wasilibc_errno_from_wasi(error);
    return -1;
  }

  // Linux accept4(SOCK_CLOEXEC) marks the accepted fd close-on-exec.
  // SOCK_CLOEXEC was validated as legal above but then silently dropped:
  // sock_accept_v2 only carries NONBLOCK, and the runtime creates accepted
  // fds with empty fdflagsext — so the accepted connection leaked into
  // every spawned child (firebox#ENM finding C5). Set it post-hoc; the
  // syscall has no fdflagsext parameter.
  if (flags & SOCK_CLOEXEC) {
    if (fcntl(ret, F_SETFD, FD_CLOEXEC) < 0) {
      int saved_errno = errno;
      close(ret);
      errno = saved_errno;
      return -1;
    }
  }

  // firebox#9EJ — THE CONVERSION'S ERRNO WAS DISCARDED.
  // wasi_to_sockaddr diagnoses ENTIRELY IN THE GUEST and returns a guest `E*`;
  // no host call is made in it, so there is nothing for
  // __wasilibc_errno_from_wasi to translate and running it through the
  // translator would translate a guest constant a second time. Separate
  // variable, separate type. See the note in common/net.h.
  //
  // The `addr != NULL` guard is not defensive padding: POSIX and Linux both let
  // accept() take a NULL address, meaning "I do not want the peer's address."
  // That is a success, not the EFAULT the helper reports for a copy-out target
  // it was asked to write through. Only a caller that asked for the address can
  // fail to receive it.
  if (addr != NULL) {
    int guest_error = wasi_to_sockaddr(&peer_addr, addr, addrlen);
    if (guest_error != 0) {
      // Linux drops the accepted connection when it cannot hand the address
      // back (__sys_accept4_file's move_addr_to_user failure path releases the
      // socket and the fd), so returning -1 while leaking `ret` would be a
      // divergence of its own. Same shape as the SOCK_CLOEXEC failure above.
      close(ret);
      errno = guest_error;
      return -1;
    }
  }
  return ret;
}
