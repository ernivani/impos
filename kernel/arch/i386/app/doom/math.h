/* Stub <math.h> for ImposOS doom port — doom uses fixed-point math */
#ifndef _DOOM_MATH_H
#define _DOOM_MATH_H

static inline double fabs(double x) { return x < 0 ? -x : x; }
static inline float fabsf(float x) { return x < 0 ? -x : x; }

#endif
