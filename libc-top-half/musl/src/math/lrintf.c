#include <limits.h>
#include <fenv.h>
#include <math.h>

/* uses LONG_MAX > 2^24, see comments in lrint.c */

long lrintf(float x)
{
	/* firebox #RNS: raise FE_INVALID when x rounds outside long's range (incl.
	   nan/inf); wasm's float->int conversion saturates silently. See lrint.c. */
	if (isnan(x) || x < (double)LONG_MIN || x >= -(double)LONG_MIN)
		feraiseexcept(FE_INVALID);
	return rintf(x);
}
