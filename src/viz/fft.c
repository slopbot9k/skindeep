/* The FFT (fft.h), as app/viz/quantum.js writes it: the points put in
 * bit-reversed order, then butterflies of growing size. */
#include "fft.h"

#include <math.h>

bool sk_fft_plan_init(sk_fft_plan *p, int n)
{
    int bits = 0;
    while (bits <= 20 && (1 << bits) < n)
        bits++;
    if (n < 2 || bits > 20 || (1 << bits) != n)
        return false;
    p->n = n;
    p->rev = sk_xreallocarray(NULL, (size_t)n, sizeof *p->rev);
    p->cos = sk_xreallocarray(NULL, (size_t)n / 2, sizeof *p->cos);
    p->sin = sk_xreallocarray(NULL, (size_t)n / 2, sizeof *p->sin);
    for (int i = 0; i < n; i++) {
        uint32_t r = 0;
        for (int b = 0; b < bits; b++)
            r |= (uint32_t)((i >> b) & 1) << (bits - 1 - b);
        p->rev[i] = r;
    }
    for (int k = 0; k < n / 2; k++)
        sk_vm_sincos(SK_TAU * k / n, &p->sin[k], &p->cos[k]);
    return true;
}

void sk_fft_plan_free(sk_fft_plan *p)
{
    free(p->rev);
    free(p->cos);
    free(p->sin);
    p->rev = NULL;
    p->cos = p->sin = NULL;
}

void sk_fft(double *re, double *im, size_t off, size_t stride, const sk_fft_plan *p, int sign)
{
    size_t n = (size_t)p->n;
    for (size_t i = 0; i < n; i++) {
        size_t j = p->rev[i];
        if (j <= i)
            continue;
        size_t a = off + i * stride, b = off + j * stride;
        double t = re[a];
        re[a] = re[b];
        re[b] = t;
        t = im[a];
        im[a] = im[b];
        im[b] = t;
    }
    for (size_t size = 2; size <= n; size *= 2) {
        size_t half = size / 2, tstep = n / size;
        for (size_t i = 0; i < n; i += size) {
            for (size_t k = 0; k < half; k++) {
                double wr = p->cos[k * tstep], wi = sign * p->sin[k * tstep];
                size_t a = off + (i + k) * stride, b = a + half * stride;
                double tr = wr * re[b] - wi * im[b];
                double ti = wr * im[b] + wi * re[b];
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
        }
    }
}

void sk_fft_spectrum(const float *samples, int n, double *out)
{
    sk_fft_plan p;
    if (!sk_fft_plan_init(&p, n))
        return;
    double *re = sk_xcalloc((size_t)n, sizeof *re), *im = sk_xcalloc((size_t)n, sizeof *im);
    for (int i = 0; i < n; i++)
        re[i] = samples[i] * (0.5 - 0.5 * sk_vm_cos(SK_TAU * i / (n - 1)));
    sk_fft(re, im, 0, 1, &p, -1);
    for (int k = 0; k <= n / 2; k++)
        out[k] = sqrt(re[k] * re[k] + im[k] * im[k]) / (n / 4.0);
    free(re);
    free(im);
    sk_fft_plan_free(&p);
}
