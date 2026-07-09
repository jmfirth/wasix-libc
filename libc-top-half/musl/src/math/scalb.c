/* origin: FreeBSD /usr/src/lib/msun/src/e_scalb.c */
/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunSoft, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 */
/*
 * scalb(x, fn) is provide for
 * passing various standard test suite. One
 * should use scalbn() instead.
 */

#define _GNU_SOURCE
#include <math.h>
#include <fenv.h>	/* firebox #RNS: software fenv — raise FE_INVALID for invalid scalb ops on wasm */

double scalb(double x, double fn)
{
	if (isnan(x) || isnan(fn))
		return x*fn;			/* quiet nan propagates, no flag */
	if (!isfinite(fn)) {
		double r = fn > 0.0 ? x*fn : x/(-fn);
		/* firebox #RNS: 0*inf (fn=+inf,x=0) or inf/inf (fn=-inf,x=inf) is an
		   invalid operation -> nan; wasm's silent mul/div don't flag it. */
		if (isnan(r))
			feraiseexcept(FE_INVALID);
		return r;
	}
	if (rint(fn) != fn) {
		/* firebox #RNS: a non-integer exponent is a domain error. The rint(fn)
		   probe above already raised FE_INEXACT (fn non-integer) exactly as
		   hardware does; add FE_INVALID for the 0.0/0.0 nan below (wasm doesn't
		   flag it), giving the expected INEXACT|INVALID. */
		feraiseexcept(FE_INVALID);
		return (fn-fn)/(fn-fn);
	}
	if ( fn > 65000.0) return scalbn(x, 65000);
	if (-fn > 65000.0) return scalbn(x,-65000);
	return scalbn(x,(int)fn);
}
