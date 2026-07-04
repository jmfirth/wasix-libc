#include "libm.h"

/* atanh(x) = log((1+x)/(1-x))/2 = log1p(2x/(1-x))/2 ~= x + x^3/3 + o(x^5) */
float atanhf(float x)
{
	union {float f; uint32_t i;} u = {.f = x};
	unsigned s = u.i >> 31;
	float_t y;

	/* |x| */
	u.i &= 0x7fffffff;
	y = u.f;

	/* firebox #C36: explicit domain flags (wasm has no HW fp status word).
	   atanh is defined only on (-1,1): |x|==1 → ±inf (DIVBYZERO); 1<|x|<inf and
	   ±inf → NaN (INVALID); a NaN input propagates quietly (no flag). */
	if (u.i >= 0x3f800000) {
		if (u.i == 0x3f800000)
			feraiseexcept(FE_DIVBYZERO);   /* |x| == 1.0 */
		else if (u.i <= 0x7f800000)
			feraiseexcept(FE_INVALID);     /* 1<|x|<inf or |x|==inf (not NaN) */
	}

	if (u.i < 0x3f800000 - (1<<23)) {
		if (u.i < 0x3f800000 - (32<<23)) {
			/* firebox #C36: sub-normal |x| → atanhf(x) ~= x is an inexact
			   underflow (replaces the FORCE_EVAL, a no-op on wasm). */
			if (u.i != 0 && u.i < (1<<23))
				feraiseexcept(FE_UNDERFLOW | FE_INEXACT);
		} else {
			/* |x| < 0.5, up to 1.7ulp error */
			y = 0.5f*log1pf(2*y + 2*y*y/(1-y));
		}
	} else {
		/* avoid overflow */
		y = 0.5f*log1pf(2*(y/(1-y)));
	}
	return s ? -y : y;
}
