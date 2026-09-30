#define _BSD_SOURCE
#include <resolv.h>
#include <netdb.h>
#include <errno.h>

int res_query(const char *name, int class, int type, unsigned char *dest, int len)
{
	unsigned char q[280];
	int ql = __res_mkquery(0, name, class, type, 0, 0, 0, q, sizeof q);
	if (ql < 0) { errno = EINVAL; h_errno = NO_RECOVERY; return -1; }
	int r = __res_send(q, ql, dest, len);
	if (r < 12) { h_errno = TRY_AGAIN; return -1; }
	switch (dest[3] & 15) {
	case 0:
		if (dest[6] || dest[7]) return r;
		h_errno = NO_DATA;
		break;
	case 2: h_errno = TRY_AGAIN; break;
	case 3: h_errno = HOST_NOT_FOUND; break;
	default: h_errno = NO_RECOVERY; break;
	}
	return -1;
}
