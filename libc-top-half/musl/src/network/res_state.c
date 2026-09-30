#include <resolv.h>

/* Legacy resolver state: explicit IPv4 server overrides and transport flags
 * are consumed by res_send; file configuration supplies its defaults. */

struct __res_state *__res_state()
{
	static struct __res_state res;
	return &res;
}
