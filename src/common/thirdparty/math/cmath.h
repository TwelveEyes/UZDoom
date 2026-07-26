#ifndef __CMATH_H
#define __CMATH_H

#include "m_round.h"

#define USE_CUSTOM_MATH	// we want repreducably reliable results, even at the cost of performance
#define USE_FAST_MATH	// use faster table-based sin and cos variants with limited precision (sufficient for Doom gameplay)

#define CMATHFLOOR std::floor
#define CMATHABS std::abs
#define FMATHFLOOR floorf
#define FMATHABS fabsf

extern"C"
{
double c_asin(double);
double c_acos(double);
double c_atan(double);
double c_atan2(double, double);
double c_sin(double);
double c_cos(double);
double c_tan(double);
double c_cot(double);
double c_sqrt(double);
double c_sinh(double);
double c_cosh(double);
double c_tanh(double);
double c_exp(double);
double c_log(double);
double c_log10(double);
double c_pow(double, double);
}


// [Sherbet] very fast cos/sin implementation haha wtf
// the old gzdoom/zdoom used a lookup table for it's cos/sin functions, which is slow (it involved divisions and a table that could miss cache)
// i've found that using this instead of the lookup table is way faster (probably because it's inlined + it's just a few calculations) and MORE accurate
// the biggest trick here is the compile-time division tp_pi which saves us a TON of trouble so this entire thing just ends up being a
// few subtractions, additions, and multiplications, absolutely no division involved at all. the extra precision feed is optional as well
// but left in due to game logic requiring it (without it theres a 6% err on some ranges, untolerable). the renderer probably doesn't though.
inline double fastcos(double x) {
	const double tp_pi = 1. / (2. * M_PI);
	x *= tp_pi;
	x -= double(.25) + CMATHFLOOR(x + double(.25));
	x *= double(16.) * (CMATHABS(x) - double(.5));
	x += double(.225) * x * (CMATHABS(x) - double(1.)); // extra precision feed
	return x;
}

inline double fastsin(double x) {
	const double tp_pi = -1. / (2. * M_PI); // neg for sine
	x *= tp_pi;
	x += CMATHFLOOR(double(.5) - x); // change for usage with sine
	x *= double(16.) * (CMATHABS(x) - double(.5));
	x += double(.225) * x * (CMATHABS(x) - double(1.)); // extra precision feed
	return x;
}

// [Sherbet] just realized that this shit cannot be trusted in fucking ohio so im going to manually unroll it
inline double fastcosdeg(double x) {
	const double tp_pi = M_PI / (360. * M_PI);
	x *= tp_pi;
	x -= double(.25) + CMATHFLOOR(x + double(.25));
	x *= double(16.) * (CMATHABS(x) - double(.5));
	x += double(.225) * x * (CMATHABS(x) - double(1.)); // extra precision feed
	return x;
}

inline double fastsindeg(double x) {
	const double tp_pi = -M_PI / (360. * M_PI); // neg for sine
	x *= tp_pi;
	x += CMATHFLOOR(double(.5) - x); // change for usage with sine
	x *= double(16.) * (CMATHABS(x) - double(.5));
	x += double(.225) * x * (CMATHABS(x) - double(1.)); // extra precision feed
	return x;
}

// [Sherbet] float versions, used in renderer math.
inline float float_fastcosdeg(float x) {
	const float tp_pi = 3.14159265358979323846f / (360.f * 3.14159265358979323846f);
	x *= tp_pi;
	x -= float(.25f) + FMATHFLOOR(x + float(.25f));
	x *= float(16.f) * (FMATHABS(x) - float(.5f));
	x += float(.225f) * x * (FMATHABS(x) - float(1.f)); // extra precision feed
	return x;
}

inline float float_fastsindeg(float x) {
	const float tp_pi = -3.14159265358979323846f / (360.f * 3.14159265358979323846f); // neg for sine
	x *= tp_pi;
	x += FMATHFLOOR(float(.5f) - x); // change for usage with sine
	x *= float(16.f) * (FMATHABS(x) - float(.5f));
	x += float(.225f) * x * (FMATHABS(x) - float(1.f)); // extra precision feed
	return x;
}

// High accuracy degree, use only when absolutely required.
inline double sindeg(double v) {
#ifdef USE_CUSTOM_MATH
	return c_sin(v * (3.14159265358979323846 / 180.));
#else
	return sin(v * (3.14159265358979323846 / 180.));
#endif
}

inline double cosdeg(double v) {
#ifdef USE_CUSTOM_MATH
	return c_cos(v * (3.14159265358979323846 / 180.));
#else
	return cos(v * (3.14159265358979323846 / 180.));
#endif
}


#ifndef USE_CUSTOM_MATH
#define g_asin  asin
#define g_acos  acos
#define g_atan  atan
#define g_atan2 atan2
#define g_sin	sin
#define g_cos	cos
#define g_sindeg	sindeg
#define g_cosdeg	cosdeg
#define g_tan	tan
#define g_cot	cot
#define g_sqrt  sqrt
#define g_sinh  sinh
#define g_cosh  cosh
#define g_tanh  tanh
#define g_exp	exp
#define g_log	log
#define g_log10 log10
#define g_pow	pow
#else
#define g_asin  c_asin
#define g_acos  c_acos
#define g_atan  c_atan
#define g_atan2 c_atan2
#ifndef USE_FAST_MATH
#define g_sindeg	sindeg
#define g_cosdeg	cosdeg
#define g_sin	c_sin
#define g_cos	c_cos
#else
#define g_sindeg	fastsindeg
#define g_cosdeg	fastcosdeg
#define g_sinbam	fastsin
#define g_cosbam	fastcos
#define g_sin	fastsin
#define g_cos	fastcos
#endif
#define g_tan	c_tan
#define g_cot	c_cot
#define g_sqrt  c_sqrt
#define g_sinh  c_sinh
#define g_cosh  c_cosh
#define g_tanh  c_tanh
#define g_exp	c_exp
#define g_log	c_log
#define g_log10 c_log10
#define g_pow	c_pow
#endif



#endif
