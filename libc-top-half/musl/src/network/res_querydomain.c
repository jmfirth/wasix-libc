#include <resolv.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>

int res_querydomain(const char *name, const char *domain, int class, int type, unsigned char *dest, int len)
{
	if (!domain || !*domain) return res_query(name, class, type, dest, len);
	char tmp[255];
	size_t nl = strnlen(name, 255), dl = strnlen(domain, 255);
	if (nl+dl+1 > 254) { errno = EMSGSIZE; h_errno = NO_RECOVERY; return -1; }
	memcpy(tmp, name, nl);
	tmp[nl] = '.';
	memcpy(tmp+nl+1, domain, dl+1);
	return res_query(tmp, class, type, dest, len);
}
