#include <common/net.h>

#include <sys/socket.h>

#include <assert.h>
#include <wasi/api.h>
#include <errno.h>
#include <string.h>
#include <wasi/libc.h>

#define MIN(a,b) ((a)<(b) ? (a) : (b))

ssize_t recvfrom(int socket, void* buffer, size_t length, int flags, struct sockaddr *restrict addr, socklen_t *restrict addrlen) {
  // Prepare input parameters.
  __wasi_iovec_t iov = {.buf = buffer, .buf_len = length};
  __wasi_iovec_t *ri_data = &iov;
  size_t ri_data_len = 1;
  __wasi_riflags_t ri_flags = 0;

  if ((flags & MSG_PEEK) != 0) { ri_flags |= __WASI_RIFLAGS_RECV_PEEK; }
  if ((flags & MSG_WAITALL) != 0) { ri_flags |= __WASI_RIFLAGS_RECV_WAITALL; }
  if ((flags & MSG_TRUNC) != 0) { ri_flags |= __WASI_RIFLAGS_RECV_DATA_TRUNCATED; }
  if ((flags & MSG_DONTWAIT) != 0) { ri_flags |= __WASI_RIFLAGS_RECV_DONT_WAIT; }

  // Perform system call.
  __wasi_size_t ro_datalen;
  __wasi_roflags_t ro_flags;
  __wasi_addr_port_t peer_addr;
  __wasi_errno_t error = __wasi_sock_recv_from(socket,
                                               ri_data, ri_data_len, ri_flags,
                                               &ro_datalen,
                                               &ro_flags,
                                               &peer_addr);
  if (error != 0) {
    errno = __wasilibc_errno_from_wasi(error);
    return -1;
  }

  // firebox#9EJ — the conversion's errno was discarded. It is a guest `E*` and
  // must not pass through __wasilibc_errno_from_wasi; see common/net.h.
  //
  // A NULL `addr` is legal here for the same reason it is legal in accept():
  // recvfrom() with a NULL source address means the caller does not want it.
  // The datagram has already been consumed at this point, but reporting the
  // failure anyway is what Linux does -- __sys_recvfrom overwrites its return
  // with move_addr_to_user's error.
  if (addr != NULL) {
    int guest_error = wasi_to_sockaddr(&peer_addr, addr, addrlen);
    if (guest_error != 0) {
      errno = guest_error;
      return -1;
    }
  }
  return ro_datalen;
}
