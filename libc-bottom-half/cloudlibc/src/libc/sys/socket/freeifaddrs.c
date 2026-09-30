#include <ifaddrs.h>
#include <stdlib.h>
#include <__struct_if_addrs.h>

/* The WASIX producer allocates nodes and members separately. Both public
 * spellings share that ownership; the broadcast/destination union owns one
 * allocation. Preserve the legacy spelling for existing consumers (#YZN). */
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

void freeif_addrs(struct if_addrs *restrict ifa) {
  /* The legacy producer has a distinct C struct type with the same fields;
   * walk that type directly rather than relying on type-punned accesses. */
  while (ifa) {
    struct if_addrs *next = ifa->ifa_next;
    free(ifa->ifa_name);
    free(ifa->ifa_addr);
    free(ifa->ifa_netmask);
    free(ifa->ifa_ifu.ifu_broadaddr);
    free(ifa->ifa_data);
    free(ifa);
    ifa = next;
  }
}
