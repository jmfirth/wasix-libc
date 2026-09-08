#include <errno.h>
#include <common/net.h>
#include <sys/socket.h>

#include <assert.h>
#include <wasi/api.h>
#include <errno.h>
#include <string.h>
#include <wasi/libc.h>

int bind(int socket, const struct sockaddr *restrict addr, socklen_t addrlen) {
  __wasi_addr_port_t peer_addr;
  // firebox#EK0 — sockaddr_to_wasi diagnoses ENTIRELY IN THE GUEST and returns
  // a guest `E*`. No host call was made, so there is nothing for
  // __wasilibc_errno_from_wasi to translate; running it through the translator
  // would translate a guest constant a second time, which reads as correct
  // today only because the two numberings still coincide. Separate variable,
  // separate type. See the note in common/net.h.
  int guest_error = sockaddr_to_wasi(addr, addrlen, &peer_addr);
  if (guest_error != 0) {
    errno = guest_error;
    return -1;
  }

  __wasi_errno_t error = __wasi_sock_bind(socket, &peer_addr);
  if (error != 0) {
    errno = __wasilibc_errno_from_wasi(error);
    return -1;
  }

  return 0;
}
