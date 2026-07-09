/* origin: FreeBSD /usr/src/lib/msun/src/s_fmaf.c */
/*-
 * Copyright (c) 2005-2011 David Schultz <das@FreeBSD.ORG>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <fenv.h>
#include <math.h>
#include <stdint.h>

/*
 * Fused multiply-add: Compute x * y + z with a single rounding error.
 *
 * A double has more than twice as much precision than a float, so
 * direct double-precision arithmetic suffices, except where double
 * rounding occurs.
 */
float fmaf(float x, float y, float z)
{
	#pragma STDC FENV_ACCESS ON
	double xy, result;
	union {double f; uint64_t i;} u;
	int e;

	/* firebox #RNS: raise FE_INVALID for the invalid ops that wasm's silent
	   mul/add produce as a quiet nan — 0*inf (either order) and
	   (+-inf)+(-+inf). A quiet-NaN operand alone does NOT raise INVALID. See
	   fma.c for the true-infinity rationale. */
	if ((x == 0 && isinf(y)) || (isinf(x) && y == 0)) {
		feraiseexcept(FE_INVALID);
	} else if (isinf(z) && (isinf(x) || isinf(y)) && !isnan(x) && !isnan(y)
	           && x != 0 && y != 0
	           && (signbit(x) ^ signbit(y)) != signbit(z)) {
		feraiseexcept(FE_INVALID);
	}

	xy = (double)x * y;
	result = xy + z;
	u.f = result;
	e = u.i>>52 & 0x7ff;
	/* Common case: The double precision result is fine. */
	if ((u.i & 0x1fffffff) != 0x10000000 || /* not a halfway case */
		e == 0x7ff ||                   /* NaN */
		(result - xy == z && result - z == xy) || /* exact */
		fegetround() != FE_TONEAREST)       /* not round-to-nearest */
	{
		/*
		underflow may not be raised correctly, example:
		fmaf(0x1p-120f, 0x1p-120f, 0x1p-149f)
		*/
#if defined(FE_INEXACT) && defined(FE_UNDERFLOW)
		/* firebox #RNS: the upstream path relies on the double add xy+z setting
		   FE_INEXACT, but wasm's software fenv sets no flag for f64 add (and the
		   add is frequently exact in double anyway, as in the example above), so
		   it never fires. Detect the underflow directly: in the float-subnormal
		   magnitude range the narrowing result->float is inexact iff
		   (double)(float)result != result, and a tiny inexact result is a genuine
		   UNDERFLOW|INEXACT. An exactly-representable float subnormal stays
		   flagless. */
		if (e < 0x3ff-126 && e >= 0x3ff-149) {
			float fr = (float)result;
			/* firebox #FPH (RC3): the narrowing check `(double)fr != result`
			   only catches inexactness from result->float, and MISSES the case
			   the comment above names — fmaf(0x1p-120,0x1p-120,0x1p-149):
			   result=0x1p-149 is an EXACT float subnormal (narrowing is exact),
			   but the DOUBLE ADD xy+z rounded away the tiny 0x1p-240 term. Also
			   fire when the add was inexact, detected by musl's own Dekker
			   error-free test (the negation of the `exact` clause on line 65):
			   result==xy+z exactly iff (result-xy==z && result-z==xy). */
			if ((double)fr != result || result - xy != z || result - z != xy)
				feraiseexcept(FE_UNDERFLOW | FE_INEXACT);
		}
#endif
		z = result;
		return z;
	}

	/*
	 * If result is inexact, and exactly halfway between two float values,
	 * we need to adjust the low-order bit in the direction of the error.
	 */
#ifdef FE_TOWARDZERO
	fesetround(FE_TOWARDZERO);
#endif
#ifdef __wasilibc_unmodified_upstream // WASI doesn't need old GCC workarounds
	volatile double vxy = xy;  /* XXX work around gcc CSE bug */
#else
	double vxy = xy;
#endif
	double adjusted_result = vxy + z;
	fesetround(FE_TONEAREST);
	if (result == adjusted_result) {
		u.f = adjusted_result;
		u.i++;
		adjusted_result = u.f;
	}
	z = adjusted_result;
	return z;
}
