#include <fenv.h>
#include "libm.h"

double __math_oflow(uint32_t sign)
{
	/* firebox #7CD: overflow signals FE_OVERFLOW|FE_INEXACT on hardware; raise
	 * explicitly on wasm. (The raise lives here, not in __math_xflow, so overflow
	 * and underflow stay distinguishable.) */
	feraiseexcept(FE_OVERFLOW | FE_INEXACT);
	return __math_xflow(sign, 0x1p769);
}
