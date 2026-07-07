#define _GNU_SOURCE
#include <sched.h>

/* firebox#QAF — the substrate presents a single logical CPU (#0), so the
 * currently-executing CPU is always 0. This mirrors sched_getaffinity's
 * single-CPU mask in affinity.c. (Upstream used a Linux vDSO/getcpu syscall
 * that does not exist under WASI.) */

int sched_getcpu(void)
{
	return 0;
}
