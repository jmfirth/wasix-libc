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

#include <_/cdefs.h>
#include <ifaddrs.h>
#include <stdlib.h>

/* firebox#P47 (P47-B 77bbf88e, merged onto the #YZN rename): the WASIX
 * producer allocates each node and each member separately, and every member is
 * heap-owned or NULL. The broadcast/destination union owns ONE allocation, so it
 * is freed once through ifa_broadaddr. ifa_name is freed too (NULL today). */
void freeifaddrs(struct ifaddrs *ifa) {
  while (ifa) {
    struct ifaddrs *next = ifa->ifa_next;
    free(ifa->ifa_name);
    free(ifa->ifa_addr);
    free(ifa->ifa_netmask);
    free(ifa->ifa_broadaddr);
    free(ifa->ifa_data);
    free(ifa);
    ifa = next;
  }
}

// firebox#YZN: pre-rename spelling kept as a strong alias; see getifaddrs.c.
__strong_reference(freeifaddrs, freeif_addrs);
