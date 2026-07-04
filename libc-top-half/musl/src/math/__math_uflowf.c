#include <fenv.h>
#include "libm.h"

float __math_uflowf(uint32_t sign)
{
	/* firebox #7CD: raise FE_UNDERFLOW|FE_INEXACT explicitly (see __math_uflow.c). */
	feraiseexcept(FE_UNDERFLOW | FE_INEXACT);
	return __math_xflowf(sign, 0x1p-95f);
}
