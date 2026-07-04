#include <fenv.h>
#include "libm.h"

double __math_invalid(double x)
{
	/* firebox #7CD: wasm has no HW FP flags, so raise FE_INVALID explicitly.
	 * Guard on !isnan(x) to match the exact semantics of (x-x)/(x-x): a finite
	 * or infinite x yields 0/0 or inf-inf which signals INVALID, but an already-
	 * quiet-NaN x propagates quietly and must NOT raise (e.g. sqrt(NaN)). */
	if (!isnan(x))
		feraiseexcept(FE_INVALID);
	return (x - x) / (x - x);
}
