#include <limits.h>
#include <fenv.h>
#include <math.h>

/* uses LLONG_MAX > 2^24, see comments in lrint.c */

long long llrintf(float x)
{
	/* firebox #RNS: raise FE_INVALID when x rounds outside long long's range
	   (incl. nan/inf); wasm's float->int conversion saturates silently. */
	if (isnan(x) || x < (double)LLONG_MIN || x >= -(double)LLONG_MIN)
		feraiseexcept(FE_INVALID);
	return rintf(x);
}
