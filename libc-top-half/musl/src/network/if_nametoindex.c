#define _GNU_SOURCE
#ifdef __wasilibc_unmodified_upstream /* netlink / SIOCGIF* */
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <string.h>
#include "syscall.h"

unsigned if_nametoindex(const char *name)
{
	struct ifreq ifr;
	int fd, r;

	if ((fd = socket(AF_UNIX, SOCK_DGRAM|SOCK_CLOEXEC, 0)) < 0) return 0;
	strncpy(ifr.ifr_name, name, sizeof ifr.ifr_name);
	r = ioctl(fd, SIOCGIFINDEX, &ifr);
	__syscall(SYS_close, fd);
	return r < 0 ? 0 : ifr.ifr_ifindex;
}
#else
/* firebox#25D / #9FB: WASIX has no netlink and no SIOCGIF* ioctls, so musl's
 * body (kept above for upstream diffs) cannot run. This one derives from
 * __wasix_port_if_list, the same host list getifaddrs reads, so the four
 * interface functions cannot disagree. Without an interface model (no --net;
 * the browser) the list fails with ENOTSUP and so does this. */
#include <net/if.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <wasi/api.h>
#include <wasi/libc.h>

unsigned if_nametoindex(const char *name)
{
	struct __wasix_if_rec *recs;
	size_t n, i;
	unsigned index = 0;
	__wasi_errno_t err = __wasix_if_list_fetch(&recs, &n);
	if (err) {
		errno = __wasilibc_errno_from_wasi(err);
		return 0;
	}
	for (i = 0; i < n; i++) {
		if (!strncmp((const char *)recs[i].name, name, IF_NAMESIZE)) {
			index = recs[i].index;
			break;
		}
	}
	free(recs);
	/* Linux (glibc and musl alike) reports an unknown name as ENODEV. */
	if (!index) errno = ENODEV;
	return index;
}
#endif
