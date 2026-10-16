/* The Gravity 3D mode's physics (gravity3d.c), for its tests:
 * gravity3d.js's step, fall, centre, horizon, circular and rs. G = 1
 * and c = SK_G3_C. */
#ifndef SK_VIZ_GRAVITY3D_H
#define SK_VIZ_GRAVITY3D_H

#include "vmath.h"

#define SK_G3_C 11.5
#define SK_G3_CHI_MAX 0.95

typedef struct {
    double x, y, z, vx, vy, vz, m, r, ax, ay, az;
    double hue;
    int id; /* its own, while it lasts */
    bool star, hole, comet;
    /* a hole's spin, as a fraction of the extreme, about its axis j */
    double chi, jx, jy, jz;
    /* the places it passed, x y z each, at most 64, and when the last
     * was kept */
    double trail[3 * 64];
    int n_trail;
    double tt, flash;
    /* this frame's projection, its layer among the holes, its heat */
    double px, py, pd, pk, heat;
    int layer;
} sk_g3_body;

typedef struct {
    double x, y, z, vx, vy, vz, hue;
    bool disc, gone, inner;
    double px, py, pd, pk, mu, ix, iy, imu, heat, beam;
    int layer;
} sk_g3_grain;

typedef struct {
    double x, y, z, m;
    int hole; /* swallowings: the id of the hole that swallowed */
} sk_g3_event;

typedef SK_VEC(sk_g3_body) sk_g3_bodies;
typedef SK_VEC(sk_g3_grain) sk_g3_dust;
typedef SK_VEC(sk_g3_event) sk_g3_events;

/* The Schwarzschild radius of mass m. */
double sk_g3_rs(double m);
/* A hole's outer horizon, M / c^2 + sqrt((M / c^2)^2 - a^2). */
double sk_g3_horizon(const sk_g3_body *h);
/* The speed of a circular orbit at distance r about body s. */
double sk_g3_circular(const sk_g3_body *s, double r);
double sk_g3_radius(double m, bool star);
/* Steps the bodies by h in leapfrog substeps short enough near a
 * horizon; mergers and swallowings appended to merged and swallowed
 * (either may be NULL). */
void sk_g3_step(sk_g3_bodies *bodies, double h, sk_g3_events *merged, sk_g3_events *swallowed);
/* Steps the dust by h in the bodies' field; a grain that crosses a
 * horizon is gone, its mass the hole's. eaten[i], when not NULL, counts
 * what body i swallowed. */
void sk_g3_fall(sk_g3_dust *dust, sk_g3_bodies *bodies, double h, int *eaten);
/* Total momentum to zero and the centre of mass to the origin, the dust
 * and the trails moved with the bodies. */
void sk_g3_centre(sk_g3_bodies *bodies, sk_g3_dust *dust);

#endif
