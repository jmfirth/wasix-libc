#include <math.h>
#include <fenv.h>	/* #7CD: software fenv raises */

/*
special cases:
	logb(+-0) = -inf, and raise divbyzero
	logb(+-inf) = +inf
	logb(nan) = nan
*/

double logb(double x)
{
	if (!isfinite(x))
		return x * x;
	if (x == 0)
		{ feraiseexcept(FE_DIVBYZERO); return -1/(x*x); }  /* #7CD */
	return ilogb(x);
}
