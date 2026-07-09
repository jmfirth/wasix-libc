#include "libm.h"

#if FLT_EVAL_METHOD==2
#undef sqrt
#define sqrt sqrtl
#endif

/* acosh(x) = log(x + sqrt(x*x-1)) */
double acosh(double x)
{
	union {double f; uint64_t i;} u = {.f = x};
	unsigned e = u.i >> 52 & 0x7ff;

	/* firebox #RNS/#7CD: acosh is defined only on [1,inf). Any x<1 (incl.
	   negatives and -inf) is a domain error -> FE_INVALID; a NaN propagates
	   quietly (x<1 is false for NaN). wasm has no HW fp status word, so raise it
	   here, covering ALL branches (the prior #7CD raise reached only |x|<2, so
	   e.g. acosh(-inf)/acosh(-5) never flagged). */
	if (x < 1)
		feraiseexcept(FE_INVALID);

	if (e < 0x3ff + 1)
		/* |x| < 2, up to 2ulp error in [1,1.125] */
		return log1p(x-1 + sqrt((x-1)*(x-1)+2*(x-1)));
	if (e < 0x3ff + 26)
		/* |x| < 0x1p26 */
		return log(2*x - 1/(x+sqrt(x*x-1)));
	/* |x| >= 0x1p26 or nan */
	return log(x) + 0.693147180559945309417232121458176568;
}
