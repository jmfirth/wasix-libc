#include <stdio.h>
#include <stdlib.h>

#ifndef __wasilibc_unmodified_upstream
/* firebox#526 — breadcrumb stamp; see libc-top-half/musl/src/exit/abort.c. */
extern void firebox_526_stamp(const char *crumb);
#endif

_Noreturn void __assert_fail(const char *expr, const char *file, int line, const char *func)
{
#ifndef __wasilibc_unmodified_upstream
	firebox_526_stamp("__assert_fail");
#endif
	fprintf(stderr, "Assertion failed: %s (%s: %s: %d)\n", expr, file, func, line);
	abort();
}
