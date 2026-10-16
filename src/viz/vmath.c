/* The visualizer's arithmetic (vmath.h): fdlibm's sin, cos, exp, log
 * and atan, and the functions built on them.
 *
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 * Developed at SunSoft, a Sun Microsystems, Inc. business. Permission
 * to use, copy, modify, and distribute this software is freely granted,
 * provided that this notice is preserved. (The algorithms and
 * coefficients below are fdlibm's, rewritten without its bit tricks.) */
#include "vmath.h"

#include <math.h>

/* ---- sin and cos ---- */

/* sin on [-pi/4, pi/4]: x + x^3 (S1 + x^2 r(x^2)). */
static double ksin(double x)
{
    static const double S1 = -1.66666666666666324348e-01, S2 = 8.33333333332248946124e-03,
                        S3 = -1.98412698298579493134e-04, S4 = 2.75573137070700676789e-06,
                        S5 = -2.50507602534068634195e-08, S6 = 1.58969099521155010221e-10;
    double z = x * x, v = z * x;
    double r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
    return x + v * (S1 + z * r);
}

/* cos on [-pi/4, pi/4]: 1 - x^2 / 2 + x^4 r(x^2), the first two terms
 * summed so their rounding is kept (musl's __cos). */
static double kcos(double x)
{
    static const double C1 = 4.16666666666666019037e-02, C2 = -1.38888888888741095749e-03,
                        C3 = 2.48015872894767294178e-05, C4 = -2.75573143513906633035e-07,
                        C5 = 2.08757232129817482790e-09, C6 = -1.13596475577881948265e-11;
    double z = x * x;
    double r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
    double hz = 0.5 * z, w = 1.0 - hz;
    return w + (((1.0 - w) - hz) + z * r);
}

/* x - n pi/2 for the nearest n, in three parts of pi/2 (Cody and
 * Waite): exact while n stays below 2^20, and past that only a little
 * off, which the modes' arguments never reach but by the synthesiser's
 * clock after hours, where an error of 1e-8 is inaudible. n's low two
 * bits in *q. */
static double reduce(double x, int *q)
{
    static const double invpio2 = 6.36619772367581382433e-01, pio2_1 = 1.57079632673412561417e+00,
                        pio2_2 = 6.07710050630396597660e-11,
                        pio2_2t = 2.02226624879595063154e-21, pio2_3 = 2.02226624871116645580e-21,
                        pio2_3t = 8.47842766036889956997e-32;
    /* Far out the quotient would leave an integer's range: fmod is
     * exact, so it brings x near at the same cost on every machine. */
    if (!(fabs(x) < 1e15))
        x = fmod(x, 4 * pio2_1);
    double fn = floor(x * invpio2 + 0.5);
    /* The tail of pio2_1 is pio2_2 + pio2_2t, and pio2_2t's is pio2_3
     * + pio2_3t: musl's three rounds, all of them always. */
    double r = x - fn * pio2_1, t = r;
    double w = fn * pio2_2;
    r = t - w;
    w = fn * pio2_2t - ((t - r) - w);
    t = r;
    w = fn * pio2_3;
    r = t - w;
    w = fn * pio2_3t - ((t - r) - w);
    *q = (int)(fmod(fn, 4.0) + 4.0) & 3;
    return r - w;
}

void sk_vm_sincos(double x, double *s, double *c)
{
    if (isnan(x) || isinf(x)) {
        *s = *c = NAN;
        return;
    }
    if (fabs(x) <= 0.7853981633974483) {
        *s = ksin(x);
        *c = kcos(x);
        return;
    }
    int q;
    double y = reduce(x, &q);
    double ks = ksin(y), kc = kcos(y);
    switch (q) {
    case 0:
        *s = ks;
        *c = kc;
        break;
    case 1:
        *s = kc;
        *c = -ks;
        break;
    case 2:
        *s = -ks;
        *c = -kc;
        break;
    default:
        *s = -kc;
        *c = ks;
        break;
    }
}

double sk_vm_sin(double x)
{
    if (fabs(x) <= 0.7853981633974483)
        return ksin(x);
    if (isnan(x) || isinf(x))
        return NAN;
    int q;
    double y = reduce(x, &q);
    switch (q) {
    case 0:
        return ksin(y);
    case 1:
        return kcos(y);
    case 2:
        return -ksin(y);
    default:
        return -kcos(y);
    }
}

double sk_vm_cos(double x)
{
    if (fabs(x) <= 0.7853981633974483)
        return kcos(x);
    if (isnan(x) || isinf(x))
        return NAN;
    int q;
    double y = reduce(x, &q);
    switch (q) {
    case 0:
        return kcos(y);
    case 1:
        return -ksin(y);
    case 2:
        return -kcos(y);
    default:
        return ksin(y);
    }
}

double sk_vm_tan(double x)
{
    double s, c;
    sk_vm_sincos(x, &s, &c);
    return s / c;
}

/* ---- exp and log ---- */

double sk_vm_exp(double x)
{
    static const double ln2hi = 6.93147180369123816490e-01, ln2lo = 1.90821492927058770002e-10,
                        invln2 = 1.44269504088896338700e+00, P1 = 1.66666666666666019037e-01,
                        P2 = -2.77777777770155933842e-03, P3 = 6.61375632143793436117e-05,
                        P4 = -1.65339022054652515390e-06, P5 = 4.13813679705723846039e-08;
    if (isnan(x))
        return x;
    if (x > 709.782712893383973096)
        return INFINITY;
    if (x < -745.13321910194110842)
        return 0;
    /* x = k ln 2 + r, |r| <= ln 2 / 2, r kept as hi - lo. */
    double hi = x, lo = 0;
    int k = 0;
    if (fabs(x) > 0.5 * 0.6931471805599453) {
        k = (int)floor(invln2 * x + 0.5);
        hi = x - k * ln2hi;
        lo = k * ln2lo;
    }
    double r = hi - lo, t = r * r;
    double c = r - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
    double y = 1.0 - ((lo - (r * c) / (2.0 - c)) - hi);
    return k ? ldexp(y, k) : y;
}

double sk_vm_log(double x)
{
    static const double ln2hi = 6.93147180369123816490e-01, ln2lo = 1.90821492927058770002e-10,
                        Lg1 = 6.666666666666735130e-01, Lg2 = 3.999999999940941908e-01,
                        Lg3 = 2.857142874366239149e-01, Lg4 = 2.222219843214978396e-01,
                        Lg5 = 1.818357216161805012e-01, Lg6 = 1.531383769920937332e-01,
                        Lg7 = 1.479819860511658591e-01;
    if (isnan(x) || x < 0)
        return NAN;
    if (x == 0)
        return -INFINITY;
    if (isinf(x))
        return x;
    /* x = 2^k m, sqrt(2) / 2 <= m < sqrt(2), m = 1 + f exactly. */
    int k;
    double m = frexp(x, &k);
    if (m < 0.70710678118654752440) {
        m *= 2;
        k--;
    }
    double f = m - 1.0;
    double s = f / (2.0 + f), z = s * s, w = z * z;
    double t1 = w * (Lg2 + w * (Lg4 + w * Lg6));
    double t2 = z * (Lg1 + w * (Lg3 + w * (Lg5 + w * Lg7)));
    double R = t2 + t1, hfsq = 0.5 * f * f, dk = k;
    return dk * ln2hi - ((hfsq - (s * (hfsq + R) + dk * ln2lo)) - f);
}

double sk_vm_log10(double x)
{
    return sk_vm_log(x) * 0.43429448190325182765;
}

double sk_vm_pow(double x, double y)
{
    if (y == 0)
        return 1;
    if (x == 0)
        return y > 0 ? 0 : INFINITY;
    /* A small whole power by multiplying, exact where the product is,
     * as Math.pow is for (-2) ** 3. */
    if (y == floor(y) && fabs(y) <= 64) {
        double r = 1, b = x;
        for (unsigned n = (unsigned)fabs(y); n; n >>= 1, b *= b)
            if (n & 1)
                r *= b;
        return y < 0 ? 1 / r : r;
    }
    if (x < 0) {
        if (y != floor(y))
            return NAN;
        double r = sk_vm_exp(y * sk_vm_log(-x));
        return fmod(y, 2.0) != 0 ? -r : r;
    }
    if (x == 1)
        return 1;
    return sk_vm_exp(y * sk_vm_log(x));
}

double sk_vm_tanh(double x)
{
    if (isnan(x))
        return x;
    if (x > 20)
        return 1;
    if (x < -20)
        return -1;
    double e = sk_vm_exp(2 * x);
    return (e - 1) / (e + 1);
}

double sk_vm_cbrt(double x)
{
    if (x == 0 || isnan(x) || isinf(x))
        return x;
    double a = fabs(x), r = sk_vm_exp(sk_vm_log(a) / 3);
    /* One Newton step takes the last bits. */
    r = r - (r * r * r - a) / (3 * r * r);
    return x < 0 ? -r : r;
}

/* ---- atan ---- */

double sk_vm_atan(double x)
{
    static const double atanhi[] = {4.63647609000806093515e-01, 7.85398163397448278999e-01,
                                    9.82793723247329054082e-01, 1.57079632679489655800e+00};
    static const double atanlo[] = {2.26987774529616870924e-17, 3.06161699786838301793e-17,
                                    1.39033110312309984516e-17, 6.12323399573676603587e-17};
    static const double aT[] = {3.33333333333329318027e-01,  -1.99999999998764832476e-01,
                                1.42857142725034663711e-01,  -1.11111104054623557880e-01,
                                9.09088713343650656196e-02,  -7.69187620504482999495e-02,
                                6.66107313738753120669e-02,  -5.83357013379057348645e-02,
                                4.97687799461593236017e-02,  -3.65315727442169155270e-02,
                                1.62858201153657823623e-02};
    if (isnan(x))
        return x;
    double a = fabs(x);
    int id = -1;
    if (a >= 7.0 / 16) {
        if (a < 11.0 / 16) {
            id = 0;
            a = (2.0 * a - 1.0) / (2.0 + a);
        } else if (a < 19.0 / 16) {
            id = 1;
            a = (a - 1.0) / (a + 1.0);
        } else if (a < 39.0 / 16) {
            id = 2;
            a = (a - 1.5) / (1.0 + 1.5 * a);
        } else {
            id = 3;
            a = -1.0 / a;
        }
    }
    double z = a * a, w = z * z;
    double s1 = z * (aT[0] + w * (aT[2] + w * (aT[4] + w * (aT[6] + w * (aT[8] + w * aT[10])))));
    double s2 = w * (aT[1] + w * (aT[3] + w * (aT[5] + w * (aT[7] + w * aT[9]))));
    double r = id < 0 ? a - a * (s1 + s2) : atanhi[id] - ((a * (s1 + s2) - atanlo[id]) - a);
    return x < 0 ? -r : r;
}

double sk_vm_atan2(double y, double x)
{
    static const double pi = 3.1415926535897931160e+00, pi_lo = 1.2246467991473531772e-16,
                        pio2 = 1.5707963267948965580e+00;
    if (isnan(x) || isnan(y))
        return NAN;
    if (x == 0)
        return y > 0 ? pio2 : y < 0 ? -pio2 : (signbit(x) ? (signbit(y) ? -pi : pi) : y);
    if (y == 0)
        return x > 0 ? y : (signbit(y) ? -pi : pi);
    if (isinf(x) || isinf(y)) {
        double ax = isinf(x) ? (x > 0 ? 0 : pi) : pio2;
        if (isinf(x) && isinf(y))
            ax = x > 0 ? pi / 4 : 3 * pi / 4;
        return y < 0 ? -ax : ax;
    }
    double z = sk_vm_atan(fabs(y / x));
    if (x > 0)
        return y < 0 ? -z : z;
    z = pi - (z - pi_lo);
    return y < 0 ? -z : z;
}

double sk_vm_asin(double x)
{
    if (!(fabs(x) <= 1))
        return NAN;
    return sk_vm_atan2(x, sqrt((1 - x) * (1 + x)));
}

double sk_vm_round(double x)
{
    return floor(x + 0.5);
}

/* ---- Random numbers ---- */

static uint64_t splitmix(uint64_t *s)
{
    uint64_t z = (*s += 0x9e3779b97f4a7c15u);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9u;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebu;
    return z ^ (z >> 31);
}

void sk_rng_seed(sk_rng *r, uint64_t seed)
{
    for (int i = 0; i < 4; i++)
        r->s[i] = splitmix(&seed);
}

static uint64_t rotl(uint64_t x, int k)
{
    return (x << k) | (x >> (64 - k));
}

uint64_t sk_rng_u64(sk_rng *r)
{
    uint64_t *s = r->s;
    uint64_t out = rotl(s[1] * 5, 7) * 9, t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl(s[3], 45);
    return out;
}

double sk_rng_next(sk_rng *r)
{
    return (double)(sk_rng_u64(r) >> 11) * 0x1.0p-53;
}
