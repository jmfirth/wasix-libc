// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#ifndef COMMON_NET_H
#define COMMON_NET_H

#include <sys/socket.h>
#include <__struct_sockaddr_in.h>
#include <__struct_sockaddr_in6.h>
#include <__struct_sockaddr_un.h>
#include <errno.h>

#include <wasi/api.h>
#include <string.h>
#include <stdint.h>
#include <netinet/in.h>
#include <unistd.h>

#ifndef MIN
#define MIN(a,b) ((a)<(b) ? (a) : (b))
#endif

static inline int is_wasi_port_ok() {
  // check if wasi port in sockaddr is not byte swapped already.
  // To find out: create a socket, bind to a port, read back and check the port
  // in case of error, return 1 as default
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if(sock<0)
    return 1;
  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_port = 9000;
  if(bind(sock, (struct sockaddr *)&addr, sizeof(addr))<0) {
    close(sock);
    return 1;
  }
  // readback the address
  __wasi_addr_port_t local_addr;
  if(__wasi_sock_addr_local(sock, &local_addr)!=0) {
    close(sock);
    return 1;
  }
  close(sock);
  return (local_addr.u.inet4.port==10275); //10275 is 9000 with swapped bytes
}

// firebox#EK0 — THESE TWO HELPERS RETURN A *GUEST* ERRNO. NEVER TRANSLATE IT.
//
// They used to return a bare 0/-1 sentinel, and every caller that kept the
// value stored it in a `__wasi_errno_t` (uint16_t), so -1 became 65535. Before
// this branch that number reached `errno` verbatim -- an errno in no numbering
// at all, with no `strerror` entry and no `E*` a caller could compare against.
// The translator added earlier on this branch turns it into EUNKNOWN instead,
// which is honest but still tells the caller nothing: bind() rejecting an
// AF_PACKET address and bind() rejecting a truncated addrlen are the same
// opaque code, where Linux gives two different ones. That is invariant 2's
// failure mode -- the guest cannot do what it does on Linux.
//
// The value they now return is a guest `E*` (0 on success, positive on
// failure), because the failure is diagnosed ENTIRELY IN THE GUEST: no host
// call is made here, so there is no host errno to carry and nothing for
// `__wasilibc_errno_from_wasi` to translate. A caller assigns it straight to
// `errno`:
//
//     int guest_error = sockaddr_to_wasi(addr, addrlen, &peer_addr);
//     if (guest_error != 0) { errno = guest_error; return -1; }
//
// ⚠ Passing this through `__wasilibc_errno_from_wasi` would be a
// DOUBLE-TRANSLATION bug -- invisible today, because the guest and host
// numberings are still the same numbers, and silently wrong the moment #87F
// step 4 renumbers the guest side. That is why the return type is `int` and
// not `__wasi_errno_t`, and why callers name the variable `guest_error`: the
// two spaces must not share a type or a name. Same rule as pread/pwrite.
//
// The errno choices are Linux's, and they are distinguished on purpose:
//   EAFNOSUPPORT -- a family this libc cannot express as a WASI address.
//   EINVAL       -- the right family, an addrlen that cannot hold it (or, for
//                   AF_UNIX, a path longer than the WASI address can carry).
//   EFAULT       -- a NULL address buffer, in either direction: a copy-out
//                   target we cannot write the address into, or (firebox#9EJ)
//                   a copy-in source we cannot read one out of. Linux reports
//                   EFAULT for an unusable user pointer on both sides.
// Blanketing one code over all three would erase a distinction Linux makes and
// callers switch on.
//
// firebox#9EJ — THE NULL CHECKS LIVE HERE, NOT IN THE CALLERS, AND A NULL
// POINTER DOES NOT TRAP ON WASM.
//
// The ticket predicted a trap. MEASURED, it is worse than that. A null
// `struct sockaddr *` is linear-memory offset 0, and offset 0 is ordinary
// readable, writable module memory -- there is no unmapped page to fault on.
// So `addr->sa_family` on a NULL address quietly read the zeros that live
// there, decided the family was AF_UNSPEC, and returned EAFNOSUPPORT: a
// plausible, wrong, un-diagnosable errno where Linux says EFAULT. On the
// copy-out side it was worse still -- getsockname's ENOTSOCK arm WROTE
// AF_LOCAL through the null pointer into guest address 0 and returned 0, so
// the guest corrupted its own memory and was told it had succeeded. Only an
// out-of-bounds pointer traps; a null one is silent.
//
// The guard is at the only place that dereferences, so the next caller to be
// written cannot reintroduce it. Callers that are ALLOWED a NULL address by
// POSIX -- accept() and recvfrom(), where NULL means "do not report the peer"
// and is not an error -- must still skip the call entirely rather than
// translate the EFAULT, and they do.

/// Converts a WASI address into a socket address
static inline int wasi_to_sockaddr(const struct __wasi_addr_port_t *restrict peer_addr, struct sockaddr *restrict addr, socklen_t *restrict addrlen) {
  // No usable buffer to write the address into. EFAULT is what Linux reports
  // for a copy-out target it cannot use. `addrlen` is checked alongside `addr`
  // because the very first thing done with a non-NULL `addr` is `*addrlen`:
  // Linux's move_addr_to_user reads the length through the same get_user, so a
  // non-NULL addr with a NULL addrlen is EFAULT there too -- not a trap.
  if (addr == NULL || addrlen == NULL) {
    return EFAULT;
  }
  // This test needs to be here because some older versions of wasmer report the port as big-endian
  static int tested = 0;
  static int need_revert = 1;
  if(!tested) {
    tested = 1;
    need_revert = is_wasi_port_ok();
  }
  memset(addr, 0, *addrlen);
  if (peer_addr->tag == __WASI_ADDRESS_FAMILY_INET4) {
    struct sockaddr_in addr4;
    addr4.sin_family = AF_INET;
    addr4.sin_port = need_revert?htons(peer_addr->u.inet4.port):peer_addr->u.inet4.port;
    addr4.sin_addr.s_addr = *((in_addr_t*)&peer_addr->u.inet4.addr);
    memcpy(addr, &addr4, MIN(sizeof(struct sockaddr_in), *addrlen));
    *addrlen = sizeof(struct sockaddr_in);
  } else if (peer_addr->tag == __WASI_ADDRESS_FAMILY_INET6) {
    struct sockaddr_in6 addr6;
    addr6.sin6_family = AF_INET6;
    addr6.sin6_flowinfo = peer_addr->u.inet6.addr.flow_info1 << 16 | peer_addr->u.inet6.addr.flow_info0;
    addr6.sin6_scope_id = peer_addr->u.inet6.addr.scope_id1 << 16 | peer_addr->u.inet6.addr.scope_id0;;
    addr6.sin6_port = need_revert?htons(peer_addr->u.inet6.port):peer_addr->u.inet6.port;
    memcpy(&addr6.sin6_addr.s6_addr, &peer_addr->u.inet6.addr, sizeof(struct in6_addr));
    memcpy(addr, &addr6, MIN(sizeof(struct sockaddr_in6), *addrlen));
    *addrlen = sizeof(struct sockaddr_in6);
  } else if (peer_addr->tag == __WASI_ADDRESS_FAMILY_UNIX) {
    struct sockaddr_un addrun;
    addrun.sun_family = AF_UNIX;
    memcpy(&addrun.sun_path, &peer_addr->u.unix.b0, sizeof(addrun.sun_path));
    addrun.sun_path[sizeof(addrun.sun_path) - 1] = '\0'; // make sure the address is null-terminated
    // firebox#GN5 — THE COPY-OUT. This arm built `addrun` on the stack, set
    // `*addrlen` to describe it, and returned 0 WITHOUT EVER COPYING IT INTO
    // `addr` -- the one line the INET4 and INET6 arms above both end with.
    //
    // MEASURED with a guest probe under `firebox run`: getsockname() on a
    // socket bound to /tmp/gn5-probe.sock returned 0 with *addrlen == 21,
    // and the caller's buffer -- poisoned with 0xAA before the call --
    // came back all zeros: sun_family 0 (AF_UNSPEC), sun_path "". The
    // memset at the top of this function is what zeroed it; without that
    // the caller would have read back its own stale bytes. Either way the
    // call reports success and a length that claims N valid bytes are
    // present, so broken state is indistinguishable from working state.
    // Invariant 3 admits no deferral for that class.
    //
    // ⚠ ORDER. `*addrlen` is an in/out parameter: on the way in it is the
    // SIZE OF THE CALLER'S BUFFER, on the way out the length of the
    // address. The length is computed into a local FIRST so the copy can
    // still be bounded by the incoming buffer size; assigning `*addrlen`
    // before the memcpy would bound the copy by the value we just invented
    // and overrun a short buffer. Same sequencing as the arms above, where
    // the input size is read inside the MIN and overwritten on the next
    // line.
    //
    // Truncation follows Linux's move_addr_to_user: copy what fits, and
    // still report the UNTRUNCATED length, which is how a caller detects
    // that it got a short address.
    socklen_t unix_addrlen =
        (socklen_t)(offsetof(struct sockaddr_un, sun_path) + strlen(addrun.sun_path));
    memcpy(addr, &addrun, MIN((size_t)unix_addrlen, (size_t)*addrlen));
    *addrlen = unix_addrlen;
  } else {
    addr->sa_family = AF_UNSPEC;
    *addrlen = sizeof(struct sockaddr);
  }
  return 0;
}

static inline int sockaddr_to_wasi(const struct sockaddr *restrict addr, const socklen_t addrlen, struct __wasi_addr_port_t *restrict peer_addr) {
  // firebox#9EJ — `addr->sa_family` below is an unconditional dereference, and
  // bind()/connect() pass the caller's pointer through untouched. On wasm that
  // read does not fault: it returns the zeros at linear-memory offset 0, so
  // `bind(fd, NULL, 0)` reported EAFNOSUPPORT where Linux reports EFAULT --
  // measured, not predicted. Checked before the memset so nothing is written
  // on a request that cannot be read.
  if (addr == NULL) {
    return EFAULT;
  }
  memset(peer_addr, 0, sizeof(struct __wasi_addr_port_t));
  // The family test and the length test are separate branches on purpose. A
  // single `family == X && addrlen >= Y` chain funnels "family we do not know"
  // and "family we know, addrlen too short for it" into one else, and Linux
  // distinguishes them: EAFNOSUPPORT for the former, EINVAL for the latter.
  if (addr->sa_family == AF_INET) {
    if (addrlen < (socklen_t)sizeof(struct sockaddr_in))
      return EINVAL;
    struct sockaddr_in *addr4 = (struct sockaddr_in *)addr;
    peer_addr->tag = __WASI_ADDRESS_FAMILY_INET4;
    peer_addr->u.inet4.port = ntohs(addr4->sin_port);
    *((in_addr_t*)&peer_addr->u.inet4.addr) = addr4->sin_addr.s_addr;
    return 0;
  } else if (addr->sa_family == AF_INET6) {
    if (addrlen < (socklen_t)sizeof(struct sockaddr_in6))
      return EINVAL;
    struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)addr;
    peer_addr->tag = __WASI_ADDRESS_FAMILY_INET6;
    peer_addr->u.inet6.port = ntohs(addr6->sin6_port);
    peer_addr->u.inet6.addr.flow_info1 = addr6->sin6_flowinfo >> 16;
    peer_addr->u.inet6.addr.flow_info0 = addr6->sin6_flowinfo & 0xffff;
    peer_addr->u.inet6.addr.scope_id1 = addr6->sin6_scope_id >> 16;
    peer_addr->u.inet6.addr.scope_id0 = addr6->sin6_scope_id & 0xffff;
    memcpy(&peer_addr->u.inet6.addr, &addr6->sin6_addr.s6_addr, sizeof(struct in6_addr));
    return 0;
  } else if (addr->sa_family == AF_UNIX) {
    if (addrlen < (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1))
      return EINVAL;
    struct sockaddr_un *addrun = (struct sockaddr_un *)addr;
    peer_addr->tag = __WASI_ADDRESS_FAMILY_UNIX;
    socklen_t pathlen = addrlen - offsetof(struct sockaddr_un, sun_path);
    if (pathlen > 107) { // Addresses are limited to 107 bytes + 1 null byte only
      // Same code Linux's unix_mkname gives an over-long sockaddr_un: the
      // family is one we support, the length is not one it can hold.
      return EINVAL;
    }
    memcpy(&peer_addr->u.unix.b0, &addrun->sun_path, (size_t)pathlen);
    *(uint8_t *)(&peer_addr->u.unix.b0 + pathlen) = '\0';
    return 0;
  } else {
    // A family this libc has no WASI address shape for -- AF_UNSPEC included.
    return EAFNOSUPPORT;
  }
}

#endif
