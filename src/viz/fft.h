/* A radix-2 complex FFT, in place over strided points, as the Quantum
 * mode's split-step method needs it (app/viz/quantum.js) and as a
 * spectrum of the synthesiser's samples would. */
#ifndef SK_VIZ_FFT_H
#define SK_VIZ_FFT_H

#include "vmath.h"

/* The bit reversal and the twiddles of an n-point transform, n a power
 * of two. */
typedef struct {
    int n;
    uint32_t *rev;
    double *cos, *sin; /* n / 2 each */
} sk_fft_plan;

/* False when n is not a power of two from 2 to 2^20. */
bool sk_fft_plan_init(sk_fft_plan *p, int n);
void sk_fft_plan_free(sk_fft_plan *p);
/* The n points re[off + i stride], im[...], in place. sign -1 forward,
 * 1 back, unscaled: back after forward gives n times the input. */
void sk_fft(double *re, double *im, size_t off, size_t stride, const sk_fft_plan *p, int sign);

/* The magnitudes of the spectrum of n real samples (n a power of two,
 * at most 2^20), Hann-windowed: out[k] for k from 0 to n / 2, scaled
 * so a full-scale sine at bin k gives about 1 there. */
void sk_fft_spectrum(const float *samples, int n, double *out);

#endif
