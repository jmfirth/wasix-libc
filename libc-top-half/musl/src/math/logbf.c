#include <math.h>
#include <fenv.h>	/* #7CD: software fenv raises */

float logbf(float x)
{
	if (!isfinite(x))
		return x * x;
	if (x == 0)
		{ feraiseexcept(FE_DIVBYZERO); return -1/(x*x); }  /* #7CD */
	return ilogbf(x);
}
