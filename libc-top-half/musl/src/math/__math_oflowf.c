#include <fenv.h>
#include "libm.h"

float __math_oflowf(uint32_t sign)
{
	/* firebox #7CD: raise FE_OVERFLOW|FE_INEXACT explicitly (see __math_oflow.c). */
	feraiseexcept(FE_OVERFLOW | FE_INEXACT);
	return __math_xflowf(sign, 0x1p97f);
}
