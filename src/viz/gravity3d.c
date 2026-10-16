/* Gravity 3D mode (decision 0015), app/viz/gravity3d.js: the Gravity
 * mode's bodies in three dimensions, seen through a perspective camera
 * that turns about their centre of mass, with black holes. G = 1 and c =
 * C, so a hole of mass M has its Schwarzschild radius r_s = 2 M / C^2. A
 * hole pulls by the Paczynski-Wiita potential -M / (r - r_+), r_+ its
 * horizon, which puts the last stable circular orbit at 3 r_s, and a
 * spinning (Kerr) hole, of spin a = chi M / C^2, drags what moves near
 * it by the Lense-Thirring acceleration v x B, B = (2 M a / (C r^3)) (j
 * - 3 (j . n) n), j its axis and n the direction from it. What crosses
 * a horizon is swallowed, its mass and angular momentum the hole's.
 * Other pairs attract as in the Gravity mode, softened. Each hole is
 * drawn as its shadow, ringed by its photon ring at sqrt(27) / 2 r_s,
 * and bends the light of what lies behind it by the point-mass lens:
 * the distant stars, and the dust, its disc's far side among it. The
 * signal and the pointer: see react and pointer. */
#include "gravity3d.h"

#include "internal.h"

#define SOFT 9
#define H 0.25
#define MAX_BODIES 28
#define MAX_DUST 700
#define VIEW 240
#define C SK_G3_C
#define C2 (C * C)
#define CHI_MAX SK_G3_CHI_MAX
#define HOLE_MAX 1500
/* A dust grain's mass, and the disc's viscosity: between 3 and DISC r_s
 * of a hole a grain's velocity relaxes at rate VISC toward a circular
 * orbit's, a little slower, so the disc's grains settle on circles and
 * spiral in, and plunge from the last stable orbit. */
#define DUST_M 0.004
#define VISC 0.01
#define SLOW 0.995
#define DISC 10
#define RING 2.598076211353316 /* sqrt(27) / 2 */

static double len3(double x, double y, double z)
{
    return sqrt(x * x + y * y + z * z);
}

double sk_g3_rs(double m)
{
    return 2 * m / C2;
}

double sk_g3_horizon(const sk_g3_body *h)
{
    double g = h->m / C2, a = h->chi * g;
    return g + sqrt(g * g - a * a);
}

double sk_g3_circular(const sk_g3_body *s, double r)
{
    return s->hole ? sqrt(s->m * r) / (r - sk_g3_horizon(s)) : sqrt(s->m / r);
}

double sk_g3_radius(double m, bool star)
{
    return star ? 4 + 1.1 * sk_vm_cbrt(m) : 1.2 + 1.3 * sk_vm_cbrt(m);
}

/* The Lense-Thirring acceleration of what is at r from spinning hole h,
 * moving at v relative to it, into out. */
static void drag(const sk_g3_body *h, double rx, double ry, double rz, double vx, double vy, double vz, double out[3])
{
    double d2 = rx * rx + ry * ry + rz * rz, d = sqrt(d2);
    double k = 2 * h->m * (h->chi * h->m / C2) / (C * d2 * d);
    double jn = (h->jx * rx + h->jy * ry + h->jz * rz) / d2;
    double bx = k * (h->jx - 3 * jn * rx), by = k * (h->jy - 3 * jn * ry), bz = k * (h->jz - 3 * jn * rz);
    out[0] = vy * bz - vz * by;
    out[1] = vz * bx - vx * bz;
    out[2] = vx * by - vy * bx;
}

static void accel(sk_g3_bodies *bodies)
{
    sk_g3_body *b = bodies->ptr;
    size_t n = bodies->len;
    for (size_t i = 0; i < n; i++)
        b[i].ax = b[i].ay = b[i].az = 0;
    for (size_t i = 0; i < n; i++)
        for (size_t j = i + 1; j < n; j++) {
            sk_g3_body *a = &b[i], *o = &b[j];
            double dx = o->x - a->x, dy = o->y - a->y, dz = o->z - a->z, f;
            if (a->hole || o->hole) {
                /* Paczynski-Wiita, about the larger horizon of the pair. */
                double s = SK_MAX(a->hole ? sk_g3_horizon(a) : 0, o->hole ? sk_g3_horizon(o) : 0);
                double d = sqrt(dx * dx + dy * dy + dz * dz), gap = SK_MAX(d - s, 0.02 * s);
                f = 1 / (gap * gap * d);
            } else {
                double d2 = dx * dx + dy * dy + dz * dz + SOFT;
                f = 1 / (d2 * sqrt(d2));
            }
            a->ax += dx * f * o->m;
            a->ay += dy * f * o->m;
            a->az += dz * f * o->m;
            o->ax -= dx * f * a->m;
            o->ay -= dy * f * a->m;
            o->az -= dz * f * a->m;
        }
    double lt[3];
    for (size_t hi = 0; hi < n; hi++) {
        sk_g3_body *h = &b[hi];
        if (!h->hole || !h->chi)
            continue;
        for (size_t i = 0; i < n; i++) {
            if (i == hi)
                continue;
            sk_g3_body *o = &b[i];
            drag(h, o->x - h->x, o->y - h->y, o->z - h->z, o->vx - h->vx, o->vy - h->vy, o->vz - h->vz, lt);
            o->ax += lt[0];
            o->ay += lt[1];
            o->az += lt[2];
            h->ax -= lt[0] * o->m / h->m;
            h->ay -= lt[1] * o->m / h->m;
            h->az -= lt[2] * o->m / h->m;
        }
    }
}

static void leap(sk_g3_bodies *bodies, double h)
{
    accel(bodies);
    for (size_t i = 0; i < bodies->len; i++) {
        sk_g3_body *b = &bodies->ptr[i];
        b->vx += b->ax * h / 2;
        b->vy += b->ay * h / 2;
        b->vz += b->az * h / 2;
        b->x += b->vx * h;
        b->y += b->vy * h;
        b->z += b->vz * h;
    }
    accel(bodies);
    for (size_t i = 0; i < bodies->len; i++) {
        sk_g3_body *b = &bodies->ptr[i];
        b->vx += b->ax * h / 2;
        b->vy += b->ay * h / 2;
        b->vz += b->az * h / 2;
    }
}

/* The longest safe substep: near a hole, a small part of the gap to its
 * horizon over the speed across it. */
static double limit(const sk_g3_bodies *bodies)
{
    double hm = INFINITY;
    for (size_t hi = 0; hi < bodies->len; hi++) {
        const sk_g3_body *h = &bodies->ptr[hi];
        if (!h->hole)
            continue;
        double s = sk_g3_horizon(h);
        for (size_t i = 0; i < bodies->len; i++) {
            if (i == hi)
                continue;
            const sk_g3_body *b = &bodies->ptr[i];
            double gap = SK_MAX(1e-6, len3(b->x - h->x, b->y - h->y, b->z - h->z) - s - (b->hole ? sk_g3_horizon(b) : 0));
            double v = len3(b->vx - h->vx, b->vy - h->vy, b->vz - h->vz);
            hm = SK_MIN(hm, SK_MIN(0.03 * gap / (v + 1e-9), 0.05 * gap * sqrt(gap / h->m)));
        }
    }
    return hm;
}

/* Hole h swallows b: mass, momentum and angular momentum kept, the spin
 * capped below the extreme. */
static void absorb(sk_g3_body *h, const sk_g3_body *b)
{
    double m = h->m + b->m, mu = h->m * b->m / m;
    double rx = b->x - h->x, ry = b->y - h->y, rz = b->z - h->z;
    double vx = b->vx - h->vx, vy = b->vy - h->vy, vz = b->vz - h->vz;
    double sh = h->chi * h->m * h->m / C, sb = b->hole ? b->chi * b->m * b->m / C : 0;
    double jx = sh * h->jx + sb * (b->hole ? b->jx : 0) + mu * (ry * vz - rz * vy);
    double jy = sh * h->jy + sb * (b->hole ? b->jy : 0) + mu * (rz * vx - rx * vz);
    double jz = sh * h->jz + sb * (b->hole ? b->jz : 0) + mu * (rx * vy - ry * vx);
    double J = len3(jx, jy, jz);
    h->x = (h->x * h->m + b->x * b->m) / m;
    h->y = (h->y * h->m + b->y * b->m) / m;
    h->z = (h->z * h->m + b->z * b->m) / m;
    h->vx = (h->vx * h->m + b->vx * b->m) / m;
    h->vy = (h->vy * h->m + b->vy * b->m) / m;
    h->vz = (h->vz * h->m + b->vz * b->m) / m;
    h->m = m;
    if (J > 0) {
        h->chi = SK_MIN(CHI_MAX, J * C / (m * m));
        h->jx = jx / J;
        h->jy = jy / J;
        h->jz = jz / J;
    }
    h->r = sk_g3_horizon(h);
}

static void remove_at(sk_g3_bodies *bodies, size_t j)
{
    memmove(&bodies->ptr[j], &bodies->ptr[j + 1], (bodies->len - j - 1) * sizeof *bodies->ptr);
    bodies->len--;
}

/* Swallowings, then mergers of bodies that touch, as in the Gravity
 * mode. Each is appended to its list. */
static void collide(sk_g3_bodies *bodies, sk_g3_events *merged, sk_g3_events *swallowed)
{
    for (size_t i = 0; i < bodies->len; i++)
        for (size_t j = i + 1; j < bodies->len; j++) {
            sk_g3_body a = bodies->ptr[i], b = bodies->ptr[j];
            double d = len3(a.x - b.x, a.y - b.y, a.z - b.z);
            if (a.hole || b.hole) {
                if (b.hole && (!a.hole || b.m > a.m)) {
                    sk_g3_body t = a;
                    a = b;
                    b = t;
                }
                if (d > sk_g3_horizon(&a) + (b.hole ? sk_g3_horizon(&b) : 0))
                    continue;
                absorb(&a, &b);
                bodies->ptr[i] = a;
                sk_g3_event e = {a.x, a.y, a.z, b.m, a.id};
                if (swallowed)
                    sk_vec_push(swallowed, e);
            } else {
                if (d > a.r + b.r)
                    continue;
                double m = a.m + b.m;
                bool big = a.m >= b.m;
                sk_g3_body *s = &bodies->ptr[i];
                s->x = (a.x * a.m + b.x * b.m) / m;
                s->y = (a.y * a.m + b.y * b.m) / m;
                s->z = (a.z * a.m + b.z * b.m) / m;
                s->vx = (a.vx * a.m + b.vx * b.m) / m;
                s->vy = (a.vy * a.m + b.vy * b.m) / m;
                s->vz = (a.vz * a.m + b.vz * b.m) / m;
                s->hue = big ? a.hue : b.hue;
                s->star = a.star || b.star;
                s->comet = big ? a.comet : b.comet;
                sk_g3_event e = {s->x, s->y, s->z, SK_MIN(a.m, b.m), -1};
                if (merged)
                    sk_vec_push(merged, e);
                s->m = m;
                s->r = sk_g3_radius(m, s->star);
            }
            remove_at(bodies, j);
            /* gravity3d.js sets j to i and its loop then goes on from
             * i + 1. */
            j = i;
        }
}

void sk_g3_step(sk_g3_bodies *bodies, double h, sk_g3_events *merged, sk_g3_events *swallowed)
{
    double t = 0;
    while (t < h) {
        double hs = SK_MIN(h - t, SK_MAX(h / 256, limit(bodies)));
        leap(bodies, hs);
        collide(bodies, merged, swallowed);
        t += hs;
    }
}

#define MAX_HOLES 64

void sk_g3_fall(sk_g3_dust *dust, sk_g3_bodies *bodies, double h, int *eaten)
{
    size_t holes[MAX_HOLES], nh = 0;
    double r2[MAX_HOLES], disc[MAX_HOLES], isco[MAX_HOLES];
    for (size_t i = 0; i < bodies->len && nh < MAX_HOLES; i++)
        if (bodies->ptr[i].hole) {
            holes[nh] = i;
            r2[nh] = sk_g3_horizon(&bodies->ptr[i]);
            disc[nh] = DISC * sk_g3_rs(bodies->ptr[i].m);
            isco[nh] = 3 * sk_g3_rs(bodies->ptr[i].m);
            nh++;
        }
    double *hz = sk_xmalloc((bodies->len ? bodies->len : 1) * sizeof *hz);
    for (size_t i = 0; i < bodies->len; i++)
        hz[i] = bodies->ptr[i].hole ? sk_g3_horizon(&bodies->ptr[i]) : 0;
    double lt[3];
    for (size_t di = 0; di < dust->len; di++) {
        sk_g3_grain *d = &dust->ptr[di];
        double n = 1;
        for (size_t i = 0; i < nh; i++) {
            const sk_g3_body *o = &bodies->ptr[holes[i]];
            double gap = SK_MAX(1e-3, len3(d->x - o->x, d->y - o->y, d->z - o->z) - r2[i]);
            double v = len3(d->vx - o->vx, d->vy - o->vy, d->vz - o->vz);
            n = SK_MAX(n, ceil(h * v / (0.08 * gap)));
        }
        n = SK_MIN(n, 12);
        double hs = h / n;
        for (int k = 0; k < (int)n && !d->gone; k++) {
            double ax = 0, ay = 0, az = 0;
            for (size_t bi = 0; bi < bodies->len; bi++) {
                const sk_g3_body *b = &bodies->ptr[bi];
                double dx = b->x - d->x, dy = b->y - d->y, dz = b->z - d->z, f;
                if (b->hole) {
                    double r = sqrt(dx * dx + dy * dy + dz * dz), gap = SK_MAX(r - hz[bi], 0.02 * hz[bi]);
                    f = b->m / (gap * gap * r);
                } else {
                    double d2 = dx * dx + dy * dy + dz * dz + SOFT;
                    f = b->m / (d2 * sqrt(d2));
                }
                ax += dx * f;
                ay += dy * f;
                az += dz * f;
            }
            d->vx += ax * hs;
            d->vy += ay * hs;
            d->vz += az * hs;
            for (size_t i = 0; i < nh; i++) {
                const sk_g3_body *o = &bodies->ptr[holes[i]];
                double rx = d->x - o->x, ry = d->y - o->y, rz = d->z - o->z;
                if (o->chi) {
                    drag(o, rx, ry, rz, d->vx - o->vx, d->vy - o->vy, d->vz - o->vz, lt);
                    d->vx += lt[0] * hs;
                    d->vy += lt[1] * hs;
                    d->vz += lt[2] * hs;
                }
                double r = sqrt(rx * rx + ry * ry + rz * rz);
                if (r < disc[i] && r > isco[i]) {
                    /* The circular velocity in the plane of the grain's
                     * orbit. */
                    double vx = d->vx - o->vx, vy = d->vy - o->vy, vz = d->vz - o->vz;
                    double lx = ry * vz - rz * vy, ly = rz * vx - rx * vz, lz = rx * vy - ry * vx;
                    double tx = ly * rz - lz * ry, ty = lz * rx - lx * rz, tz = lx * ry - ly * rx;
                    double tl = len3(tx, ty, tz);
                    if (tl > 0) {
                        double c = sk_g3_circular(o, r) * SLOW / tl, kk = SK_MIN(1, VISC * hs);
                        d->vx += (tx * c - vx) * kk;
                        d->vy += (ty * c - vy) * kk;
                        d->vz += (tz * c - vz) * kk;
                    }
                }
            }
            d->x += d->vx * hs;
            d->y += d->vy * hs;
            d->z += d->vz * hs;
            for (size_t i = 0; i < nh; i++) {
                sk_g3_body *o = &bodies->ptr[holes[i]];
                double ex = d->x - o->x, ey = d->y - o->y, ez = d->z - o->z;
                if (ex * ex + ey * ey + ez * ez > r2[i] * r2[i])
                    continue;
                o->vx = (o->vx * o->m + d->vx * DUST_M) / (o->m + DUST_M);
                o->vy = (o->vy * o->m + d->vy * DUST_M) / (o->m + DUST_M);
                o->vz = (o->vz * o->m + d->vz * DUST_M) / (o->m + DUST_M);
                o->m += DUST_M;
                d->gone = true;
                if (eaten)
                    eaten[holes[i]]++;
                break;
            }
        }
    }
    free(hz);
}

void sk_g3_centre(sk_g3_bodies *bodies, sk_g3_dust *dust)
{
    double m = 0, p[3] = {0, 0, 0}, q[3] = {0, 0, 0};
    for (size_t i = 0; i < bodies->len; i++) {
        const sk_g3_body *b = &bodies->ptr[i];
        m += b->m;
        p[0] += b->m * b->vx;
        p[1] += b->m * b->vy;
        p[2] += b->m * b->vz;
        q[0] += b->m * b->x;
        q[1] += b->m * b->y;
        q[2] += b->m * b->z;
    }
    if (!m)
        return;
    for (size_t i = 0; i < bodies->len; i++) {
        sk_g3_body *o = &bodies->ptr[i];
        o->vx -= p[0] / m;
        o->vy -= p[1] / m;
        o->vz -= p[2] / m;
        o->x -= q[0] / m;
        o->y -= q[1] / m;
        o->z -= q[2] / m;
        for (int k = 0; k < o->n_trail; k++) {
            o->trail[3 * k] -= q[0] / m;
            o->trail[3 * k + 1] -= q[1] / m;
            o->trail[3 * k + 2] -= q[2] / m;
        }
    }
    for (size_t i = 0; dust && i < dust->len; i++) {
        sk_g3_grain *o = &dust->ptr[i];
        o->vx -= p[0] / m;
        o->vy -= p[1] / m;
        o->vz -= p[2] / m;
        o->x -= q[0] / m;
        o->y -= q[1] / m;
        o->z -= q[2] / m;
    }
}

/* ---- The sky ---- */

/* A seeded generator, so every pane shows the same sky: gravity3d.js's
 * rng, in 32-bit integers as Math.imul works. */
static double sky_rand(uint32_t *s)
{
    *s += 0x6d2b79f5u;
    uint32_t t = (*s ^ (*s >> 15)) * (1 | *s);
    t = (t + ((t ^ (t >> 7)) * (61 | t))) ^ t;
    return (double)(t ^ (t >> 14)) / 4294967296.0;
}

static double gauss(uint32_t *s)
{
    double a = sk_vm_log(sky_rand(s) + 1e-9);
    return sqrt(-2 * a) * sk_vm_cos(2 * SK_PI * sky_rand(s));
}

/* Two unit vectors normal to unit n and to each other. */
static void basis(const double n[3], double u[3], double v[3])
{
    double a[3] = {0, 0, 1};
    if (!(fabs(n[2]) < 0.9)) {
        a[0] = 1;
        a[2] = 0;
    }
    u[0] = n[1] * a[2] - n[2] * a[1];
    u[1] = n[2] * a[0] - n[0] * a[2];
    u[2] = n[0] * a[1] - n[1] * a[0];
    double l = len3(u[0], u[1], u[2]);
    u[0] /= l;
    u[1] /= l;
    u[2] /= l;
    v[0] = n[1] * u[2] - n[2] * u[1];
    v[1] = n[2] * u[0] - n[0] * u[2];
    v[2] = n[0] * u[1] - n[1] * u[0];
}

typedef struct {
    double n[3], b, size;
    sk_color fill, arc;
} sky_star;

typedef struct {
    double n[3], a, size;
    bool warm;
} sky_haze;

#define STARS 3006
#define HAZE 120

typedef struct {
    double band[3], e1[3], e2[3];
} band;

static void on_band(const band *bd, double t, double lat, double out[3])
{
    double cl = sk_vm_cos(lat), sl = sk_vm_sin(lat), ct = sk_vm_cos(t), st = sk_vm_sin(t);
    for (int i = 0; i < 3; i++)
        out[i] = cl * (ct * bd->e1[i] + st * bd->e2[i]) + sl * bd->band[i];
}

/* The distant stars: directions only, so they turn with the camera and
 * show no parallax. Most lie anywhere, some in a band about a great
 * circle, a galaxy's disc seen from inside, its glow a haze of soft
 * dots brightest toward one point, and a few bright ones on the circle
 * the camera's line of sight through the centre sweeps at its first
 * pitch, so a hole there lines up with one now and then. */
static void make_sky(double pitch, sky_star *stars, sky_haze *haze)
{
    uint32_t s = 83;
    band bd;
    double l = len3(0.35, -0.42, 0.84);
    bd.band[0] = 0.35 / l;
    bd.band[1] = -0.42 / l;
    bd.band[2] = 0.84 / l;
    basis(bd.band, bd.e1, bd.e2);
    static const double hues[10] = {210, 220, 230, 200, 40, 48, 15, 120, 280, 320};
    for (int k = 0; k < 3000; k++) {
        sky_star *o = &stars[k];
        if (k < 2000) {
            double z = 2 * sky_rand(&s) - 1, a = 2 * SK_PI * sky_rand(&s), c = sqrt(1 - z * z);
            o->n[0] = c * sk_vm_cos(a);
            o->n[1] = c * sk_vm_sin(a);
            o->n[2] = z;
        } else {
            double t = 2 * SK_PI * sky_rand(&s);
            on_band(&bd, t, 0.06 * gauss(&s), o->n);
        }
        double b = sk_vm_pow(sky_rand(&s), 3);
        o->b = 0.2 + 0.8 * b;
        o->size = b > 0.6 ? 2 : b > 0.2 ? 1.5 : 1;
        double hue = hues[(int)floor(sky_rand(&s) * 10)], sat = 30 + 40 * sky_rand(&s);
        o->fill = sk_vc_hsl(hue, (int)sat, 85);
        o->arc = sk_vc_hsl(hue, (int)(sat + 20), 85);
    }
    for (int k = 0; k < 6; k++) {
        sky_star *o = &stars[3000 + k];
        double a = 2 * SK_PI * (k + sky_rand(&s) * 0.5) / 6;
        double z = -sk_vm_sin(pitch) + 0.01 * gauss(&s), c = sqrt(1 - z * z);
        o->n[0] = -c * sk_vm_cos(a);
        o->n[1] = -c * sk_vm_sin(a);
        o->n[2] = z;
        o->b = 1;
        o->size = 2;
        o->fill = sk_vc_hsl(hues[k], 60, 85);
        o->arc = sk_vc_hsl(hues[k], 80, 85);
    }
    for (int k = 0; k < HAZE; k++) {
        sky_haze *o = &haze[k];
        double t = 2 * SK_PI * sky_rand(&s);
        double w = sk_vm_atan2(sk_vm_sin(t - 1), sk_vm_cos(t - 1));
        double core = sk_vm_exp(-(w * w) / 0.5);
        on_band(&bd, t, 0.035 * gauss(&s), o->n);
        o->a = 0.035 + 0.07 * core * sky_rand(&s);
        o->size = 0.05 + 0.05 * sky_rand(&s);
        o->warm = sky_rand(&s) < 0.2 + 0.6 * core;
    }
}

/* Heat from a fall: 0 far out, 1 at the last stable orbit and within.
 * The colour of a grain or a body as it heats: toward orange that
 * whitens, then a blue white. */
static void hot(double hue, double sat, double light, double heat, double out[3])
{
    if (heat <= 0.02) {
        out[0] = hue;
        out[1] = sat;
        out[2] = light;
        return;
    }
    if (heat < 0.7) {
        double t = heat / 0.7, dh = fmod(30 - hue + 540, 360) - 180;
        out[0] = fmod(hue + dh * SK_MIN(1, 2 * t) + 360, 360);
        out[1] = sat + (95 - sat) * t * (1 - t) * 2;
        out[2] = light + (88 - light) * t;
        return;
    }
    double t = (heat - 0.7) / 0.3;
    out[0] = 215;
    out[1] = 70 * t;
    out[2] = 88 + 8 * t;
}

/* ---- The mode ---- */

typedef struct {
    double x, y, z, t, r;
} flare;

/* A hole on screen this frame. */
typedef struct {
    size_t b;
    double x, y, d, k, s;
} screen_hole;

typedef struct {
    sk_vmode base;
    sky_star stars[STARS];
    sky_haze haze[HAZE];
    /* A soft dot, white at its centre, in each hue: the gradient of
     * gravity3d.js's sprites. */
    sk_vc_paint blue_dot, warm_dot;
    sk_g3_bodies bodies;
    sk_g3_dust dust;
    SK_VEC(flare) flares;
    sk_g3_events merged, swallowed;
    int *eaten;
    double clock, shed;
    int subs, ids;
    /* The camera: its turn about the vertical and its height, in
     * radians, and the time the pointer last turned it; the press while
     * the pointer is down. */
    double yaw, pitch, held;
    bool pressed, moved;
    double px0, py0, pyaw, ppitch;
    int thrown, turns, n_swallowed, arcs;
    double tick;
    /* The frame's projection: centre, focal length, camera distance and
     * basis (right, up, toward the camera). */
    double cx, cy, f, D, r[3], u[3], e[3];
    /* The last point projected or lensed: place, depth, scale,
     * magnification. */
    double Px, Py, Pd, Pk, Pmu;
    SK_VEC(screen_hole) holes;
    size_t hot_hole;
} g3;

#define PITCH 0.36

static double rnd(g3 *g)
{
    return sk_rng_next(&g->base.rng);
}

enum { KIND_PLANET, KIND_STAR, KIND_HOLE, KIND_COMET };

static size_t body(g3 *g, double x, double y, double z, double vx, double vy, double vz, double m, double hue, int kind)
{
    sk_g3_body b;
    memset(&b, 0, sizeof b);
    b.x = x;
    b.y = y;
    b.z = z;
    b.vx = vx;
    b.vy = vy;
    b.vz = vz;
    b.m = m;
    b.hue = hue;
    b.star = kind == KIND_STAR;
    b.hole = kind == KIND_HOLE;
    b.comet = kind == KIND_COMET;
    b.jz = 1;
    b.id = ++g->ids;
    b.r = b.hole ? sk_g3_horizon(&b) : sk_g3_radius(m, b.star);
    b.tt = g->clock;
    b.pd = 1;
    sk_vec_push(&g->bodies, b);
    return g->bodies.len - 1;
}

/* Body m on a circular orbit about s at distance r, in the plane of
 * unit normal n, at angle a, retrograde if back. */
static size_t orbit(g3 *g, sk_g3_body s, double r, const double n[3], double a, double m, double hue, int kind,
                    bool back)
{
    double p[3], q[3];
    basis(n, p, q);
    double c = sk_vm_cos(a), si = sk_vm_sin(a), v = sk_g3_circular(&s, r) * (back ? -1 : 1);
    return body(g, s.x + r * (c * p[0] + si * q[0]), s.y + r * (c * p[1] + si * q[1]), s.z + r * (c * p[2] + si * q[2]),
                s.vx + v * (-si * p[0] + c * q[0]), s.vy + v * (-si * p[1] + c * q[1]),
                s.vz + v * (-si * p[2] + c * q[2]), m, hue, kind);
}

/* A unit normal tilted from the vertical by up to t. */
static void tilt(g3 *g, double t, double out[3])
{
    double a = rnd(g) * SK_TAU, i = (rnd(g) * 2 - 1) * t;
    out[0] = sk_vm_sin(i) * sk_vm_cos(a);
    out[1] = sk_vm_sin(i) * sk_vm_sin(a);
    out[2] = sk_vm_cos(i);
}

/* The heaviest body, the first of the heaviest. */
static size_t sun(const g3 *g)
{
    size_t s = SIZE_MAX;
    for (size_t i = 0; i < g->bodies.len; i++)
        if (s == SIZE_MAX || g->bodies.ptr[i].m > g->bodies.ptr[s].m)
            s = i;
    return s;
}

static void push_grain(g3 *g, double x, double y, double z, double vx, double vy, double vz, double hue, bool disc)
{
    sk_g3_grain d;
    memset(&d, 0, sizeof d);
    d.x = x;
    d.y = y;
    d.z = z;
    d.vx = vx;
    d.vy = vy;
    d.vz = vz;
    d.hue = hue;
    d.disc = disc;
    d.pd = 1;
    d.mu = 1;
    d.beam = 1;
    sk_vec_push(&g->dust, d);
}

/* Grains on circular orbits about hole h, in its equator. */
static void disc(g3 *g, sk_g3_body h, int n, double r0, double r1, double hue)
{
    double j[3] = {h.jx, h.jy, h.jz}, p[3], q[3];
    basis(j, p, q);
    for (int k = 0; k < n; k++) {
        double r = r0 + (r1 - r0) * rnd(g), a = rnd(g) * SK_TAU, v = sk_g3_circular(&h, r);
        double c = sk_vm_cos(a), si = sk_vm_sin(a), w = (rnd(g) - 0.5) * 0.02 * r, at[3], vel[3];
        for (int i = 0; i < 3; i++) {
            at[i] = r * (c * p[i] + si * q[i]) + w * j[i];
            vel[i] = v * (-si * p[i] + c * q[i]);
        }
        push_grain(g, h.x + at[0], h.y + at[1], h.z + at[2], h.vx + vel[0], h.vy + vel[1], h.vz + vel[2], hue, true);
    }
}

/* A spinning hole at the centre with its disc, a companion star that
 * feeds it, and three planets. */
static void seed(g3 *g)
{
    g->bodies.len = 0;
    g->dust.len = 0;
    size_t hi = body(g, 0, 0, 0, 0, 0, 0, 900, 0, KIND_HOLE);
    double n[3];
    tilt(g, 0.12, n);
    sk_g3_body *h = &g->bodies.ptr[hi];
    h->chi = 0.7;
    h->jx = n[0];
    h->jy = n[1];
    h->jz = n[2];
    h->r = sk_g3_horizon(h);
    double s = sk_g3_rs(h->m);
    disc(g, *h, 420, 3.3 * s, 8 * s, 30);
    tilt(g, 0.35, n);
    orbit(g, g->bodies.ptr[hi], 175, n, rnd(g) * 6.3, 80, 48, KIND_STAR, false);
    static const double planets[3][2] = {{128, 200}, {215, 120}, {245, 30}};
    for (int i = 0; i < 3; i++) {
        tilt(g, 0.3, n);
        double a = rnd(g) * 6.3, m = 2 + rnd(g) * 3;
        orbit(g, g->bodies.ptr[hi], planets[i][0], n, a, m, planets[i][1], KIND_PLANET, false);
    }
    sk_g3_centre(&g->bodies, &g->dust);
}

static void planet(g3 *g, double hue, double m)
{
    sk_g3_body s = g->bodies.ptr[sun(g)];
    /* Clear of a hole's disc. */
    double r = s.hole ? SK_MIN(VIEW, (DISC + 1) * sk_g3_rs(s.m) + rnd(g) * 120) : SK_MAX(s.r + 20, 60 + rnd(g) * 160);
    double n[3];
    tilt(g, 0.5, n);
    double a = rnd(g) * 6.3;
    bool back = rnd(g) < 0.15;
    orbit(g, s, r, n, a, m, hue, KIND_PLANET, back);
}

static void cap(g3 *g)
{
    long over = (long)g->dust.len - MAX_DUST;
    if (over <= 0)
        return;
    size_t k = 0;
    for (size_t i = 0; i < g->dust.len; i++)
        if (g->dust.ptr[i].disc || over-- <= 0)
            g->dust.ptr[k++] = g->dust.ptr[i];
    g->dust.len = k;
    if (g->dust.len > MAX_DUST)
        SK_VIZ_SHIFT(&g->dust, g->dust.len - MAX_DUST);
}

/* The lightest planets past the cap gone, the oldest dust past its cap,
 * a disc's last, the frame recentred. */
static void trim(g3 *g)
{
    while (g->bodies.len > MAX_BODIES) {
        size_t best = SIZE_MAX;
        for (size_t i = 0; i < g->bodies.len; i++) {
            const sk_g3_body *b = &g->bodies.ptr[i];
            if (!b->star && !b->hole && (best == SIZE_MAX || !(g->bodies.ptr[best].m < b->m)))
                best = i;
        }
        if (best == SIZE_MAX)
            break;
        remove_at(&g->bodies, best);
    }
    cap(g);
    sk_g3_centre(&g->bodies, &g->dust);
}

/* The star nearest each hole sheds grains from the point between them
 * where their pulls balance, L1, at about the star's angular speed:
 * slower than a circular orbit there, so they fall in a stream toward
 * the hole and feed its disc. */
static void feed(g3 *g, double hh)
{
    g->shed += hh * 1.5;
    while (g->shed >= 1) {
        g->shed--;
        for (size_t hi = 0; hi < g->bodies.len; hi++) {
            if (!g->bodies.ptr[hi].hole)
                continue;
            sk_g3_body h = g->bodies.ptr[hi];
            size_t si = SIZE_MAX;
            for (size_t i = 0; i < g->bodies.len; i++) {
                const sk_g3_body *b = &g->bodies.ptr[i];
                if (!b->star)
                    continue;
                if (si == SIZE_MAX)
                    si = i;
                else {
                    const sk_g3_body *a = &g->bodies.ptr[si];
                    if (len3(b->x - h.x, b->y - h.y, b->z - h.z) < len3(a->x - h.x, a->y - h.y, a->z - h.z))
                        si = i;
                }
            }
            if (si == SIZE_MAX)
                continue;
            sk_g3_body s = g->bodies.ptr[si];
            double dx = h.x - s.x, dy = h.y - s.y, dz = h.z - s.z, d = len3(dx, dy, dz);
            if (d > VIEW * 1.5)
                continue;
            double l1 = d * sk_vm_cbrt(s.m / (3 * h.m));
            double k = (1 - l1 / d) * (1.1 + 0.35 * rnd(g));
            double jx = (rnd(g) - 0.5) * 0.08, jy = (rnd(g) - 0.5) * 0.08, jz = (rnd(g) - 0.5) * 0.08;
            push_grain(g, s.x + dx / d * l1, s.y + dy / d * l1, s.z + dz / d * l1, h.vx + (s.vx - h.vx) * k + jx,
                       h.vy + (s.vy - h.vy) * k + jy, h.vz + (s.vz - h.vz) * k + jz, s.hue, false);
        }
    }
    cap(g);
}

static void add_flare(g3 *g, double x, double y, double z, double r)
{
    flare f = {x, y, z, g->clock, r};
    sk_vec_push(&g->flares, f);
}

/* The events, as decision 0015 lists them. */
static void react(g3 *g, const sk_viz_event *e)
{
    size_t si = sun(g);
    sk_g3_body s = g->bodies.ptr[si];
    if (e->kind == SK_EV_TOOL) {
        planet(g, sk_viz_class_hue(e->cls), 1 + rnd(g) * 6);
    } else if (e->kind == SK_EV_PROMPT) {
        /* From the edge, aimed to pass close by the heaviest body: for
         * a hole, within a few r_s, so some are swallowed. */
        double n[3], p[3], q[3];
        tilt(g, 1.2, n);
        basis(n, p, q);
        double a = rnd(g) * SK_TAU, r = VIEW * 1.3, ca = sk_vm_cos(a), sa = sk_vm_sin(a), d[3];
        for (int i = 0; i < 3; i++)
            d[i] = ca * p[i] + sa * q[i];
        double miss = s.hole ? sk_g3_rs(s.m) * (1.5 + 3 * rnd(g)) : 20 + rnd(g) * 40;
        miss *= rnd(g) < 0.5 ? -1 : 1;
        double v = sqrt(2 * s.m / r) * 1.05;
        double side[3] = {n[1] * d[2] - n[2] * d[1], n[2] * d[0] - n[0] * d[2], n[0] * d[1] - n[1] * d[0]};
        size_t bi = body(g, s.x + r * d[0], s.y + r * d[1], s.z + r * d[2], 0, 0, 0, 0.5, 190, KIND_COMET);
        sk_g3_body *b = &g->bodies.ptr[bi];
        b->vx = s.vx + v * (-d[0] + miss / r * side[0]);
        b->vy = s.vy + v * (-d[1] + miss / r * side[1]);
        b->vz = s.vz + v * (-d[2] + miss / r * side[2]);
        add_flare(g, s.x, s.y, s.z, s.hole ? RING * sk_g3_rs(s.m) : s.r);
    } else if (e->kind == SK_EV_STOP) {
        sk_g3_body *sp = &g->bodies.ptr[si];
        sp->m = SK_MIN(SK_MAX(sp->m, HOLE_MAX), sp->m * 1.12);
        sp->r = sp->hole ? sk_g3_horizon(sp) : sk_g3_radius(sp->m, sp->star);
        sp->flash = 1;
        add_flare(g, sp->x, sp->y, sp->z, sp->hole ? RING * sk_g3_rs(sp->m) : sp->r);
    } else if (e->kind == SK_EV_AGENT) {
        /* A subagent: a black hole, then a star, in turn, three of each
         * at most, a star first when none is left. */
        g->subs++;
        int holes = 0, suns = 0;
        for (size_t i = 0; i < g->bodies.len; i++) {
            holes += g->bodies.ptr[i].hole;
            suns += g->bodies.ptr[i].star;
        }
        double r = 150 + rnd(g) * 60, n[3];
        if (g->subs % 2 && holes < 3 && suns > 0) {
            tilt(g, 0.4, n);
            double a = rnd(g) * 6.3, m = 150 + rnd(g) * 150;
            size_t hi = orbit(g, s, r, n, a, m, 0, KIND_HOLE, false);
            tilt(g, 1, n);
            sk_g3_body *h = &g->bodies.ptr[hi];
            h->chi = 0.3 + 0.6 * rnd(g);
            h->jx = n[0];
            h->jy = n[1];
            h->jz = n[2];
            h->r = sk_g3_horizon(h);
            sk_g3_body hc = *h;
            disc(g, hc, 50, 3.3 * sk_g3_rs(hc.m), 7 * sk_g3_rs(hc.m), 280);
            add_flare(g, hc.x, hc.y, hc.z, RING * sk_g3_rs(hc.m));
        } else if (suns < 3) {
            tilt(g, 0.4, n);
            double a = rnd(g) * 6.3, m = 100 + rnd(g) * 150;
            orbit(g, s, r, n, a, m, 15, KIND_STAR, false);
        } else {
            planet(g, 300, 8);
        }
    } else if (e->kind == SK_EV_COMPACT) {
        for (size_t i = 0; i < g->bodies.len; i++) {
            g->bodies.ptr[i].vx *= 0.6;
            g->bodies.ptr[i].vy *= 0.6;
            g->bodies.ptr[i].vz *= 0.6;
        }
        for (size_t i = 0; i < g->dust.len; i++) {
            g->dust.ptr[i].vx *= 0.6;
            g->dust.ptr[i].vy *= 0.6;
            g->dust.ptr[i].vz *= 0.6;
        }
    } else {
        /* A puff of dust from a body that is not a hole. */
        size_t from = 0;
        for (size_t i = 0; i < g->bodies.len; i++)
            from += !g->bodies.ptr[i].hole;
        size_t pick = (size_t)floor(rnd(g) * (double)from), seen = 0;
        for (size_t i = 0; i < g->bodies.len && from; i++) {
            if (g->bodies.ptr[i].hole)
                continue;
            if (seen++ != pick)
                continue;
            sk_g3_body p = g->bodies.ptr[i];
            for (int k = 0; k < 16; k++) {
                double z = 1 - 2 * (k + 0.5) / 16, a = k * 2.39996, c = sqrt(1 - z * z);
                double d[3] = {c * sk_vm_cos(a), c * sk_vm_sin(a), z}, v = 0.3 + rnd(g) * 0.5;
                push_grain(g, p.x + (p.r + 3) * d[0], p.y + (p.r + 3) * d[1], p.z + (p.r + 3) * d[2], p.vx + v * d[0],
                           p.vy + v * d[1], p.vz + v * d[2], p.hue, false);
            }
            break;
        }
    }
    trim(g);
}

/* The camera's basis from its turn and height. */
static void aim(g3 *g, double w, double h)
{
    double cy = sk_vm_cos(g->yaw), sy = sk_vm_sin(g->yaw), cp = sk_vm_cos(g->pitch), sp = sk_vm_sin(g->pitch);
    g->e[0] = cp * cy;
    g->e[1] = cp * sy;
    g->e[2] = sp;
    g->r[0] = -sy;
    g->r[1] = cy;
    g->r[2] = 0;
    g->u[0] = -sp * cy;
    g->u[1] = -sp * sy;
    g->u[2] = cp;
    g->cx = w / 2;
    g->cy = h / 2;
    g->f = SK_MIN(w, h) / 2 / sk_vm_tan(0.42);
}

/* A point of the world into P: its place on screen, its depth along the
 * view and its scale. */
static void project(g3 *g, double x, double y, double z)
{
    double X = x * g->r[0] + y * g->r[1];
    double Y = x * g->u[0] + y * g->u[1] + z * g->u[2];
    double d = SK_MAX(1, g->D - (x * g->e[0] + y * g->e[1] + z * g->e[2]));
    g->Pk = g->f / d;
    g->Px = g->cx + X * g->Pk;
    g->Py = g->cy - Y * g->Pk;
    g->Pd = d;
}

/* The Einstein radius, in pixels squared, of hole o for a source at
 * depth d (INFINITY for the sky): theta_E^2 = 2 r_s (D_s - D_l) / (D_l
 * D_s), a point-mass lens. */
static double einstein(const g3 *g, const screen_hole *o, double d)
{
    if (isinf(d))
        return g->f * g->f * 2 * o->s / o->d;
    return d <= o->d ? 0 : g->f * g->f * 2 * o->s * (d - o->d) / (o->d * d);
}

/* P moved to its outer image through every hole in front of it, those
 * of holes for which in_front says so. Its magnification in Pmu. */
static void lens(g3 *g, double d)
{
    g->Pmu = 1;
    for (size_t i = 0; i < g->holes.len; i++) {
        const screen_hole *o = &g->holes.ptr[i];
        if (!isinf(d) && !(o->d < d))
            continue;
        double t2 = einstein(g, o, d);
        if (!t2)
            continue;
        double bx = g->Px - o->x, by = g->Py - o->y, b = sqrt(bx * bx + by * by);
        if (!b)
            b = 1e-3;
        double te = sqrt(t2), u = b / te, s = sqrt(u * u + 4), tp = (u + s) / 2;
        g->Px = o->x + bx / b * tp * te;
        g->Py = o->y + by / b * tp * te;
        g->Pmu *= (u * u + 2) / (2 * u * s) + 0.5;
    }
}

/* The colour of style(): hue, saturation and lightness truncated to
 * whole numbers. */
static sk_color style(double hue, double sat, double light)
{
    return sk_vc_hsl((int)hue, (int)sat, (int)light);
}

/* The distant stars and the galaxy's glow, lensed by each hole: a
 * star's outer image pushed out, its inner one inside the Einstein
 * ring, both drawn as arcs when it lies close behind the hole, a ring
 * when it lines up. */
static void draw_sky(g3 *g, sk_vc *c, double dim, double w, double h)
{
    sk_vc_op(c, SK_BLEND_ADD);
    const double *r = g->r, *u = g->u, *e = g->e;
    double f = g->f, cx = g->cx, cy = g->cy;
    for (int k = 0; k < HAZE; k++) {
        const sky_haze *o = &g->haze[k];
        const double *n = o->n;
        double Z = -(n[0] * e[0] + n[1] * e[1] + n[2] * e[2]);
        if (Z < 0.1)
            continue;
        g->Px = cx + f * (n[0] * r[0] + n[1] * r[1]) / Z;
        g->Py = cy - f * (n[0] * u[0] + n[1] * u[1] + n[2] * u[2]) / Z;
        lens(g, INFINITY);
        double sz = o->size * f * SK_MIN(2, sqrt(g->Pmu));
        if (g->Px < -sz || g->Py < -sz || g->Px > w + sz || g->Py > h + sz)
            continue;
        /* gravity3d.js stretches a sprite of this gradient over the
         * square; drawn as the gradient itself, at the pane's pixels. */
        sk_vc_alpha(c, SK_MIN(1, o->a * dim));
        sk_vc_paint p = o->warm ? g->warm_dot : g->blue_dot;
        p.x0 = p.x1 = g->Px;
        p.y0 = p.y1 = g->Py;
        p.r1 = sz;
        sk_vc_fill_rect(c, g->Px - sz, g->Py - sz, 2 * sz, 2 * sz, &p);
    }
    for (int k = 0; k < STARS; k++) {
        const sky_star *s = &g->stars[k];
        const double *n = s->n;
        double Z = -(n[0] * e[0] + n[1] * e[1] + n[2] * e[2]);
        if (Z < 0.1)
            continue;
        double x0 = cx + f * (n[0] * r[0] + n[1] * r[1]) / Z;
        double y0 = cy - f * (n[0] * u[0] + n[1] * u[1] + n[2] * u[2]) / Z;
        g->Px = x0;
        g->Py = y0;
        lens(g, INFINITY);
        double a = s->b * dim;
        if (g->Px >= 0 && g->Py >= 0 && g->Px < w && g->Py < h)
            sk_vc_batch(c, s->fill, a * SK_MIN(2, g->Pmu), g->Px, g->Py, s->size * SK_MIN(2.5, sqrt(g->Pmu)));
        /* Inner images and arcs, by the hole nearest the star's line. */
        for (size_t i = 0; i < g->holes.len; i++) {
            const screen_hole *o = &g->holes.ptr[i];
            double te = sqrt(einstein(g, o, INFINITY));
            double bx = x0 - o->x, by = y0 - o->y, b = sqrt(bx * bx + by * by);
            double uu = b / te;
            if (uu > 2.5)
                continue;
            double sq = sqrt(uu * uu + 4), tm = (sq - uu) / 2;
            double mum = (uu * uu + 2) / (2 * uu * sq) - 0.5;
            double ang = sk_vm_atan2(by, bx);
            if (uu < 0.3) {
                /* The star seen as a disc of radius RHO theta_E: an arc
                 * of half angle asin(RHO / u) on each side, a ring
                 * within RHO. */
                const double RHO = 0.05;
                double half = uu <= RHO ? SK_PI : sk_vm_asin(RHO / uu);
                sk_vc_paint p = sk_vc_solid(s->arc);
                sk_vc_alpha(c, SK_MIN(1, a * (0.5 + 0.5 * (1 - uu / 0.3))));
                sk_vc_line_width(c, 0.8 + s->size * 0.3);
                sk_vc_begin(c);
                sk_vc_arc(c, o->x, o->y, (uu + sq) / 2 * te, ang - half, ang + half, false);
                g->arcs++;
                sk_vc_stroke(c, &p);
                sk_vc_begin(c);
                sk_vc_arc(c, o->x, o->y, tm * te, ang + SK_PI - half, ang + SK_PI + half, false);
                sk_vc_stroke(c, &p);
            } else if (mum > 0.02) {
                sk_vc_batch(c, s->fill, a * mum * 1.5, o->x - bx / b * tm * te, o->y - by / b * tm * te,
                            s->size * SK_MIN(1.5, sqrt(mum) + 0.5));
            }
        }
    }
    sk_vc_flush(c);
    sk_vc_op(c, SK_BLEND_SRC_OVER);
}

/* A hole: the soft glow of its lensed disc light, its shadow and its
 * photon ring at sqrt(27) / 2 r_s apparent. A spinning hole's shadow is
 * flattened on its approaching side and shifted toward the receding
 * one, by about 2 a sin i, i its tilt from the line of sight. */
static void draw_hole(g3 *g, sk_vc *c, const screen_hole *o, double dim)
{
    const sk_g3_body *b = &g->bodies.ptr[o->b];
    double R = RING * o->s * o->k;
    double t[3] = {g->e[1] * b->jz - g->e[2] * b->jy, g->e[2] * b->jx - g->e[0] * b->jz,
                   g->e[0] * b->jy - g->e[1] * b->jx};
    double tx = t[0] * g->r[0] + t[1] * g->r[1] + t[2] * g->r[2];
    double ty = -(t[0] * g->u[0] + t[1] * g->u[1] + t[2] * g->u[2]);
    double sini = sqrt(tx * tx + ty * ty);
    if (sini > 1e-6) {
        tx /= sini;
        ty /= sini;
    }
    double shift = 2 * b->chi * b->m / C2 * sini * o->k, flat = 1 - 0.22 * b->chi * sini;
    double ox = o->x - tx * shift, oy = o->y - ty * shift;
    sk_vc_paint glow = sk_vc_radial(ox, oy, R * 0.9, ox, oy, R * 2.2);
    sk_vc_add_stop(&glow, 0, sk_vc_hsla(36, 100, 70, (0.25 + 0.4 * b->flash) * dim));
    sk_vc_add_stop(&glow, 1, sk_vc_hsla(36, 100, 50, 0));
    sk_vc_fill_rect(c, ox - R * 2.2, oy - R * 2.2, R * 4.4, R * 4.4, &glow);
    sk_vc_begin(c);
    for (int k = 0; k <= 48; k++) {
        double a = k / 48.0 * SK_TAU, p = sk_vm_cos(a) * R, q = sk_vm_sin(a) * R;
        if (p > 0)
            p *= flat;
        double x = ox + p * tx - q * ty, y = oy + p * ty + q * tx;
        if (k)
            sk_vc_line(c, x, y);
        else
            sk_vc_move(c, x, y);
    }
    sk_vc_close(c);
    sk_vc_paint black = sk_vc_solid(SK_RGB(0, 0, 0));
    sk_vc_fill(c, &black, SK_FILL_NONZERO);
    sk_vc_paint ring = sk_vc_solid(sk_vc_hsla(40, 100, 80 + 15 * b->flash, SK_MIN(1, (0.55 + 0.45 * b->flash) * dim)));
    sk_vc_line_width(c, SK_MAX(1, R * 0.07) * (1 + b->flash));
    sk_vc_stroke(c, &ring);
}

/* The layer of what lies at depth d: the holes in front of it. */
static int layer_of(const g3 *g, double d)
{
    int n = 0;
    for (size_t i = 0; i < g->holes.len; i++)
        if (g->holes.ptr[i].d > d)
            n++;
    return n;
}

/* A body's trail: its places, kept in the world as the camera moves,
 * projected each frame, fading with age. */
static void draw_trail(g3 *g, sk_vc *c, const sk_g3_body *b, int L, double dim, double keep)
{
    int len = b->n_trail;
    int n = (int)SK_MIN(len, ceil(keep * len));
    if (n < 2)
        return;
    const int CH = 6;
    int per = (n + CH - 1) / CH;
    sk_vc_line_width(c, SK_MAX(1, SK_MIN(3, b->r * b->pk * 0.8)));
    int start = len - n;
    for (int ch = 0; ch < CH; ch++) {
        int i0 = start + ch * per, i1 = SK_MIN(start + n - 1, i0 + per);
        if (i1 <= i0)
            continue;
        int m = 3 * ((i0 + i1) >> 1);
        project(g, b->trail[m], b->trail[m + 1], b->trail[m + 2]);
        if (layer_of(g, g->Pd) != L)
            continue;
        double a = (ch + 1.0) / CH * 0.9 * dim;
        sk_vc_paint p = sk_vc_solid(style(b->hue, b->comet ? 40 : 85, b->comet ? 80 : 60));
        sk_vc_alpha(c, sk_vm_round(a * 20) / 20);
        sk_vc_begin(c);
        for (int i = i0; i <= i1; i++) {
            project(g, b->trail[3 * i], b->trail[3 * i + 1], b->trail[3 * i + 2]);
            if (i == i0)
                sk_vc_move(c, g->Px, g->Py);
            else
                sk_vc_line(c, g->Px, g->Py);
        }
        if (i1 == start + n - 1)
            sk_vc_line(c, b->px, b->py);
        sk_vc_stroke(c, &p);
    }
    sk_vc_alpha(c, 1);
}

static void draw_body(g3 *g, sk_vc *c, const sk_g3_body *b, double dim)
{
    double fog = SK_MAX(0.5, SK_MIN(1.1, 1.3 - 0.5 * b->pd / g->D)), lit = dim * fog;
    double r = SK_MAX(1.5, b->r * b->pk * (b->star ? 1 : 1.4)), hsl[3];
    hot(b->hue, b->star ? 100 : b->comet ? 40 : 80, b->star ? 90 : b->comet ? 85 : 62, b->heat, hsl);
    int hue = (int)hsl[0];
    if (b->star || b->heat > 0.15) {
        double R = b->star ? r * 3.2 : r * (1.5 + 3 * b->heat);
        sk_vc_paint glow = sk_vc_radial(b->px, b->py, r * 0.3, b->px, b->py, R);
        sk_vc_add_stop(&glow, 0, sk_vc_hsla(hue, 100, 85, lit));
        sk_vc_add_stop(&glow, 0.35, sk_vc_hsla(hue, 100, 60, 0.35 * lit));
        sk_vc_add_stop(&glow, 1, sk_vc_hsla(hue, 100, 50, 0));
        sk_vc_fill_rect(c, b->px - R, b->py - R, 2 * R, 2 * R, &glow);
    }
    sk_vc_paint p = sk_vc_solid(sk_vc_hsl(hue, (int)hsl[1], (int)(hsl[2] * lit)));
    sk_vc_begin(c);
    sk_vc_arc(c, b->px, b->py, r, 0, SK_TAU, false);
    sk_vc_fill(c, &p, SK_FILL_NONZERO);
}

/* The heat of a point from the holes: 0 far out, about 0.6 at the last
 * stable orbit, 1 within 2 r_s; and in hot_hole the hole that heats it
 * most. skip: a body not to count, or SIZE_MAX. */
static double heat_at(g3 *g, double x, double y, double z, size_t skip)
{
    double heat = 0;
    g->hot_hole = SIZE_MAX;
    for (size_t i = 0; i < g->holes.len; i++) {
        const screen_hole *o = &g->holes.ptr[i];
        if (o->b == skip)
            continue;
        const sk_g3_body *b = &g->bodies.ptr[o->b];
        double d = len3(x - b->x, y - b->y, z - b->z);
        double t = SK_MIN(1, (2.3 * o->s / d) * (2.3 * o->s / d));
        if (t > heat) {
            heat = t;
            g->hot_hole = o->b;
        }
    }
    return heat;
}

static void g3_init(sk_vmode *m)
{
    g3 *g = (g3 *)m;
    make_sky(PITCH, g->stars, g->haze);
    for (int k = 0; k < 2; k++) {
        double hue = k ? 32 : 228;
        sk_vc_paint p = sk_vc_radial(0, 0, 0, 0, 0, 1);
        sk_vc_add_stop(&p, 0, sk_vc_hsla(hue, 60, 75, 1));
        sk_vc_add_stop(&p, 0.4, sk_vc_hsla(hue, 60, 55, 0.35));
        sk_vc_add_stop(&p, 1, sk_vc_hsla(hue, 60, 40, 0));
        if (k)
            g->warm_dot = p;
        else
            g->blue_dot = p;
    }
    g->yaw = 0.6;
    g->pitch = PITCH;
    g->held = -1e9;
    g->D = 620;
    seed(g);
}

static void g3_fini(sk_vmode *m)
{
    g3 *g = (g3 *)m;
    sk_vec_free(&g->bodies);
    sk_vec_free(&g->dust);
    sk_vec_free(&g->flares);
    sk_vec_free(&g->merged);
    sk_vec_free(&g->swallowed);
    sk_vec_free(&g->holes);
    free(g->eaten);
}

static int by_depth(const void *a, const void *b)
{
    double x = ((const screen_hole *)a)->d, y = ((const screen_hole *)b)->d;
    return (x < y) - (x > y);
}

#define FLARE_LIVE(f) (g->clock - (f).t < 18)

static void g3_draw(sk_vmode *m, sk_synth *s, double dt)
{
    g3 *g = (g3 *)m;
    int w = m->w, h = m->h;
    if (w < 4 || h < 4)
        return;
    sk_vmode_events(m, s);
    for (size_t k = 0; k < m->ev.len; k++)
        react(g, &m->ev.ptr[k]);
    const sk_synth_levels *L = &s->level;
    /* Simulation time per second: the clock, as in the Gravity mode. */
    double speed = (6 + 30 * SK_MIN(1, L->bass) + 14 * L->busy + 10 * L->treble) * (1 - 0.9 * L->idle);
    double left = SK_MIN(speed * dt, 60 * H);
    g->merged.len = 0;
    g->swallowed.len = 0;
    while (left > 0) {
        double hh = SK_MIN(H, left);
        sk_g3_step(&g->bodies, hh, &g->merged, &g->swallowed);
        g->eaten = sk_xreallocarray(g->eaten, g->bodies.len ? g->bodies.len : 1, sizeof *g->eaten);
        memset(g->eaten, 0, (g->bodies.len ? g->bodies.len : 1) * sizeof *g->eaten);
        sk_g3_fall(&g->dust, &g->bodies, hh, g->eaten);
        for (size_t i = 0; i < g->bodies.len; i++)
            if (g->eaten[i]) {
                g->bodies.ptr[i].flash = SK_MIN(1, g->bodies.ptr[i].flash + 0.03 * g->eaten[i]);
                g->n_swallowed += g->eaten[i];
            }
        feed(g, hh);
        left -= hh;
        g->clock += hh;
    }
    if (g->merged.len || g->swallowed.len)
        sk_g3_centre(&g->bodies, &g->dust);
    for (size_t k = 0; k < g->merged.len; k++) {
        const sk_g3_event *e = &g->merged.ptr[k];
        add_flare(g, e->x, e->y, e->z, 6 + 2 * sk_vm_cbrt(e->m));
    }
    /* A swallowing flashes its hole, if it is still one. */
    for (size_t k = 0; k < g->swallowed.len; k++) {
        for (size_t i = 0; i < g->bodies.len; i++)
            if (g->bodies.ptr[i].id == g->swallowed.ptr[k].hole)
                g->bodies.ptr[i].flash = 1;
        g->n_swallowed++;
    }
    size_t kept = 0;
    for (size_t i = 0; i < g->bodies.len; i++) {
        const sk_g3_body *b = &g->bodies.ptr[i];
        if (b->star || b->hole || len3(b->x, b->y, b->z) < VIEW * 4)
            g->bodies.ptr[kept++] = *b;
    }
    g->bodies.len = kept;
    kept = 0;
    for (size_t i = 0; i < g->dust.len; i++) {
        const sk_g3_grain *d = &g->dust.ptr[i];
        bool keep = !d->gone && len3(d->x, d->y, d->z) < VIEW * 2;
        for (size_t k = 0; keep && k < g->bodies.len; k++) {
            const sk_g3_body *b = &g->bodies.ptr[k];
            double dx = b->x - d->x, dy = b->y - d->y, dz = b->z - d->z;
            keep = b->hole || dx * dx + dy * dy + dz * dz > b->r * b->r;
        }
        if (keep)
            g->dust.ptr[kept++] = *d;
    }
    g->dust.len = kept;
    if (g->bodies.len < 2)
        seed(g);
    for (size_t i = 0; i < g->bodies.len; i++) {
        sk_g3_body *b = &g->bodies.ptr[i];
        b->flash *= sk_vm_exp(-dt / 0.6);
        /* A place every 1.5 time units, or every 4 units of travel. */
        if (b->hole || b->star)
            continue;
        int k = b->n_trail - 1;
        if (k >= 0) {
            double dx = b->x - b->trail[3 * k], dy = b->y - b->trail[3 * k + 1], dz = b->z - b->trail[3 * k + 2];
            if (g->clock - b->tt < 1.5 && dx * dx + dy * dy + dz * dz < 16)
                continue;
        }
        b->tt = g->clock;
        if (b->n_trail == 64) {
            memmove(b->trail, b->trail + 3, 3 * 63 * sizeof *b->trail);
            b->n_trail = 63;
        }
        b->trail[3 * b->n_trail] = b->x;
        b->trail[3 * b->n_trail + 1] = b->y;
        b->trail[3 * b->n_trail + 2] = b->z;
        b->n_trail++;
    }

    /* The camera turns slowly by itself, unless the pointer turned it in
     * the last 4 s. */
    double now = s->t;
    if (now - g->held > 4)
        g->yaw += dt * 0.06 * (1 - 0.8 * L->idle);
    aim(g, w, h);
    double dim = 1 - 0.45 * L->idle;
    /* Trails: long while busy, gone while idle, when time all but
     * stops. */
    double keep = 1 - 0.85 * L->idle;
    sk_vc *c = &m->vc;
    sk_vc_op(c, SK_BLEND_SRC_OVER);
    sk_vc_clear(c, SK_RGB(0, 0, 0));

    /* The holes on screen, far to near: a body or a grain is drawn after
     * every hole behind it and before every one in front. */
    g->holes.len = 0;
    for (size_t i = 0; i < g->bodies.len; i++) {
        const sk_g3_body *b = &g->bodies.ptr[i];
        if (!b->hole)
            continue;
        project(g, b->x, b->y, b->z);
        screen_hole o = {i, g->Px, g->Py, g->Pd, g->Pk, sk_g3_rs(b->m)};
        sk_vec_push(&g->holes, o);
    }
    if (g->holes.len > 1)
        qsort(g->holes.ptr, g->holes.len, sizeof *g->holes.ptr, by_depth);
    draw_sky(g, c, dim, w, h);
    for (size_t i = 0; i < g->bodies.len; i++) {
        sk_g3_body *b = &g->bodies.ptr[i];
        project(g, b->x, b->y, b->z);
        b->pd = g->Pd;
        b->pk = g->Pk;
        b->px = g->Px;
        b->py = g->Py;
        b->layer = layer_of(g, g->Pd);
        b->heat = b->hole ? 0 : heat_at(g, b->x, b->y, b->z, i);
    }
    for (size_t i = 0; i < g->dust.len; i++) {
        sk_g3_grain *d = &g->dust.ptr[i];
        project(g, d->x, d->y, d->z);
        double x0 = g->Px, y0 = g->Py;
        d->pd = g->Pd;
        d->pk = g->Pk;
        d->layer = layer_of(g, g->Pd);
        lens(g, d->pd);
        d->px = g->Px;
        d->py = g->Py;
        d->mu = g->Pmu;
        /* The inner image, inside the ring, of a grain close behind a
         * hole. */
        d->inner = false;
        for (size_t k = 0; k < g->holes.len; k++) {
            const screen_hole *o = &g->holes.ptr[k];
            if (!(o->d < d->pd))
                continue;
            double t2 = einstein(g, o, d->pd);
            if (!t2)
                continue;
            double bx = x0 - o->x, by = y0 - o->y, b = sqrt(bx * bx + by * by);
            if (!b)
                b = 1e-3;
            double te = sqrt(t2), u = b / te;
            if (u > 2)
                continue;
            double sq = sqrt(u * u + 4), tm = (sq - u) / 2;
            d->inner = true;
            d->ix = o->x - bx / b * tm * te;
            d->iy = o->y - by / b * tm * te;
            d->imu = (u * u + 2) / (2 * u * sq) - 0.5;
        }
        d->heat = heat_at(g, d->x, d->y, d->z, SIZE_MAX);
        /* Beaming: brighter where the grain comes toward the camera. */
        double vl = 0;
        if (g->hot_hole != SIZE_MAX && d->heat > 0.05) {
            const sk_g3_body *o = &g->bodies.ptr[g->hot_hole];
            vl = ((d->vx - o->vx) * g->e[0] + (d->vy - o->vy) * g->e[1] + (d->vz - o->vz) * g->e[2]) / C;
        }
        d->beam = SK_MAX(0.35, SK_MIN(2.2, (1 + 0.6 * vl) * (1 + 0.6 * vl)));
    }
    /* The bodies not holes, far first. */
    size_t *order = sk_xmalloc((g->bodies.len ? g->bodies.len : 1) * sizeof *order);
    size_t no = 0;
    for (size_t i = 0; i < g->bodies.len; i++)
        if (!g->bodies.ptr[i].hole)
            order[no++] = i;
    for (size_t i = 1; i < no; i++)
        for (size_t j = i; j > 0 && g->bodies.ptr[order[j - 1]].pd < g->bodies.ptr[order[j]].pd; j--) {
            size_t t = order[j];
            order[j] = order[j - 1];
            order[j - 1] = t;
        }
    sk_vc_cap(c, SK_CAP_ROUND);
    for (int Lr = 0; Lr <= (int)g->holes.len; Lr++) {
        for (size_t i = 0; i < no; i++) {
            const sk_g3_body *b = &g->bodies.ptr[order[i]];
            if (b->n_trail)
                draw_trail(g, c, b, Lr, dim, keep);
        }
        sk_vc_op(c, SK_BLEND_ADD);
        for (size_t i = 0; i < g->dust.len; i++) {
            const sk_g3_grain *d = &g->dust.ptr[i];
            if (d->layer != Lr)
                continue;
            double hsl[3], heat = d->heat;
            hot(d->hue, 70, 75, sk_vm_round(heat * 12) / 12, hsl);
            double fog = SK_MAX(0.5, SK_MIN(1.1, 1.3 - 0.5 * d->pd / g->D));
            double a = (0.55 + 0.45 * heat) * dim * fog * (heat > 0.05 ? d->beam : 1);
            sk_color fill = style(hsl[0], hsl[1], hsl[2]);
            sk_vc_batch(c, fill, a, d->px, d->py, (1.2 + 1.3 * heat) * SK_MIN(2, sqrt(d->mu)));
            if (d->inner && d->imu > 0.03)
                sk_vc_batch(c, fill, a * d->imu * 2, d->ix, d->iy, 1.5);
        }
        sk_vc_flush(c);
        sk_vc_op(c, SK_BLEND_SRC_OVER);
        for (size_t i = 0; i < no; i++)
            if (g->bodies.ptr[order[i]].layer == Lr)
                draw_body(g, c, &g->bodies.ptr[order[i]], dim);
        if (Lr < (int)g->holes.len)
            draw_hole(g, c, &g->holes.ptr[Lr], dim);
    }
    free(order);

    SK_VIZ_KEEP(&g->flares, FLARE_LIVE);
    sk_vc_line_width(c, 2);
    for (size_t i = 0; i < g->flares.len; i++) {
        const flare *f = &g->flares.ptr[i];
        double age = g->clock - f->t, a = (1 - age / 18) * (1 - age / 18);
        project(g, f->x, f->y, f->z);
        sk_vc_paint p = sk_vc_solid(sk_vc_rgba(255, 230, 170, a));
        sk_vc_begin(c);
        sk_vc_arc(c, g->Px, g->Py, (f->r + age * 3) * g->Pk, 0, SK_TAU, false);
        sk_vc_stroke(c, &p);
    }
    g->tick = now;
}

/* A drag turns the camera about the centre: across, about the vertical,
 * and up and down, its height. A click throws a moon from the point
 * under it, on the plane through the centre facing the camera, on a
 * circular orbit about the heaviest body. */
static void g3_pointer(sk_vmode *m, int kind, double x, double y, bool down)
{
    g3 *g = (g3 *)m;
    if (kind == SK_VIZ_DOWN) {
        g->pressed = true;
        g->moved = false;
        g->px0 = x;
        g->py0 = y;
        g->pyaw = g->yaw;
        g->ppitch = g->pitch;
        return;
    }
    if (!g->pressed)
        return;
    double dx = x - g->px0, dy = y - g->py0;
    if (kind == SK_VIZ_MOVE && down) {
        if (!g->moved && sqrt(dx * dx + dy * dy) >= 5) {
            g->moved = true;
            g->turns++;
        }
        if (g->moved) {
            g->yaw = g->pyaw - dx * 0.008;
            g->pitch = SK_MAX(-1.45, SK_MIN(1.45, g->ppitch + dy * 0.008));
            g->held = g->tick;
        }
        return;
    }
    if (kind != SK_VIZ_UP)
        return;
    bool moved = g->moved || sqrt(dx * dx + dy * dy) >= 5;
    g->pressed = false;
    if (moved || !g->f)
        return;
    double k = g->f / g->D, X = (x - g->cx) / k, Y = -(y - g->cy) / k, p[3];
    for (int i = 0; i < 3; i++)
        p[i] = X * g->r[i] + Y * g->u[i];
    sk_g3_body s = g->bodies.ptr[sun(g)];
    double rel[3] = {p[0] - s.x, p[1] - s.y, p[2] - s.z}, d = len3(rel[0], rel[1], rel[2]);
    double least = s.hole ? 3.3 * sk_g3_rs(s.m) : s.r + 4;
    if (d < 1e-6) {
        memcpy(rel, g->r, sizeof rel);
        d = 1;
    }
    if (d < least) {
        for (int i = 0; i < 3; i++)
            rel[i] = rel[i] / d * least;
        d = least;
    }
    double n[3] = {0, 0, 1};
    if (s.hole) {
        n[0] = s.jx;
        n[1] = s.jy;
        n[2] = s.jz;
    }
    double t[3] = {n[1] * rel[2] - n[2] * rel[1], n[2] * rel[0] - n[0] * rel[2], n[0] * rel[1] - n[1] * rel[0]};
    if (len3(t[0], t[1], t[2]) < 1e-3 * d) {
        t[0] = g->u[1] * rel[2] - g->u[2] * rel[1];
        t[1] = g->u[2] * rel[0] - g->u[0] * rel[2];
        t[2] = g->u[0] * rel[1] - g->u[1] * rel[0];
    }
    double tl = len3(t[0], t[1], t[2]), v = sk_g3_circular(&s, d);
    body(g, s.x + rel[0], s.y + rel[1], s.z + rel[2], s.vx + v * t[0] / tl, s.vy + v * t[1] / tl, s.vz + v * t[2] / tl,
         2 + rnd(g) * 3, 55, KIND_PLANET);
    g->thrown++;
    trim(g);
}

const sk_vmode_class sk_vmode_gravity3d = {sizeof(g3), g3_init, g3_draw, g3_pointer, g3_fini};
