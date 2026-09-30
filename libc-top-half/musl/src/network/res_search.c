#include <resolv.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "lookup.h"

int res_search(const char *name, int class, int type, unsigned char *dest, int len)
{
	struct resolvconf conf;
	char search[256];
	if (__get_resolv_conf(&conf, search, sizeof search)) { h_errno = TRY_AGAIN; return -1; }
	size_t nl = strnlen(name, 255);
	if (!nl || nl >= 255 || name[nl-1] == '.') return res_query(name, class, type, dest, len);
	unsigned dots = 0;
	for (const char *p = name; *p; p++) dots += *p == '.';
	int first_error = 0, no_data = 0, again = 0;
	if (dots >= conf.ndots) {
		int r = res_query(name, class, type, dest, len);
		if (r >= 0) return r;
		first_error = h_errno;
	}
	const char *local = getenv("LOCALDOMAIN");
	if (!local) local = search;
	while (*local) {
		while (isspace((unsigned char)*local)) local++;
		const char *end = local;
		while (*end && !isspace((unsigned char)*end) && *end != '#' && *end != ';') end++;
		if (end == local) break;
		size_t n = end-local;
		if (n < sizeof search) {
			char domain[256];
			memcpy(domain, local, n);
			domain[n] = 0;
			int r = res_querydomain(name, domain, class, type, dest, len);
			if (r >= 0) return r;
			no_data |= h_errno == NO_DATA;
			again |= h_errno == TRY_AGAIN;
			if (h_errno != HOST_NOT_FOUND && h_errno != NO_DATA && h_errno != TRY_AGAIN) return -1;
		}
		local = end;
	}
	if (!first_error) {
		int r = res_query(name, class, type, dest, len);
		if (r >= 0) return r;
	}
	if (first_error) h_errno = first_error;
	else if (no_data) h_errno = NO_DATA;
	else if (again) h_errno = TRY_AGAIN;
	return -1;
}
