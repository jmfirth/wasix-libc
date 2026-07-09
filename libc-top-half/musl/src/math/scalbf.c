/* origin: FreeBSD /usr/src/lib/msun/src/e_scalbf.c */
/*
 * Conversion to float by Ian Lance Taylor, Cygnus Support, ian@cygnus.com.
 */
/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 */

#define _GNU_SOURCE
#include <math.h>
#include <fenv.h>	/* firebox #RNS: software fenv — raise FE_INVALID for invalid scalb ops on wasm */

float scalbf(float x, float fn)
{
	if (isnan(x) || isnan(fn)) return x*fn;	/* quiet nan propagates, no flag */
	if (!isfinite(fn)) {
		float r = fn > 0.0f ? x*fn : x/(-fn);
		/* firebox #RNS: 0*inf or inf/inf is an invalid operation -> nan; wasm's
		   silent mul/div don't flag it. See scalb.c. */
		if (isnan(r))
			feraiseexcept(FE_INVALID);
		return r;
	}
	if (rintf(fn) != fn) {
		/* firebox #RNS: non-integer exponent = domain error; rintf(fn) above
		   already raised FE_INEXACT, add FE_INVALID for the 0.0/0.0 nan. */
		feraiseexcept(FE_INVALID);
		return (fn-fn)/(fn-fn);
	}
	if ( fn > 65000.0f) return scalbnf(x, 65000);
	if (-fn > 65000.0f) return scalbnf(x,-65000);
	return scalbnf(x,(int)fn);
}
