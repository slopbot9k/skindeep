/* The visualizer's arithmetic: within an ulp or two of libm, and the
 * same numbers from the same seed. */
#include "../../src/viz/vmath.h"
#include "../test.h"

#include <math.h>

/* The worst relative error of f against g over n points from lo to hi,
 * an absolute one where g is near 0. */
static double worst(double (*f)(double), double (*g)(double), double lo, double hi, int n)
{
    double bad = 0;
    for (int i = 0; i <= n; i++) {
        double x = lo + (hi - lo) * i / n, want = g(x), got = f(x);
        double err = fabs(got - want) / SK_MAX(1.0, fabs(want));
        bad = SK_MAX(bad, err);
    }
    return bad;
}

static void test_functions(void)
{
    CHECK(worst(sk_vm_sin, sin, -50, 50, 100003) < 4e-16);
    CHECK(worst(sk_vm_cos, cos, -50, 50, 100003) < 4e-16);
    CHECK(worst(sk_vm_sin, sin, -2e6, 2e6, 20011) < 1e-15);
    CHECK(worst(sk_vm_exp, exp, -700, 700, 100003) < 4e-16);
    CHECK(worst(sk_vm_log, log, 1e-300, 1e300, 100003) < 4e-16);
    CHECK(worst(sk_vm_log, log, 0.01, 10, 100003) < 4e-16);
    CHECK(worst(sk_vm_atan, atan, -100, 100, 100003) < 4e-16);
    CHECK(worst(sk_vm_tanh, tanh, -30, 30, 100003) < 1e-15);
    CHECK(worst(sk_vm_cbrt, cbrt, -1e6, 1e6, 100003) < 4e-16);
    CHECK(worst(sk_vm_asin, asin, -1, 1, 100003) < 1e-15);
    double bad = 0;
    for (int i = -200; i <= 200; i++)
        for (int j = -200; j <= 200; j++) {
            double y = i * 0.37, x = j * 0.41;
            bad = SK_MAX(bad, fabs(sk_vm_atan2(y, x) - atan2(y, x)));
        }
    CHECK(bad < 1e-15);
    CHECK(sk_vm_atan2(0, -1) == atan2(0, -1));
    CHECK(sk_vm_atan2(1, 0) == atan2(1, 0));
    for (double x = 0.01; x < 100; x *= 1.37)
        for (double y = -3; y < 3; y += 0.29)
            CHECK(fabs(sk_vm_pow(x, y) / pow(x, y) - 1) < 1e-14);
    CHECK(sk_vm_pow(0, 2) == 0 && sk_vm_pow(5, 0) == 1 && sk_vm_pow(-2, 3) == -8);
    CHECK(sk_vm_round(2.5) == 3 && sk_vm_round(-2.5) == -2 && sk_vm_round(-2.6) == -3);
    double s, c;
    sk_vm_sincos(1e7, &s, &c);
    CHECK(s == sk_vm_sin(1e7) && c == sk_vm_cos(1e7));
}

static void test_rng(void)
{
    sk_rng a, b;
    sk_rng_seed(&a, 42);
    sk_rng_seed(&b, 42);
    double sum = 0, lo = 1, hi = 0;
    for (int i = 0; i < 100000; i++) {
        double x = sk_rng_next(&a);
        CHECK(x == sk_rng_next(&b));
        sum += x;
        lo = SK_MIN(lo, x);
        hi = SK_MAX(hi, x);
    }
    CHECK(lo >= 0 && hi < 1);
    CHECK(fabs(sum / 100000 - 0.5) < 0.01);
    /* The first number of seed 1, pinned: a change would change every
     * golden image. */
    sk_rng_seed(&a, 1);
    CHECK_INT(sk_rng_u64(&a) % 1000003, 298468);
}

const sk_test sk_suite_viz_math[] = {T(test_functions), T(test_rng), {0}};
