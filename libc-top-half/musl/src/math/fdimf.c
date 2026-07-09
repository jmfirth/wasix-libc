#include <math.h>
#include <fenv.h>	/* firebox #RNS: software fenv — raise the x-y subtraction's flags on wasm */

float fdimf(float x, float y)
{
	if (isnan(x))
		return x;
	if (isnan(y))
		return y;
	if (x > y) {
		float d = x - y;
		/* firebox #RNS: raise the subtraction's flags explicitly (wasm sets
		   none). See fdim.c. wasm has FLT_EVAL_METHOD==0, so the float TwoSum
		   below rounds to float and captures the exact rounding error. */
		if (isinf(d)) {
			if (isfinite(x) && isfinite(y))
				feraiseexcept(FE_OVERFLOW | FE_INEXACT);
		} else {
			float my = -y;
			float ap = d - my;
			float bp = d - ap;
			float err = (x - ap) + (my - bp);
			if (err != 0) {
				feraiseexcept(FE_INEXACT);
				if (d != 0 && fabsf(d) < 0x1p-126f)
					feraiseexcept(FE_UNDERFLOW);
			}
		}
		return d;
	}
	return 0;
}
