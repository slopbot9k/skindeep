/* The Fern mode's geometry (fern.c), for its tests: app/viz/fern.js's
 * SPECIES, UNIT, iterate and strand. */
#ifndef SK_VIZ_FERN_H
#define SK_VIZ_FERN_H

#include "vmath.h"

/* Each map {a, b, c, d, e, f, p}: x' = a x + b y + e, y' = c x + d y +
 * f, picked with probability p. Four maps each: the stem, the frond's
 * copy of itself, the left and the right leaflet. */
#define SK_FERN_SPECIES 4
extern const double sk_fern_species[SK_FERN_SPECIES][4][7];

/* A species in the frond's own frame: the copy map made a similarity,
 * then all four conjugated so the stem's foot is the origin and the
 * copy's fixed point, the tip, is (0, 1). */
void sk_fern_unit(const double maps[4][7], double out[4][7]);

/* The chaos game's orbit from (*x, *y), n points, each to out(ud, x,
 * y): a map set whose interpolation diverges restarts at the origin.
 * cum: the maps' cumulative probabilities. */
void sk_fern_iterate(const double maps[4][7], const double cum[4], int n, double *x, double *y, sk_rng *rand,
                     void (*out)(void *ud, double x, double y), void *ud);

/* A strand, a rachis or a pinna, from its foot (x, y) heading a
 * (radians from up, clockwise, y down), length len. Its part c from the
 * foot is open, bent by bend in all and by sway(s) per unit length; the
 * rest is a coil of T turns whose curvature grows as s^1.5 toward the
 * tip, as a fiddlehead's. Fills P with x, y, heading and s per node,
 * the coil given at least its share of the n segments; returns n + 1.
 * sway NULL is none. */
int sk_fern_strand(double x, double y, double a, double len, double c, double T, double bend,
                   double (*sway)(const void *ud, double s), const void *ud, double *P, int n);

#endif
