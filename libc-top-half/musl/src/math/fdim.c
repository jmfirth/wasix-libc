#include <math.h>
#include <fenv.h>	/* firebox #RNS: software fenv — raise the x-y subtraction's flags on wasm */

double fdim(double x, double y)
{
	if (isnan(x))
		return x;
	if (isnan(y))
		return y;
	if (x > y) {
		double d = x - y;
		/* firebox #RNS: wasm's silent f64 subtract sets no status flag, so raise
		   the range-error flags explicitly. inf from FINITE operands is a genuine
		   OVERFLOW|INEXACT (but inf-finite=inf when an operand is already inf is
		   exact and flagless). Otherwise a Knuth TwoSum recovers the exact
		   rounding error of x-y: err==0 iff exact, so INEXACT fires only on a real
		   rounding, and a non-zero subnormal inexact result also underflows. */
		if (isinf(d)) {
			if (isfinite(x) && isfinite(y))
				feraiseexcept(FE_OVERFLOW | FE_INEXACT);
		} else {
			double my = -y;
			double ap = d - my;
			double bp = d - ap;
			double err = (x - ap) + (my - bp);
			if (err != 0) {
				feraiseexcept(FE_INEXACT);
				if (d != 0 && fabs(d) < 0x1p-1022)
					feraiseexcept(FE_UNDERFLOW);
			}
		}
		return d;
	}
	return 0;
}
