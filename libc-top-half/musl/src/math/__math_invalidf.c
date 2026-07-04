#include <fenv.h>
#include "libm.h"

float __math_invalidf(float x)
{
	/* firebox #7CD: raise FE_INVALID explicitly; skip when x is already NaN
	 * (quiet propagation must not signal). See __math_invalid.c. */
	if (!isnan(x))
		feraiseexcept(FE_INVALID);
	return (x - x) / (x - x);
}
