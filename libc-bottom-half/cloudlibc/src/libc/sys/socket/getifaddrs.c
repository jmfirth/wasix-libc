/* firebox#YZN: these two files implement the POSIX interface-enumeration pair
   over __wasi_port_addr_list, but they used to DEFINE them as `getif_addrs` /
   `freeif_addrs` over a private `struct if_addrs`, while <ifaddrs.h> shipped in
   the very same sysroot declared the POSIX spellings. The result was a header
   that promised a symbol no archive defined: every caller compiled and then
   died at link with `undefined symbol: getifaddrs`, indistinguishable from the
   capability being absent, even though the working implementation was sitting
   in libc.a under a name nothing calls. `struct if_addrs`
   (<__struct_if_addrs.h>, reached via <sys/socket.h>) is field-for-field
   identical to musl's `struct ifaddrs`, so binding to the declared name is a
   pure rename, not an ABI change. The private struct is deliberately left in
   place: it is public header surface, and a removal is the class that breaks
   consumers.

   musl's own network/getifaddrs.c is NOT the fix here and must not be put on
   the allow-list: it enumerates over netlink, which WASIX does not provide.
   This implementation asks the runtime. Where the runtime's network backend
   does not implement ip_list the call returns -1 with errno set from the WASI
   errno (ENOTSUP today on every backend but loopback) — an honest POSIX
   failure, not a silent empty list.  */

#include <errno.h>
#include <common/net.h>
#include <sys/socket.h>

#include <assert.h>
#include <wasi/api.h>
#include <errno.h>
#include <string.h>

#include <ifaddrs.h>
#include <wasi/libc.h>

void freeifaddrs(struct ifaddrs *ifa);

int getifaddrs(struct ifaddrs **ifap) {
  __wasi_size_t nips = 10;
  struct __wasi_addr_cidr_t *ips_heap = malloc(sizeof(struct __wasi_addr_cidr_t) * nips);
  if (ips_heap == NULL) {
    errno = ENOMEM;
    return -1;
  }
  memset(ips_heap, 0, sizeof(struct __wasi_addr_cidr_t) * nips);
  
  __wasi_errno_t error = __wasi_port_addr_list(ips_heap, &nips);
  if (error == __WASI_ERRNO_OVERFLOW) {
    free(ips_heap);
    ips_heap = malloc(sizeof(struct __wasi_addr_cidr_t) * nips);
    if (ips_heap == NULL) {
      errno = ENOMEM;
      return -1;
    }
    memset(ips_heap, 0, sizeof(struct __wasi_addr_cidr_t) * nips);

    // try again but with a bigger buffer (returned in nips)
    error = __wasi_port_addr_list(ips_heap, &nips);
  }
  if (error != 0) {
    free(ips_heap);
    errno = __wasilibc_errno_from_wasi(error);
    return -1;
  }

  struct ifaddrs *last = NULL;
  *ifap = NULL;
  for (__wasi_size_t n = 0; n < nips; n++) {
    struct ifaddrs *ifa = malloc(sizeof(struct ifaddrs));
    if (ifa == NULL) {
      freeifaddrs(*ifap);
      *ifap = NULL;
      free(ips_heap);
      errno = ENOMEM;
      return -1;
    }
    memset(ifa, 0, sizeof(struct ifaddrs));

    struct __wasi_addr_cidr_t * ip = &ips_heap[n];
    if (ip->tag == __WASI_ADDRESS_FAMILY_INET4) {
      struct sockaddr_in *addr4 = malloc(sizeof(struct sockaddr_in));
      if (addr4 == NULL) {
        freeifaddrs(ifa);
        continue;
      }
      addr4->sin_family = AF_INET;
      addr4->sin_port = 0;
      addr4->sin_addr.s_addr = *((in_addr_t*)&ip->u.inet4.addr);
      ifa->ifa_addr = (struct sockaddr*)addr4;
    } else if (ip->tag == __WASI_ADDRESS_FAMILY_INET6) {
      struct sockaddr_in6 *addr6 = malloc(sizeof(struct sockaddr_in6));
      if (addr6 == NULL) {
        freeifaddrs(ifa);
        continue;
      }
      addr6->sin6_family = AF_INET6;
      addr6->sin6_flowinfo = 0;
      addr6->sin6_scope_id = 0;
      addr6->sin6_port = 0;
      memcpy(&addr6->sin6_addr.s6_addr, &ip->u.inet6.addr, sizeof(struct in6_addr));
      ifa->ifa_addr = (struct sockaddr*)addr6; 
    } else {
      freeifaddrs(ifa);
      continue;
    }

    if (last != NULL) {
      last->ifa_next = ifa;
    } else {
      *ifap = ifa;
    }
    last = ifa;
  }

  free(ips_heap);
  return 0;
}
