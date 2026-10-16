/* The Gravity mode's physics (gravity.c), for its tests: gravity.js's
 * step and centre. */
#ifndef SK_VIZ_GRAVITY_H
#define SK_VIZ_GRAVITY_H

#include "vmath.h"

typedef struct {
    double x, y, vx, vy, m, r, ax, ay;
    double hue;
    bool star, comet;
    /* where it was drawn last frame, in points, once it has been */
    bool drawn;
    double sx, sy;
} sk_grav_body;

/* Where two bodies merged, and the mass of the one that merged into
 * the other. */
typedef struct {
    double x, y, m;
} sk_grav_merge;

typedef SK_VEC(sk_grav_body) sk_grav_bodies;
typedef SK_VEC(sk_grav_merge) sk_grav_merges;

/* A body's radius by its mass. */
double sk_grav_radius(double m, bool star);
/* One leapfrog step of h, G = 1, softened, then mergers of bodies that
 * touch, keeping mass and momentum; each merger appended to merged. */
void sk_grav_step(sk_grav_bodies *bodies, double h, sk_grav_merges *merged);
/* Total momentum to zero and the centre of mass to the origin. */
void sk_grav_centre(sk_grav_bodies *bodies);

#endif
