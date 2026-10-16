/* The Quantum mode's wave (quantum.c), for its tests: quantum.js's Wave,
 * a wave function on an nx by ny periodic grid, index y nx + x, evolved
 * by the time-dependent Schroedinger equation, hbar = m = 1, by the
 * split-step Fourier method. */
#ifndef SK_VIZ_QUANTUM_H
#define SK_VIZ_QUANTUM_H

#include "fft.h"

typedef struct {
    int nx, ny;
    double dt;
    double *re, *im;
    /* exp(-i V dt / 2) per point, and exp(-i k^2 dt / 2) with the
     * inverse transform's 1 / n */
    double *vr, *vi, *kr, *ki;
    sk_fft_plan px, py;
} sk_wave;

/* False when nx or ny is not a power of two. */
bool sk_wave_init(sk_wave *w, int nx, int ny, double dt);
void sk_wave_free(sk_wave *w);
/* The potential V, per point. */
void sk_wave_potential(sk_wave *w, const double *V);
/* Half a step of the potential, a step of the kinetic term in momentum
 * space, half a step of the potential: unitary. */
void sk_wave_step(sk_wave *w);
double sk_wave_norm(const sk_wave *w);
/* Multiplies each point by a real factor: an absorber. */
void sk_wave_damp(sk_wave *w, const double *m);
/* Adds a Gaussian packet of norm a at (x0, y0), widths sx and sy,
 * momentum (kx, ky). */
void sk_wave_packet(sk_wave *w, double x0, double y0, double sx, double sy, double kx, double ky, double a);
/* A point drawn from |psi|^2 for u uniform on [0, 1), or -1 for a wave
 * too faint to measure. */
int sk_wave_sample(const sk_wave *w, double u);

#endif
