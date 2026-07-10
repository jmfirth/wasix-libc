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
		} else if (e < 0x3ff-149 && result != 0) {
			/* firebox #9EB: below the smallest float subnormal (2^-149) a nonzero
			   result narrows to +-0 (total flush) and wasm's f32.demote raises no
			   software-fenv flag — a genuine total underflow. No value in
			   (0, 2^-149) is a representable float, so any nonzero result here is
			   inexact; the sign is already correct (result carries it and the
			   narrowing preserves it), so only the flag is owed. Same class as the
			   fma.c z==0 total-underflow path. */
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
	/* firebox #9EB: FE_TOWARDZERO is undefined on wasm (arch/generic/bits/fenv.h
	   defines only FE_TONEAREST=0) and the #7CD software fenv's fesetround is inert
	   for arithmetic, so the upstream halfway correction here was preprocessed out
	   and the `result == adjusted_result` test was always true — every halfway case
	   rounded UP, so vectors whose true sum is below the float midpoint came out ~2
	   ULP high. Recover the exact rounding residual with a Knuth 2Sum instead: xy is
	   exact (a 48-bit product inside a 53-bit double) and z is exact (a widened
	   float), so result = fl(xy + z) has a single rounding whose residual `err`
	   satisfies result + err == xy + z EXACTLY (Knuth 2Sum, not Fast2Sum — z may
	   exceed xy in magnitude). The residual's sign is exactly what FE_TOWARDZERO was
	   meant to discover, so no rounding mode is needed. Only normal-float halfways
	   reach here (the 0x10000000 low-29-bit mask above matches a normal-float
	   midpoint); subnormal-float halfways miss that mask and narrow in the common
	   case, so u.i +/- 1 always steps within the halfway mantissa pattern and never
	   crosses an exponent boundary or zero. */
	double bs  = result - xy;
	double as  = result - bs;
	double err = (xy - as) + (z - bs);   /* result + err == xy + z, exact */
	if (err != 0) {
		u.f = result;
		if (signbit(err) == signbit(result))
			u.i++;   /* |xy+z| > |result|: step one ULP away from zero */
		else
			u.i--;   /* |xy+z| < |result|: step one ULP toward zero */
		result = u.f;
	}
	z = result;   /* narrows double->float; the perturbed value is no longer a tie */
	return z;
}
