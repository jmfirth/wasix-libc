#include <errno.h>
#include <common/net.h>
#include <sys/socket.h>

#include <assert.h>
#include <wasi/api.h>
#include <errno.h>
#include <string.h>
#include <wasi/libc.h>

int getpeername(int socket, struct sockaddr *restrict addr, socklen_t *restrict addrlen) {
  __wasi_addr_port_t peer_addr;
  __wasi_errno_t error = __wasi_sock_addr_peer(socket, &peer_addr);
  if (error != 0) {
    errno = __wasilibc_errno_from_wasi(error);
    return -1;
  }

  // firebox#9EJ — the conversion's errno was discarded. It is a guest `E*` and
  // must not pass through __wasilibc_errno_from_wasi; see common/net.h. As with
  // getsockname(), a NULL address is not a legal request here -- Linux reports
  // EFAULT -- so there is no NULL guard to skip the call.
  int guest_error = wasi_to_sockaddr(&peer_addr, addr, addrlen);
  if (guest_error != 0) {
    errno = guest_error;
    return -1;
  }
  return 0;
}
