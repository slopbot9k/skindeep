/* The visualizer's arithmetic (decisions 0015, 0036): the functions of
 * libm the modes call, computed here from the four operations and sqrt
 * alone, so that a mode's frame depends only on its inputs and not on
 * the machine's libm. glibc and Apple's libm may round sin or exp apart
 * by an ulp, and a simulation that is chaotic, as the gravity modes
 * are, carries such an ulp into a different picture within seconds.
 *
 * Each function is fdlibm's algorithm and coefficients (Sun's freely
 * distributable code, as musl keeps it), accurate to an ulp or two.
 * Every file that includes this header has the contraction of a * b + c
 * into one fused rounding turned off: clang contracts by default, and
 * on arm64 that changes the bits, while x86-64 has no fused multiply
 * to contract to. GCC contracts nothing under -std=c17 and knows no
 * such pragma. So every machine with IEEE doubles computes the same
 * bits. floor, ceil, trunc, fabs, fmod, ldexp and sqrt come from libm
 * as they are: IEEE makes them exact.
 *
 * Also here: the modes' random numbers, a seeded generator in place of
 * JavaScript's Math.random. */
#ifndef SK_VIZ_VMATH_H
#define SK_VIZ_VMATH_H

#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#endif

#include "../base/base.h"

#define SK_PI 3.14159265358979323846
#define SK_TAU 6.28318530717958647692

double sk_vm_sin(double x);
double sk_vm_cos(double x);
/* Both at once, from one reduction. */
void sk_vm_sincos(double x, double *s, double *c);
double sk_vm_tan(double x);
double sk_vm_exp(double x);
/* NaN below 0, -infinity at 0. */
double sk_vm_log(double x);
double sk_vm_log10(double x);
/* exp(y log x) for x > 0; 0 to a positive power is 0, anything to the
 * power 0 is 1, and a negative x takes an integer y's sign. */
double sk_vm_pow(double x, double y);
double sk_vm_atan(double x);
double sk_vm_atan2(double y, double x);
double sk_vm_asin(double x);
double sk_vm_tanh(double x);
double sk_vm_cbrt(double x);

/* JavaScript's Math.round: halves round up, toward +infinity. */
double sk_vm_round(double x);
/* x clamped to lo..hi; NaN gives lo. */
static inline double sk_vm_clamp(double x, double lo, double hi)
{
    return x >= lo ? (x <= hi ? x : hi) : lo;
}

/* xoshiro256** seeded by splitmix64 (Blackman and Vigna, public
 * domain). */
typedef struct {
    uint64_t s[4];
} sk_rng;

void sk_rng_seed(sk_rng *r, uint64_t seed);
uint64_t sk_rng_u64(sk_rng *r);
/* Uniform on [0, 1), 53 bits: Math.random's place. */
double sk_rng_next(sk_rng *r);

#endif
