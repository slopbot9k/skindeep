/* Gravity mode (decision 0015), app/viz/gravity.js: bodies under
 * Newtonian attraction, G = 1, softened at short range, stepped by
 * leapfrog in the frame of their centre of mass, so the view never
 * drifts. Bodies that touch merge, keeping mass and momentum. Dust is
 * massless and only falls. The rates set the time step: the bass and
 * the busy level speed the clock, an idle spell slows it nearly to a
 * stop. Each event adds mass or bodies: a tool call a planet on a near
 * circular orbit in its class's colour, a prompt a comet that slings
 * past the star, a stop a flare that adds to the star's mass, a
 * subagent a second star, a compaction drags every body inward, and an
 * output event a puff of dust. The pointer throws a moon: a click drops
 * one on a circular orbit, a drag flings it from the press with the
 * drag's velocity, its aim drawn while pressed. */
#include "gravity.h"

#include "internal.h"

#define SOFT 9
#define H 0.25
#define MAX_BODIES 28
#define MAX_DUST 360
/* The view's half size, in simulation units. */
#define VIEW 240

double sk_grav_radius(double m, bool star)
{
    return star ? 4 + 1.1 * sk_vm_cbrt(m) : 1.2 + 1.3 * sk_vm_cbrt(m);
}

static void accel(sk_grav_bodies *bodies)
{
    sk_grav_body *b = bodies->ptr;
    size_t n = bodies->len;
    for (size_t i = 0; i < n; i++)
        b[i].ax = b[i].ay = 0;
    for (size_t i = 0; i < n; i++)
        for (size_t j = i + 1; j < n; j++) {
            double dx = b[j].x - b[i].x, dy = b[j].y - b[i].y;
            double d2 = dx * dx + dy * dy + SOFT;
            double f = 1 / (d2 * sqrt(d2));
            b[i].ax += dx * f * b[j].m;
            b[i].ay += dy * f * b[j].m;
            b[j].ax -= dx * f * b[i].m;
            b[j].ay -= dy * f * b[i].m;
        }
}

void sk_grav_step(sk_grav_bodies *bodies, double h, sk_grav_merges *merged)
{
    accel(bodies);
    sk_grav_body *b = bodies->ptr;
    for (size_t i = 0; i < bodies->len; i++) {
        b[i].vx += b[i].ax * h / 2;
        b[i].vy += b[i].ay * h / 2;
        b[i].x += b[i].vx * h;
        b[i].y += b[i].vy * h;
    }
    accel(bodies);
    for (size_t i = 0; i < bodies->len; i++) {
        b[i].vx += b[i].ax * h / 2;
        b[i].vy += b[i].ay * h / 2;
    }
    for (size_t i = 0; i < bodies->len; i++)
        for (size_t j = i + 1; j < bodies->len; j++) {
            sk_grav_body *a = &bodies->ptr[i], *o = &bodies->ptr[j];
            double dx = a->x - o->x, dy = a->y - o->y;
            if (dx * dx + dy * dy > (a->r + o->r) * (a->r + o->r))
                continue;
            double m = a->m + o->m;
            double hue = a->m >= o->m ? a->hue : o->hue;
            a->x = (a->x * a->m + o->x * o->m) / m;
            a->y = (a->y * a->m + o->y * o->m) / m;
            a->vx = (a->vx * a->m + o->vx * o->m) / m;
            a->vy = (a->vy * a->m + o->vy * o->m) / m;
            a->hue = hue;
            a->star = a->star || o->star;
            a->m = m;
            a->r = sk_grav_radius(m, a->star);
            /* gravity.js takes the smaller of the sum and the other's
             * mass: the other's. */
            sk_grav_merge mg = {a->x, a->y, SK_MIN(a->m, o->m)};
            if (merged)
                sk_vec_push(merged, mg);
            memmove(o, o + 1, (bodies->len - j - 1) * sizeof *o);
            bodies->len--;
            j--;
        }
}

void sk_grav_centre(sk_grav_bodies *bodies)
{
    double m = 0, px = 0, py = 0, x = 0, y = 0;
    for (size_t i = 0; i < bodies->len; i++) {
        const sk_grav_body *b = &bodies->ptr[i];
        m += b->m;
        px += b->m * b->vx;
        py += b->m * b->vy;
        x += b->m * b->x;
        y += b->m * b->y;
    }
    if (!m)
        return;
    for (size_t i = 0; i < bodies->len; i++) {
        sk_grav_body *b = &bodies->ptr[i];
        b->vx -= px / m;
        b->vy -= py / m;
        b->x -= x / m;
        b->y -= y / m;
    }
}

/* ---- The mode ---- */

typedef struct {
    double x, y, vx, vy, hue;
} grain;

typedef struct {
    double x, y, t, r;
} flare;

typedef struct {
    sk_vmode base;
    sk_grav_bodies bodies;
    SK_VEC(grain) dust;
    SK_VEC(flare) flares;
    sk_grav_merges bursts;
    double clock;
    /* The trails, kept from frame to frame, under the frame's bodies. */
    sk_image trails;
    sk_vc tvc;
    /* The view as last drawn, the pointer's press and its last point
     * while pressed, and the moons thrown. */
    bool have_view, aiming;
    double vs, vcx, vcy;
    double ax0, ay0, ax1, ay1;
    int thrown;
} gravity;

static double rnd(gravity *g)
{
    return sk_rng_next(&g->base.rng);
}

static sk_grav_body *body(gravity *g, double x, double y, double vx, double vy, double m, double hue, bool star)
{
    sk_grav_body b = {0};
    b.x = x;
    b.y = y;
    b.vx = vx;
    b.vy = vy;
    b.m = m;
    b.hue = hue;
    b.star = star;
    b.r = sk_grav_radius(m, star);
    sk_vec_push(&g->bodies, b);
    return &g->bodies.ptr[g->bodies.len - 1];
}

/* The heaviest body: the first of the heaviest. */
static sk_grav_body *sun(gravity *g)
{
    sk_grav_body *s = NULL;
    for (size_t i = 0; i < g->bodies.len; i++)
        if (!s || g->bodies.ptr[i].m > s->m)
            s = &g->bodies.ptr[i];
    return s;
}

static void planet(gravity *g, double hue, double m)
{
    sk_grav_body s = *sun(g);
    double r = 50 + rnd(g) * 160, a = rnd(g) * SK_TAU;
    double v = sqrt(s.m / r) * (0.85 + rnd(g) * 0.3);
    v *= rnd(g) < 0.15 ? -1 : 1;
    double sa, ca;
    sk_vm_sincos(a, &sa, &ca);
    body(g, s.x + r * ca, s.y + r * sa, s.vx - v * sa, s.vy + v * ca, m, hue, false);
}

static void seed(gravity *g)
{
    g->bodies.len = 0;
    g->dust.len = 0;
    body(g, 0, 0, 0, 0, 900, 48, true);
    static const double orbits[3][2] = {{60, 200}, {150, 120}, {205, 30}};
    for (int i = 0; i < 3; i++) {
        double r = orbits[i][0], v = sqrt(900 / r);
        body(g, r, 0, 0, v, 2 + rnd(g) * 3, orbits[i][1], false);
    }
    /* A belt of dust between the inner planets. */
    for (int k = 0; k < 160; k++) {
        double r = 90 + rnd(g) * 35, a = rnd(g) * SK_TAU, v = sqrt(900 / r), sa, ca;
        sk_vm_sincos(a, &sa, &ca);
        grain d = {r * ca, r * sa, -v * sa, v * ca, 40};
        sk_vec_push(&g->dust, d);
    }
    sk_grav_centre(&g->bodies);
}

/* The lightest planets past the cap gone, the oldest dust past its cap,
 * the frame recentred. */
static void trim(gravity *g)
{
    while (g->bodies.len > MAX_BODIES) {
        size_t best = SIZE_MAX;
        for (size_t i = 0; i < g->bodies.len; i++)
            if (!g->bodies.ptr[i].star && (best == SIZE_MAX || !(g->bodies.ptr[best].m < g->bodies.ptr[i].m)))
                best = i;
        if (best == SIZE_MAX)
            break;
        memmove(&g->bodies.ptr[best], &g->bodies.ptr[best + 1], (g->bodies.len - best - 1) * sizeof *g->bodies.ptr);
        g->bodies.len--;
    }
    if (g->dust.len > MAX_DUST)
        SK_VIZ_SHIFT(&g->dust, g->dust.len - MAX_DUST);
    sk_grav_centre(&g->bodies);
}

static void react(gravity *g, const sk_viz_event *e)
{
    sk_grav_body *s = sun(g);
    if (e->kind == SK_EV_TOOL) {
        planet(g, sk_viz_class_hue(e->cls), 1 + rnd(g) * 6);
    } else if (e->kind == SK_EV_PROMPT) {
        /* From the edge, aimed to pass close by the star. */
        double a = rnd(g) * SK_TAU, r = VIEW * 1.3;
        double miss = (rnd(g) < 0.5 ? -1 : 1);
        miss *= 20 + rnd(g) * 40;
        double v = sqrt(2 * s->m / r) * 1.05, sa, ca;
        sk_vm_sincos(a, &sa, &ca);
        sk_grav_body sn = *s;
        sk_grav_body *b = body(g, sn.x + r * ca, sn.y + r * sa, 0, 0, 0.5, 190, false);
        double dx = sn.x - b->x, dy = sn.y - b->y, d = sqrt(dx * dx + dy * dy);
        b->vx = sn.vx + v * (dx / d - miss / d * dy / d);
        b->vy = sn.vy + v * (dy / d + miss / d * dx / d);
        b->comet = true;
        /* The star announces the comet with a ring, before it arrives. */
        flare fl = {sn.x, sn.y, g->clock, sn.r};
        sk_vec_push(&g->flares, fl);
    } else if (e->kind == SK_EV_STOP) {
        s->m *= 1.12;
        s->r = sk_grav_radius(s->m, s->star);
        flare fl = {s->x, s->y, g->clock, s->r};
        sk_vec_push(&g->flares, fl);
    } else if (e->kind == SK_EV_AGENT) {
        int stars = 0;
        for (size_t i = 0; i < g->bodies.len; i++)
            stars += g->bodies.ptr[i].star;
        if (stars < 3) {
            double a = rnd(g) * SK_TAU, r = 150 + rnd(g) * 60, m = 150 + rnd(g) * 200;
            double v = sqrt(s->m / r) * 0.9, sa, ca;
            sk_vm_sincos(a, &sa, &ca);
            sk_grav_body sn = *s;
            body(g, sn.x + r * ca, sn.y + r * sa, sn.vx - v * sa, sn.vy + v * ca, m, 15, true);
        } else {
            planet(g, 300, 8);
        }
    } else if (e->kind == SK_EV_COMPACT) {
        for (size_t i = 0; i < g->bodies.len; i++) {
            g->bodies.ptr[i].vx *= 0.6;
            g->bodies.ptr[i].vy *= 0.6;
        }
    } else {
        sk_grav_body p = g->bodies.ptr[(size_t)floor(rnd(g) * g->bodies.len)];
        for (int k = 0; k < 14; k++) {
            double a = k / 14.0 * SK_TAU, v = 0.3 + rnd(g) * 0.5, sa, ca;
            sk_vm_sincos(a, &sa, &ca);
            grain d = {p.x + (p.r + 3) * ca, p.y + (p.r + 3) * sa, p.vx + v * ca, p.vy + v * sa, p.hue};
            sk_vec_push(&g->dust, d);
        }
    }
    trim(g);
}

/* Dust falls in the bodies' field, stepped as they are. */
static void fall(gravity *g, double h)
{
    for (size_t i = 0; i < g->dust.len; i++) {
        grain *d = &g->dust.ptr[i];
        double ax = 0, ay = 0;
        for (size_t k = 0; k < g->bodies.len; k++) {
            const sk_grav_body *b = &g->bodies.ptr[k];
            double dx = b->x - d->x, dy = b->y - d->y;
            double d2 = dx * dx + dy * dy + SOFT;
            double f = b->m / (d2 * sqrt(d2));
            ax += dx * f;
            ay += dy * f;
        }
        d->vx += ax * h;
        d->vy += ay * h;
        d->x += d->vx * h;
        d->y += d->vy * h;
    }
}

static void gravity_init(sk_vmode *m)
{
    gravity *g = (gravity *)m;
    sk_vc_init(&g->tvc, &g->trails, 1);
    seed(g);
}

static void gravity_fini(sk_vmode *m)
{
    gravity *g = (gravity *)m;
    sk_vec_free(&g->bodies);
    sk_vec_free(&g->dust);
    sk_vec_free(&g->flares);
    sk_vec_free(&g->bursts);
    sk_vc_free(&g->tvc);
    sk_image_free(&g->trails);
}

#define KEPT_BODY(b) ((b).star || sqrt((b).x * (b).x + (b).y * (b).y) < VIEW * 4)
#define FRESH_FLARE(f) (g->clock - (f).t < 25)

static bool clear_of_bodies(const gravity *g, const grain *d)
{
    if (!(sqrt(d->x * d->x + d->y * d->y) < VIEW * 2))
        return false;
    for (size_t k = 0; k < g->bodies.len; k++) {
        const sk_grav_body *b = &g->bodies.ptr[k];
        if (!((b->x - d->x) * (b->x - d->x) + (b->y - d->y) * (b->y - d->y) > b->r * b->r))
            return false;
    }
    return true;
}

static void gravity_draw(sk_vmode *m, sk_synth *s, double dt)
{
    gravity *g = (gravity *)m;
    int w = m->w, h = m->h;
    if (w < 4 || h < 4)
        return;
    if (m->fresh || g->trails.w != m->canvas.w || g->trails.h != m->canvas.h) {
        sk_image_free(&g->trails);
        sk_image_init(&g->trails, m->canvas.w, m->canvas.h);
        sk_fill_rect(&g->trails, sk_image_bounds(&g->trails), SK_RGB(0, 0, 0), SK_BLEND_SRC, NULL);
    }
    sk_vmode_events(m, s);
    for (size_t k = 0; k < m->ev.len; k++)
        react(g, &m->ev.ptr[k]);
    const sk_synth_levels *L = &s->level;
    /* Simulation time per second: the clock. */
    double speed = (6 + 30 * SK_MIN(1, L->bass) + 14 * L->busy + 10 * L->treble) * (1 - 0.9 * L->idle);
    double left = SK_MIN(speed * dt, 60 * H);
    g->bursts.len = 0;
    while (left > 0) {
        double hh = SK_MIN(H, left);
        sk_grav_step(&g->bodies, hh, &g->bursts);
        fall(g, hh);
        left -= hh;
        g->clock += hh;
    }
    if (g->bursts.len)
        sk_grav_centre(&g->bodies);
    for (size_t k = 0; k < g->bursts.len; k++) {
        const sk_grav_merge *mg = &g->bursts.ptr[k];
        flare fl = {mg->x, mg->y, g->clock, 6 + 2 * sk_vm_cbrt(mg->m)};
        sk_vec_push(&g->flares, fl);
    }
    SK_VIZ_KEEP(&g->bodies, KEPT_BODY);
    size_t kept = 0;
    for (size_t i = 0; i < g->dust.len; i++)
        if (clear_of_bodies(g, &g->dust.ptr[i]))
            g->dust.ptr[kept++] = g->dust.ptr[i];
    g->dust.len = kept;
    if (g->bodies.len < 2)
        seed(g);

    double sc = SK_MIN(w, h) / (2.0 * VIEW), cx = w / 2.0, cy = h / 2.0;
    g->have_view = true;
    g->vs = sc;
    g->vcx = cx;
    g->vcy = cy;
    /* Trails: long while busy, gone while idle, when time all but
     * stops. */
    sk_vc *tg = &g->tvc;
    sk_vc_target(tg, &g->trails, m->scale);
    sk_vc_paint p = sk_vc_solid(sk_vc_rgba(0, 0, 0, 0.04 + 0.16 * L->idle));
    sk_vc_fill_rect(tg, 0, 0, w, h, &p);
    double dim = 1 - 0.45 * L->idle;
    for (size_t i = 0; i < g->dust.len; i++) {
        const grain *d = &g->dust.ptr[i];
        sk_vc_square(tg, cx + d->x * sc, cy + d->y * sc, 1.5, 1.5, sk_vc_hsla(d->hue, 70, 75, 1), 0.8 * dim);
    }
    sk_vc_cap(tg, SK_CAP_ROUND);
    for (size_t i = 0; i < g->bodies.len; i++) {
        sk_grav_body *b = &g->bodies.ptr[i];
        double x = cx + b->x * sc, y = cy + b->y * sc;
        if (b->drawn && !b->star) {
            p = sk_vc_solid(sk_vc_hsla(b->hue, b->comet ? 40 : 85, b->comet ? 80 : 60, 0.9 * dim));
            sk_vc_line_width(tg, SK_MAX(1, SK_MIN(3, b->r * sc * 0.8)));
            sk_vc_begin(tg);
            sk_vc_move(tg, b->sx, b->sy);
            sk_vc_line(tg, x, y);
            sk_vc_stroke(tg, &p);
        }
        b->drawn = true;
        b->sx = x;
        b->sy = y;
    }
    sk_blit(&m->canvas, 0, 0, &g->trails, sk_image_bounds(&g->trails), SK_BLEND_SRC, 255, NULL);

    /* The bodies and the flares over the trails. */
    sk_vc *c = &m->vc;
    for (size_t i = 0; i < g->bodies.len; i++) {
        const sk_grav_body *b = &g->bodies.ptr[i];
        double r = SK_MAX(2, b->r * sc * (b->star ? 1 : 1.4));
        if (b->star) {
            sk_vc_paint glow = sk_vc_radial(b->sx, b->sy, r * 0.3, b->sx, b->sy, r * 3.2);
            sk_vc_add_stop(&glow, 0, sk_vc_hsla(b->hue, 100, 85, dim));
            sk_vc_add_stop(&glow, 0.35, sk_vc_hsla(b->hue, 100, 60, 0.35 * dim));
            sk_vc_add_stop(&glow, 1, sk_vc_hsla(b->hue, 100, 50, 0));
            sk_vc_fill_rect(c, b->sx - r * 3.2, b->sy - r * 3.2, r * 6.4, r * 6.4, &glow);
        }
        p = sk_vc_solid(b->star ? sk_vc_hsl(b->hue, 100, 90 * dim)
                                : sk_vc_hsl(b->hue, b->comet ? 40 : 80, (b->comet ? 85 : 62) * dim));
        sk_vc_begin(c);
        sk_vc_arc(c, b->sx, b->sy, r, 0, SK_TAU, false);
        sk_vc_fill(c, &p, SK_FILL_NONZERO);
    }
    SK_VIZ_KEEP(&g->flares, FRESH_FLARE);
    sk_vc_line_width(c, 2);
    for (size_t i = 0; i < g->flares.len; i++) {
        const flare *f = &g->flares.ptr[i];
        double a = 1 - (g->clock - f->t) / 25;
        p = sk_vc_solid(sk_vc_rgba(255, 230, 170, a));
        sk_vc_begin(c);
        sk_vc_arc(c, cx + f->x * sc, cy + f->y * sc, (f->r + (g->clock - f->t) * 3) * sc, 0, SK_TAU, false);
        sk_vc_stroke(c, &p);
    }
    if (g->aiming) {
        p = sk_vc_solid(sk_vc_rgba(255, 255, 255, 0.7));
        sk_vc_line_width(c, 1.5);
        double dash[2] = {4, 4};
        sk_vc_dash(c, dash, 2);
        sk_vc_begin(c);
        sk_vc_move(c, g->ax0, g->ay0);
        sk_vc_line(c, g->ax1, g->ay1);
        sk_vc_stroke(c, &p);
        sk_vc_dash(c, NULL, 0);
    }
}

/* A press marks the moon's place, the release throws it: on a circular
 * orbit about the heaviest body for a click, else with a velocity in
 * proportion to the drag. */
static void gravity_pointer(sk_vmode *m, int kind, double x, double y, bool down)
{
    gravity *g = (gravity *)m;
    if (kind == SK_VIZ_DOWN) {
        g->aiming = true;
        g->ax0 = g->ax1 = x;
        g->ay0 = g->ay1 = y;
    } else if (g->aiming && down) {
        g->ax1 = x;
        g->ay1 = y;
    }
    if (kind != SK_VIZ_UP || !g->aiming || !g->have_view)
        return;
    double sc = g->vs;
    double px = (g->ax0 - g->vcx) / sc, py = (g->ay0 - g->vcy) / sc;
    double dx = (g->ax1 - g->ax0) / sc, dy = (g->ay1 - g->ay0) / sc;
    g->aiming = false;
    sk_grav_body sn = *sun(g);
    double vx = sn.vx, vy = sn.vy;
    if (sqrt(dx * dx + dy * dy) * sc < 5) {
        double rx = px - sn.x, ry = py - sn.y;
        double r = SK_MAX(sn.r + 4, sqrt(rx * rx + ry * ry));
        double v = sqrt(sn.m / r);
        vx -= v * ry / r;
        vy += v * rx / r;
    } else {
        vx += dx * 0.02;
        vy += dy * 0.02;
    }
    body(g, px, py, vx, vy, 2 + rnd(g) * 3, 55, false);
    g->thrown++;
    trim(g);
}

const sk_vmode_class sk_vmode_gravity = {sizeof(gravity), gravity_init, gravity_draw, gravity_pointer, gravity_fini};
