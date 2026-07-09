#include <math.h>
#include <stdint.h>
#include <fenv.h>	/* firebox #RNS: software fenv — raise range-error flags explicitly on wasm */

double scalbn(double x, int n)
{
	union {double f; uint64_t i;} u;
	union {double f; uint64_t i;} ux = {.f = x};	/* firebox #RNS: original x for the exact-check */
	double_t y = x;
	int n0 = n;	/* firebox #FPH: original n; the reduction below mutates n, but the subnormal exact-check needs the ORIGINAL scaling exponent */

	if (n > 1023) {
		y *= 0x1p1023;
		n -= 1023;
		if (n > 1023) {
			y *= 0x1p1023;
			n -= 1023;
			if (n > 1023)
				n = 1023;
		}
	} else if (n < -1022) {
		/* make sure final n < -53 to avoid double
		   rounding in the subnormal range */
		y *= 0x1p-1022 * 0x1p53;
		n += 1022 - 53;
		if (n < -1022) {
			y *= 0x1p-1022 * 0x1p53;
			n += 1022 - 53;
			if (n < -1022)
				n = -1022;
		}
	}
	u.i = (uint64_t)(0x3ff+n)<<52;
	x = y * u.f;

	/* firebox #RNS: wasm has no HW fp status word, so scalbn must raise the
	   range-error flags explicitly (ldexp/scalbln inherit via delegation).
	   - A finite non-zero input scaled to inf is a genuine OVERFLOW|INEXACT.
	   - A non-zero subnormal result is an UNDERFLOW *only when it is inexact*,
	     i.e. when bits are shifted off in the denormal range. x*2^n is exact
	     iff the low s = -1074-(e_x+n) significand bits of the ORIGINAL x are all
	     zero, so an exact subnormal (e.g. scalbn(0x1p1023,-2097)=0x1p-1074, or
	     scalbn(0x1p-1074,0)) must NOT signal. */
	if (isinf(x)) {
		int ex0 = (ux.i >> 52) & 0x7ff;
		if (ex0 != 0x7ff && (ux.i & 0x7fffffffffffffffULL) != 0)
			feraiseexcept(FE_OVERFLOW | FE_INEXACT);
	} else if (x != 0 && fabs(x) < 0x1p-1022) {
		int ex0 = (ux.i >> 52) & 0x7ff;
		uint64_t sig = ux.i & 0xfffffffffffffULL;
		int e_x;
		if (ex0 == 0)
			e_x = -1074;		/* subnormal x: value = sig * 2^-1074 */
		else {
			sig |= 0x10000000000000ULL;	/* implicit leading 1 */
			e_x = ex0 - 1075;	/* value = sig * 2^(ex0-1023-52) */
		}
		int s = -1074 - (e_x + n0);	/* bits dropped in the denormal range */
		int inexact;
		if (s <= 0)
			inexact = 0;		/* result representable without loss */
		else if (s >= 53)
			inexact = 1;		/* every significand bit dropped, sig != 0 */
		else
			inexact = (sig & (((uint64_t)1 << s) - 1)) != 0;
		if (inexact)
			feraiseexcept(FE_UNDERFLOW | FE_INEXACT);
	}
	return x;
}
