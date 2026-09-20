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

void freeifaddrs(struct ifaddrs *ifa) {
  for (;ifa != NULL;) {
    if (ifa->ifa_addr != NULL) {
      free(ifa->ifa_addr);
      ifa->ifa_addr = NULL;
    }
    if (ifa->ifa_netmask != NULL) {
      free(ifa->ifa_netmask);
      ifa->ifa_netmask = NULL;
    }
    if (ifa->ifa_ifu.ifu_broadaddr != NULL) {
      free(ifa->ifa_ifu.ifu_broadaddr);
      ifa->ifa_ifu.ifu_broadaddr = NULL;
    }
    if (ifa->ifa_ifu.ifu_dstaddr != NULL) {
      free(ifa->ifa_ifu.ifu_dstaddr);
      ifa->ifa_ifu.ifu_dstaddr = NULL;
    }
    if (ifa->ifa_data != NULL) {
      free(ifa->ifa_data);
      ifa->ifa_data = NULL;
    }
    struct ifaddrs * next = ifa->ifa_next;
    free(ifa);
    ifa = next;
  }
}
