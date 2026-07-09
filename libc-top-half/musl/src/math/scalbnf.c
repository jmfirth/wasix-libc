#include <math.h>
#include <stdint.h>
#include <fenv.h>	/* firebox #RNS: software fenv — raise range-error flags explicitly on wasm */

float scalbnf(float x, int n)
{
	union {float f; uint32_t i;} u;
	union {float f; uint32_t i;} ux = {.f = x};	/* firebox #RNS: original x for the exact-check */
	float_t y = x;

	if (n > 127) {
		y *= 0x1p127f;
		n -= 127;
		if (n > 127) {
			y *= 0x1p127f;
			n -= 127;
			if (n > 127)
				n = 127;
		}
	} else if (n < -126) {
		y *= 0x1p-126f * 0x1p24f;
		n += 126 - 24;
		if (n < -126) {
			y *= 0x1p-126f * 0x1p24f;
			n += 126 - 24;
			if (n < -126)
				n = -126;
		}
	}
	u.i = (uint32_t)(0x7f+n)<<23;
	x = y * u.f;

	/* firebox #RNS: raise the range-error flags explicitly (wasm has no HW fp
	   status word); mirror of scalbn.c. inf-from-finite is OVERFLOW|INEXACT; a
	   non-zero subnormal result is UNDERFLOW only when inexact, i.e. when the
	   low s = -149-(e_x+n) significand bits of the ORIGINAL x are not all zero
	   (an exact subnormal such as scalbnf(0x1p127,-276)=0x1p-149 must NOT
	   signal). */
	if (isinf(x)) {
		int ex0 = (ux.i >> 23) & 0xff;
		if (ex0 != 0xff && (ux.i & 0x7fffffffU) != 0)
			feraiseexcept(FE_OVERFLOW | FE_INEXACT);
	} else if (x != 0 && fabsf(x) < 0x1p-126f) {
		int ex0 = (ux.i >> 23) & 0xff;
		uint32_t sig = ux.i & 0x7fffff;
		int e_x;
		if (ex0 == 0)
			e_x = -149;		/* subnormal x: value = sig * 2^-149 */
		else {
			sig |= 0x800000;	/* implicit leading 1 */
			e_x = ex0 - 150;	/* value = sig * 2^(ex0-127-23) */
		}
		int s = -149 - (e_x + n);	/* bits dropped in the denormal range */
		int inexact;
		if (s <= 0)
			inexact = 0;
		else if (s >= 24)
			inexact = 1;		/* every significand bit dropped, sig != 0 */
		else
			inexact = (sig & (((uint32_t)1 << s) - 1)) != 0;
		if (inexact)
			feraiseexcept(FE_UNDERFLOW | FE_INEXACT);
	}
	return x;
}
