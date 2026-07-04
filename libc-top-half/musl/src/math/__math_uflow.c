#include <fenv.h>
#include "libm.h"

double __math_uflow(uint32_t sign)
{
	/* firebox #7CD: underflow signals FE_UNDERFLOW|FE_INEXACT on hardware; raise
	 * explicitly on wasm. */
	feraiseexcept(FE_UNDERFLOW | FE_INEXACT);
	return __math_xflow(sign, 0x1p-767);
}
