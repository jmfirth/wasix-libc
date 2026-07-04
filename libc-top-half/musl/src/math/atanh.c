#include "libm.h"

/* atanh(x) = log((1+x)/(1-x))/2 = log1p(2x/(1-x))/2 ~= x + x^3/3 + o(x^5) */
double atanh(double x)
{
	union {double f; uint64_t i;} u = {.f = x};
	unsigned e = u.i >> 52 & 0x7ff;
	unsigned s = u.i >> 63;
	double_t y;

	/* |x| */
	u.i &= (uint64_t)-1/2;
	y = u.f;

	/* firebox #C36: explicit domain flags (wasm has no HW fp status word).
	   atanh is defined only on (-1,1): |x|==1 → ±inf (DIVBYZERO); 1<|x|<inf and
	   ±inf → NaN (INVALID); a NaN input propagates quietly (no flag). The result
	   itself is still produced by the existing arithmetic below. */
	if (e >= 0x3ff) {
		uint64_t m = u.i & 0xfffffffffffffULL;
		if (e == 0x3ff && m == 0)
			feraiseexcept(FE_DIVBYZERO);   /* |x| == 1.0 */
		else if (e != 0x7ff || m == 0)
			feraiseexcept(FE_INVALID);     /* 1<|x|<inf or |x|==inf (not NaN) */
	}

	if (e < 0x3ff - 1) {
		if (e < 0x3ff - 32) {
			/* firebox #C36: sub-normal |x| → atanh(x) ~= x is an inexact
			   underflow (replaces the FORCE_EVAL, a no-op on wasm). */
			if (e == 0 && y != 0)
				feraiseexcept(FE_UNDERFLOW | FE_INEXACT);
		} else {
			/* |x| < 0.5, up to 1.7ulp error */
			y = 0.5*log1p(2*y + 2*y*y/(1-y));
		}
	} else {
		/* avoid overflow */
		y = 0.5*log1p(2*(y/(1-y)));
	}
	return s ? -y : y;
}
