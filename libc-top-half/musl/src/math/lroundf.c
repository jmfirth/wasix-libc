#include <limits.h>
#include <fenv.h>
#include <math.h>

long lroundf(float x)
{
	/* firebox #RNS: raise FE_INVALID when x rounds outside long's range (incl.
	   nan/inf); wasm's float->int conversion saturates silently. See lrint.c. */
	if (isnan(x) || x < (double)LONG_MIN || x >= -(double)LONG_MIN)
		feraiseexcept(FE_INVALID);
	return roundf(x);
}
