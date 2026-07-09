#include <limits.h>
#include <fenv.h>
#include <math.h>

/* uses LLONG_MAX > 2^53, see comments in lrint.c */

long long llrint(double x)
{
	/* firebox #RNS: raise FE_INVALID when x rounds outside long long's range
	   (incl. nan/inf); wasm's float->int conversion saturates silently.
	   -(double)LLONG_MIN == LLONG_MAX+1 (an exact power of two). See lrint.c. */
	if (isnan(x) || x < (double)LLONG_MIN || x >= -(double)LLONG_MIN)
		feraiseexcept(FE_INVALID);
	return rint(x);
}
