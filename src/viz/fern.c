/* Fern mode (decision 0015), app/viz/fern.js: a night garden of fern
 * fronds, one a turn. A prompt sends up a fiddlehead, which unrolls
 * along its spiral as the turn works, pinna by pinna from the foot, and
 * opens at the stop. A tool call lights the pinna behind the coil in its
 * class's colour, which keeps a trace, and looses spores of that
 * colour. An output event runs light up the rachis, a subagent sprouts
 * a small frond at the turn's foot, a compaction withers the older half
 * of the garden, and the stop and the treble blow wind through it. At
 * idle the fronds curl half shut, the hues pale to moonlight and
 * fireflies come out. Behind, a still forest of the four species grown
 * by the chaos game. A press curls the fronds near it, and gold spores
 * rise from it.
 *
 * The light is drawn as fern.js draws it, a dot a pixel, into buffers
 * of about 400 by 400 points whatever the pane, which the pane shows
 * stretched, as the page's canvas showed it. */
#include "fern.h"

#include "internal.h"

const double sk_fern_species[SK_FERN_SPECIES][4][7] = {
    /* Barnsley's fern. */
    {{0, 0, 0, 0.16, 0, 0, 0.01},
     {0.85, 0.04, -0.04, 0.85, 0, 1.6, 0.85},
     {0.2, -0.26, 0.23, 0.22, 0, 1.6, 0.07},
     {-0.15, 0.28, 0.26, 0.24, 0, 0.44, 0.07}},
    /* Cyclosorus, a thelypteroid fern. */
    {{0, 0, 0, 0.25, 0, -0.4, 0.02},
     {0.95, 0.005, -0.005, 0.93, -0.002, 0.5, 0.84},
     {0.035, -0.2, 0.16, 0.04, -0.09, 0.02, 0.07},
     {-0.04, 0.2, 0.16, 0.04, 0.083, 0.12, 0.07}},
    /* Culcita, Barnsley's maps mutated. */
    {{0, 0, 0, 0.25, 0, -0.14, 0.02},
     {0.85, 0.02, -0.02, 0.83, 0, 1, 0.84},
     {0.09, -0.28, 0.3, 0.11, 0, 0.6, 0.07},
     {-0.09, 0.28, 0.3, 0.09, 0, 0.7, 0.07}},
    /* A fishbone fern. */
    {{0, 0, 0, 0.25, 0, -0.4, 0.02},
     {0.95, 0.002, -0.002, 0.93, -0.002, 0.5, 0.84},
     {0.035, -0.11, 0.27, 0.01, -0.05, 0.005, 0.07},
     {-0.04, 0.11, 0.27, 0.01, 0.047, 0.06, 0.07}},
};

void sk_fern_unit(const double maps[4][7], double out[4][7])
{
    double m[4][7];
    memcpy(m, maps, sizeof m);
    double *g = m[1];
    double s = (g[0] + g[3]) / 2, r = (g[1] - g[2]) / 2;
    g[0] = g[3] = s;
    g[1] = r;
    g[2] = -r;
    double det = (1 - s) * (1 - s) + r * r;
    double tx = ((1 - s) * g[4] + r * g[5]) / det;
    double ty = (-r * g[4] + (1 - s) * g[5]) / det;
    double by = m[0][5] / (1 - m[0][3]);
    double L = sqrt(tx * tx + (ty - by) * (ty - by));
    double co = (ty - by) / L, si = tx / L;
    for (int k = 0; k < 4; k++) {
        double a = m[k][0], b = m[k][1], c = m[k][2], d = m[k][3], e = m[k][4], f = m[k][5];
        double ra = co * a - si * c, rb = co * b - si * d;
        double rc = si * a + co * c, rd = si * b + co * d;
        double ux = b * by + e, uy = d * by + f - by;
        out[k][0] = ra * co - rb * si;
        out[k][1] = ra * si + rb * co;
        out[k][2] = rc * co - rd * si;
        out[k][3] = rc * si + rd * co;
        out[k][4] = (co * ux - si * uy) / L;
        out[k][5] = (si * ux + co * uy) / L;
        out[k][6] = m[k][6];
    }
}

void sk_fern_iterate(const double maps[4][7], const double cum[4], int n, double *px, double *py, sk_rng *rand,
                     void (*out)(void *ud, double x, double y), void *ud)
{
    double x = *px, y = *py;
    for (int i = 0; i < n; i++) {
        double u = sk_rng_next(rand);
        int k = 0;
        while (k < 3 && u > cum[k])
            k++;
        const double *m = maps[k];
        double nx = m[0] * x + m[1] * y + m[4];
        y = m[2] * x + m[3] * y + m[5];
        x = nx;
        if (!(fabs(x) < 1e3 && fabs(y) < 1e3)) {
            x = 0;
            y = 0;
        }
        out(ud, x, y);
    }
    *px = x;
    *py = y;
}

int sk_fern_strand(double x, double y, double a, double len, double c, double T, double bend,
                   double (*sway)(const void *ud, double s), const void *ud, double *P, int n)
{
    int k = 0;
    P[k++] = x;
    P[k++] = y;
    P[k++] = a;
    P[k++] = 0;
    double ub = 1 - SK_MIN(0.6, 1.6 * (1 - c)), s0 = 0;
    for (int i = 1; i <= n; i++) {
        double u = (double)i / n;
        double s = u <= ub ? c * u / ub : c + (1 - c) * (u - ub) / (1 - ub);
        double ds = s - s0, mid = (s + s0) / 2;
        a += (bend + (sway ? sway(ud, mid) : 0)) * ds;
        if (mid > c)
            a += T * 2 * SK_PI * 2.5 * sk_vm_pow((mid - c) / (1 - c), 1.5) * ds / (1 - c);
        double sa, ca;
        sk_vm_sincos(a, &sa, &ca);
        x += len * ds * sa;
        y -= len * ds * ca;
        P[k++] = x;
        P[k++] = y;
        P[k++] = a;
        P[k++] = s;
        s0 = s;
    }
    return n + 1;
}

/* ---- The mode ---- */

/* Rachis nodes per frond, pinna stations per side, nodes per pinna. */
#define NN 44
#define NS 22
#define NP 8
/* Turns' fronds and subagents' fronds on the ground at most. */
#define MAXF 6
#define MAXB 3
/* The stipe: the stalk below the blade, open even in a fiddlehead. */
#define STIPE 0.28
#define MAX_SPORES 400

typedef struct {
    int id;
    double x, scale, hue, open, rise, ph, side, lean;
    float tint[NS], thue[NS];
    double wither, shy;
    double sap[6];
    int n_sap;
    double born;
    bool baby;
    int parent; /* the id of a subagent's turn's frond */
    bool puff;
    int puff_j;
    double puff_h;
    double goal;
    int next;
} frond;

typedef struct {
    double x, y, vx, vy, c[3], t;
} spore;

typedef struct {
    double x, y, ph, v;
} fly;

typedef struct {
    sk_vmode base;
    double P[4 * (NN + 1)], Q[4 * (NP + 1)];
    /* The buffer of light, its backdrop, and the picture of both. */
    int bw, bh, cw, ch;
    float *R, *G, *B;
    uint8_t *ground;
    sk_image img;
    double unit[SK_FERN_SPECIES][4][7];
    double t, gust, wake, hue, ripe, grown;
    int clicks, ids;
    SK_VEC(frond) fronds;
    SK_VEC(spore) spores;
    fly flies[40];
} fern;

static double rnd(fern *f)
{
    return sk_rng_next(&f->base.rng);
}

/* A line of light into the buffer, a dot a pixel. */
static void line(fern *f, double x0, double y0, double x1, double y1, double r, double g, double b)
{
    double dx = x1 - x0, dy = y1 - y0;
    double n = SK_MAX(1, ceil(SK_MAX(fabs(dx), fabs(dy))));
    if (!(n < 1e6))
        return;
    double sx = dx / n, sy = dy / n, x = x0, y = y0;
    for (int i = 0; i < (int)n; i++) {
        /* JavaScript's x | 0 truncates toward 0, so (-1, 0) is column
         * 0 too. */
        if (x > -1 && y > -1 && x < f->bw && y < f->bh) {
            size_t at = (size_t)(int)y * (size_t)f->bw + (size_t)(int)x;
            f->R[at] += (float)r;
            f->G[at] += (float)g;
            f->B[at] += (float)b;
        }
        x += sx;
        y += sy;
    }
}

/* A soft dot of light of radius rad. */
static void glow(fern *f, double x, double y, double rad, double r, double g, double b)
{
    if (!(fabs(x) < 1e6 && fabs(y) < 1e6 && rad > 0))
        return;
    int x0 = (int)SK_MAX(0, floor(x - rad)), x1 = (int)SK_MIN(f->bw - 1, ceil(x + rad));
    int y0 = (int)SK_MAX(0, floor(y - rad)), y1 = (int)SK_MIN(f->bh - 1, ceil(y + rad));
    double k = 1 / (rad * rad);
    for (int iy = y0; iy <= y1; iy++)
        for (int ix = x0; ix <= x1; ix++) {
            double d = ((ix - x) * (ix - x) + (iy - y) * (iy - y)) * k;
            if (d >= 1)
                continue;
            double w = (1 - d) * (1 - d);
            size_t at = (size_t)iy * (size_t)f->bw + (size_t)ix;
            f->R[at] += (float)(r * w);
            f->G[at] += (float)(g * w);
            f->B[at] += (float)(b * w);
        }
}

/* The widest gap on the ground. */
static double place(fern *f)
{
    double best = 0.5, far = -1;
    for (int k = 0; k < 24; k++) {
        double x = 0.18 + 0.64 * rnd(f), d = 1;
        for (size_t i = 0; i < f->fronds.len; i++) {
            const frond *q = &f->fronds.ptr[i];
            if (!q->wither && !q->baby)
                d = SK_MIN(d, fabs(q->x - x));
        }
        if (d > far) {
            far = d;
            best = x;
        }
    }
    return best;
}

/* What a sprout is given, the rest drawn as sprout draws it. */
typedef struct {
    bool has_x, baby, has_scale, has_hue, has_side, has_lean;
    double x, scale, hue, side, lean;
    int parent;
} opts;

/* A frond: a fiddlehead at the foot of the ground. Past the cap the
 * oldest of its kind withers. Returns its index. */
static size_t sprout(fern *f, const opts *o)
{
    double x = o->has_x ? o->x : place(f);
    f->hue = fmod(f->hue + 67 + 40 * rnd(f), 360);
    /* Each leans away from the middle, as a crown of fronds spreads. */
    double side = x < 0.5 ? -1 : 1;
    frond q = {0};
    q.id = ++f->ids;
    q.x = x;
    q.scale = 0.8 + 0.2 * rnd(f);
    q.hue = f->hue;
    q.ph = 6.3 * rnd(f);
    q.side = side;
    q.lean = side * (0.05 + 0.15 * rnd(f));
    q.born = f->t;
    q.parent = -1;
    if (o->has_scale)
        q.scale = o->scale;
    if (o->has_hue)
        q.hue = o->hue;
    if (o->has_side)
        q.side = o->side;
    if (o->has_lean)
        q.lean = o->lean;
    q.baby = o->baby;
    if (o->baby)
        q.parent = o->parent;
    sk_vec_push(&f->fronds, q);
    int kin = 0;
    size_t oldest = SIZE_MAX;
    for (size_t i = 0; i < f->fronds.len; i++) {
        const frond *k = &f->fronds.ptr[i];
        if (!k->wither && k->baby == q.baby) {
            if (oldest == SIZE_MAX)
                oldest = i;
            kin++;
        }
    }
    if (kin > (q.baby ? MAXB : MAXF))
        f->fronds.ptr[oldest].wither = 0.001;
    return f->fronds.len - 1;
}

static void fern_init(sk_vmode *m)
{
    fern *f = (fern *)m;
    for (int k = 0; k < SK_FERN_SPECIES; k++)
        sk_fern_unit(sk_fern_species[k], f->unit[k]);
    f->hue = 360 * rnd(f);
    for (int i = 0; i < 40; i++) {
        f->flies[i].x = rnd(f);
        f->flies[i].y = 0.25 + 0.65 * rnd(f);
        f->flies[i].ph = 6.3 * rnd(f);
        f->flies[i].v = 0.4 + rnd(f);
    }
    for (int i = 0; i < 4; i++) {
        opts o = {0};
        o.has_x = true;
        o.x = 0.18 + 0.64 * (i + 0.3 * rnd(f)) / 3.3;
        size_t k = sprout(f, &o);
        f->fronds.ptr[k].open = 0.9 + 0.1 * rnd(f);
        f->fronds.ptr[k].rise = 1;
    }
    opts none = {0};
    size_t k = sprout(f, &none);
    f->fronds.ptr[k].open = 0.2;
    f->fronds.ptr[k].rise = 1;
}

static void fern_fini(sk_vmode *m)
{
    fern *f = (fern *)m;
    free(f->R);
    free(f->G);
    free(f->B);
    free(f->ground);
    sk_image_free(&f->img);
    sk_vec_free(&f->fronds);
    sk_vec_free(&f->spores);
}

/* The turn's frond: the newest that is no subagent's and not
 * withering. */
static size_t active(fern *f)
{
    for (size_t i = f->fronds.len; i-- > 0;)
        if (!f->fronds.ptr[i].wither && !f->fronds.ptr[i].baby)
            return i;
    opts none = {0};
    return sprout(f, &none);
}

static void react(fern *f, const sk_viz_event *e)
{
    size_t ai = active(f);
    frond *a = &f->fronds.ptr[ai];
    if (e->kind == SK_EV_PROMPT) {
        /* The last turn's frond opens the rest of the way. */
        a->goal = 1;
        opts none = {0};
        sprout(f, &none);
        f->wake = 1;
    } else if (e->kind == SK_EV_TOOL) {
        /* The pinna behind the coil lights and keeps a trace, each call
         * the next pinna up, so a turn's tools stack from the foot. */
        int j = (int)SK_MIN(NS - 1, SK_MAX(a->next, floor(0.9 * NS * a->open)));
        a->next = j + 1;
        double h = sk_viz_class_hue(e->cls);
        a->tint[j] = 1;
        a->thue[j] = (float)h;
        a->puff = true;
        a->puff_j = j;
        a->puff_h = h;
    } else if (e->kind == SK_EV_STOP) {
        f->gust = 1;
        a->goal = 1;
    } else if (e->kind == SK_EV_AGENT) {
        opts o = {0};
        o.has_x = true;
        o.x = a->x - a->side * 0.045;
        o.baby = true;
        o.parent = a->id;
        o.has_scale = true;
        o.scale = 0.3 + 0.1 * rnd(f);
        o.has_hue = true;
        o.hue = a->hue + 40;
        o.has_side = true;
        o.side = -a->side;
        o.has_lean = true;
        o.lean = -a->side * 0.3;
        sprout(f, &o);
    } else if (e->kind == SK_EV_COMPACT) {
        int live = 0;
        for (size_t i = 0; i < f->fronds.len; i++)
            if (!f->fronds.ptr[i].wither && i != ai)
                live++;
        int half = (live + 1) / 2;
        for (size_t i = 0; i < f->fronds.len && half > 0; i++)
            if (!f->fronds.ptr[i].wither && i != ai) {
                f->fronds.ptr[i].wither = 0.001;
                half--;
            }
    } else {
        if (a->n_sap == 6) {
            memmove(a->sap, a->sap + 1, 5 * sizeof *a->sap);
            a->n_sap = 5;
        }
        a->sap[a->n_sap++] = f->t;
        a->open = SK_MIN(1, a->open + 0.01);
    }
}

/* Values the backdrop's pixels take as a Uint8ClampedArray stores them:
 * clamped, ties to even. */
static uint8_t clamped(double v)
{
    if (!(v > 0))
        return 0;
    if (v >= 255)
        return 255;
    double r = floor(v + 0.5);
    if (r - v == 0.5 && fmod(r, 2) != 0)
        r -= 1;
    return (uint8_t)r;
}

typedef struct {
    fern *f;
    float *sh;
    double fx, fy, hh, mir, co, si, w;
} plot;

static void plot_point(void *ud, double x, double y)
{
    plot *p = ud;
    double ix = p->fx + (p->mir * x * p->co - y * p->si) * p->hh;
    double iy = p->fy - (p->mir * x * p->si + y * p->co) * p->hh;
    if (ix > -1 && iy > -1 && ix < p->f->bw && iy < p->f->bh)
        p->sh[(size_t)(int)iy * (size_t)p->f->bw + (size_t)(int)ix] += (float)p->w;
}

/* The backdrop, once per size: the night, the far forest of the four
 * species grown by the chaos game, the mist on the ground. */
static void backdrop(fern *f)
{
    int bw = f->bw, bh = f->bh;
    free(f->ground);
    f->ground = sk_xmalloc((size_t)3 * (size_t)bw * (size_t)bh);
    float *sh = sk_xcalloc((size_t)bw * (size_t)bh, sizeof *sh);
    double px = 0, py = 0;
    for (int k = 0; k < 22; k++) {
        double(*maps)[7] = f->unit[k % 4];
        double acc = 0, cum[4];
        for (int j = 0; j < 4; j++)
            cum[j] = (acc += maps[j][6]);
        for (int j = 0; j < 4; j++)
            cum[j] /= acc;
        int far = k % 2;
        plot p = {f, sh, 0, 0, 0, 0, 0, 0, far ? 0.35 : 0.7};
        p.hh = bh * (0.3 + 0.35 * rnd(f)) * (far ? 0.65 : 1);
        p.fx = bw * (k + rnd(f)) / 22;
        p.fy = bh * (far ? 0.88 : 1.02);
        p.mir = rnd(f) < 0.5 ? -1 : 1;
        double tilt = 0.25 * (rnd(f) - 0.5);
        p.co = sk_vm_cos(tilt);
        p.si = sk_vm_sin(tilt);
        sk_fern_iterate(maps, cum, (int)sk_vm_round(p.hh * 200), &px, &py, &f->base.rng, plot_point, &p);
    }
    for (int iy = 0; iy < bh; iy++) {
        double mist = sk_viz_smooth(((double)iy / bh - 0.5) / 0.5);
        for (int ix = 0; ix < bw; ix++) {
            size_t at = (size_t)iy * (size_t)bw + (size_t)ix;
            double d = sh[at] / (sh[at] + 2.0);
            f->ground[3 * at] = clamped(3 + 10 * mist + 24 * d);
            f->ground[3 * at + 1] = clamped(6 + 22 * mist + 54 * d);
            f->ground[3 * at + 2] = clamped(14 + 30 * mist + 60 * d);
        }
    }
    free(sh);
}

/* The wind along a rachis: its wave and its strength. */
typedef struct {
    double w0, side, ph;
} wind;

static double sway(const void *ud, double s)
{
    const wind *w = ud;
    return w->w0 * w->side * sk_vm_sin(w->ph - 3.5 * s);
}

static double tone(const frond *q, double h)
{
    return h + (40 - h) * q->wither;
}

/* One frond into the buffer. rank: 1 the newest, less the older. */
static void draw_frond(fern *f, size_t qi, const sk_synth_levels *L, double idle, double rank)
{
    frond *q = &f->fronds.ptr[qi];
    double *P = f->P, *Q = f->Q, t = f->t;
    int bw = f->bw, bh = f->bh;
    double S = bh / 380.0;
    double H = bh * q->scale * (0.3 + 0.7 * sk_viz_smooth(q->rise)) * (1 - 0.4 * sk_viz_smooth(q->wither));
    double bx = q->x * bw, by = bh * 1.01 + bh * 0.5 * sk_viz_smooth(q->wither);
    /* Asleep at idle, shy of the pointer, curled back as it withers. */
    double open = q->open * (1 - 0.5 * idle) * (1 - 0.8 * q->shy) * (1 - q->wither);
    double c = STIPE + (1 - STIPE) * open;
    double T = q->side * (0.35 + 2.2 * (1 - open));
    /* The wind: a wave out along the rachis, its own phase per frond. */
    wind wd = {(0.12 + 0.35 * f->gust + 0.15 * L->treble) * (1 - 0.7 * idle), q->side, 1.3 * t + q->ph};
    int n = sk_fern_strand(bx, by, q->lean + 0.06 * wd.w0 * sk_vm_sin(wd.ph), H, c, T, q->side * 0.35, sway, &wd, P,
                           NN);
    /* Hues drifting from the foot to the tip, moonlit at idle, amber
     * and dim as it withers, the newest brightest. */
    double drift = q->hue + 14 * t * (1 - 0.8 * idle);
    double sat = (0.9 - 0.6 * idle) * (1 - 0.6 * q->wither);
    double fresh = SK_MAX(0, 1 - (t - q->born) / 4);
    double lum = (0.25 + 0.75 * rank) * (1 - 0.35 * idle) * (1 - 0.7 * sk_viz_smooth(q->wither)) *
                 (1 + 0.8 * fresh + 0.4 * f->wake * rank);
    double amp = 0.55 * lum, v[3];
    for (int i = 1; i < n; i++) {
        sk_vc_hsl_rgb(tone(q, drift + 100 * P[4 * i + 3]), sat, 0.5, v);
        double k = 1.2 * amp;
        line(f, P[4 * i - 4], P[4 * i - 3], P[4 * i], P[4 * i + 1], v[0] * k, v[1] * k, v[2] * k);
        line(f, P[4 * i - 4] + 1, P[4 * i - 3], P[4 * i] + 1, P[4 * i + 1], v[0] * k, v[1] * k, v[2] * k);
    }
    /* The pinnae at fixed stations of the blade, so they stay put as
     * the rachis opens, each curled until the coil has passed it. */
    double flutter = 0.05 + 0.15 * L->treble + 0.2 * f->gust;
    int i = 1;
    for (int j = 0; j < NS; j++) {
        double s = STIPE + (1 - STIPE) * (j + 0.5) / NS;
        while (i < n - 1 && P[4 * i + 3] < s)
            i++;
        double s0 = P[4 * i - 1], s1 = P[4 * i + 3];
        double u = s1 > s0 ? SK_MIN(1, (s - s0) / (s1 - s0)) : 1;
        double x = P[4 * i - 4] + (P[4 * i] - P[4 * i - 4]) * u;
        double y = P[4 * i - 3] + (P[4 * i + 1] - P[4 * i - 3]) * u;
        double a = P[4 * i - 2] + (P[4 * i + 2] - P[4 * i - 2]) * u;
        double cp = sk_viz_smooth((c - s) / 0.18 + 0.1);
        double b = (s - STIPE) / (1 - STIPE);
        double lp = H * 0.24 * SK_MIN(1, 0.45 + 3 * b) * sk_vm_pow(1 - b, 0.85) * (0.2 + 0.8 * cp);
        if (lp < 1)
            continue;
        double k = amp;
        if (q->tint[j] > 0.02) {
            sk_vc_hsl_rgb(q->thue[j], 1, 0.55, v);
            k = SK_MAX(amp, 0.6) * (0.8 + 1.2 * q->tint[j]);
        } else {
            sk_vc_hsl_rgb(tone(q, drift + 100 * s + 25), sat, 0.5, v);
        }
        double r = v[0] * k, g = v[1] * k, bb = v[2] * k;
        for (int side = -1; side <= 1; side += 2) {
            double ang = a + side * (0.2 + 0.85 * cp) * (1 - 0.15 * b) +
                         flutter * sk_vm_sin(2.7 * t + 9 * s + side + q->ph);
            int mn = sk_fern_strand(x, y, ang, lp, 0.15 + 0.85 * cp, -side * (0.25 + 1.2 * (1 - cp)),
                                    side * 0.25 * cp, NULL, NULL, Q, NP);
            for (int e = 1; e < mn; e++)
                line(f, Q[4 * e - 4], Q[4 * e - 3], Q[4 * e], Q[4 * e + 1], r, g, bb);
            if (cp < 0.4 || lp < 6)
                continue;
            /* The pinnules, a comb along the pinna. */
            for (int e = 1; e < mn - 1; e++) {
                double pl = lp * 0.16 * (1 - 0.8 * e / mn) * cp;
                for (int sd = -1; sd <= 1; sd += 2) {
                    double aa = Q[4 * e + 2] + sd * 0.75, sa, ca;
                    sk_vm_sincos(aa, &sa, &ca);
                    line(f, Q[4 * e], Q[4 * e + 1], Q[4 * e] + pl * sa, Q[4 * e + 1] - pl * ca, 0.8 * r, 0.8 * g,
                         0.8 * bb);
                }
            }
        }
        if (q->puff && q->puff_j == j) {
            double h[3];
            sk_vc_hsl_rgb(q->puff_h, 1, 0.6, h);
            for (int k2 = 0; k2 < 26; k2++) {
                spore sp = {x, y, S * 50 * (rnd(f) - 0.5), S * (-15 - 40 * rnd(f)), {h[0], h[1], h[2]}, t};
                sk_vec_push(&f->spores, sp);
            }
            q = &f->fronds.ptr[qi];
            q->puff = false;
        }
    }
    /* Sap: an output event's light running up the rachis to the coil. */
    for (int k = 0; k < q->n_sap; k++) {
        double u = (t - q->sap[k]) / 1.2;
        if (u > 1)
            continue;
        int at = (int)SK_MIN(n - 1, floor(u * (n - 1)));
        sk_vc_hsl_rgb(drift + 140, 0.6, 0.7, v);
        glow(f, P[4 * at], P[4 * at + 1], 4 * S + 2, 1.5 * v[0], 1.5 * v[1], 1.5 * v[2]);
    }
    /* The coil glows while it is a coil. */
    if (open < 0.9) {
        sk_vc_hsl_rgb(drift + 100, sat, 0.6, v);
        double k = 0.5 * (1 - open) * lum;
        glow(f, P[4 * n - 4], P[4 * n - 3], 10 * S + 2, v[0] * k, v[1] * k, v[2] * k);
    }
}

#define WITHERED(q) ((q).wither < 1)
#define YOUNG(p) (f->t - (p).t <= 5)

static void fern_draw(sk_vmode *m, sk_synth *s, double dt)
{
    fern *f = (fern *)m;
    int w = m->w, h = m->h;
    if (w < 4 || h < 4)
        return;
    if (w != f->cw || h != f->ch) {
        f->cw = w;
        f->ch = h;
        double k = SK_MAX(1, sqrt((double)w * h / 160000));
        f->bw = (int)SK_MAX(1, sk_vm_round(w / k));
        f->bh = (int)SK_MAX(1, sk_vm_round(h / k));
        size_t n = (size_t)f->bw * (size_t)f->bh;
        free(f->R);
        free(f->G);
        free(f->B);
        f->R = sk_xcalloc(n, sizeof *f->R);
        f->G = sk_xcalloc(n, sizeof *f->G);
        f->B = sk_xcalloc(n, sizeof *f->B);
        sk_image_free(&f->img);
        sk_image_init(&f->img, f->bw, f->bh);
        backdrop(f);
    }
    sk_vmode_events(m, s);
    for (size_t k = 0; k < m->ev.len; k++)
        react(f, &m->ev.ptr[k]);
    const sk_synth_levels *L = &s->level;
    double idle = L->idle, bass = SK_MIN(1, L->bass);
    f->t += dt;
    f->gust *= sk_vm_exp(-dt / 1.8);
    f->wake *= sk_vm_exp(-dt / 1.5);

    /* The turn's frond rises and unrolls as the session works; on the
     * sound output alone, a frond fully open sprouts the next while the
     * sound goes on. */
    frond *a = &f->fronds.ptr[active(f)];
    a->rise = SK_MIN(1, a->rise + dt / 2.5);
    double was = a->open;
    a->open = SK_MIN(1, a->open + dt * (0.004 + 0.04 * bass + 0.02 * L->busy) * (1 - idle));
    f->grown += a->open - was;
    if (a->open > 0.97) {
        f->ripe += dt * bass;
        if (f->ripe > 4) {
            f->ripe = 0;
            opts none = {0};
            sprout(f, &none);
        }
    }
    for (size_t i = 0; i < f->fronds.len; i++) {
        frond *q = &f->fronds.ptr[i];
        if (q->goal && q->open < 1) {
            f->grown += SK_MIN(1 - q->open, dt * 0.5);
            q->open = SK_MIN(1, q->open + dt * 0.5);
        }
        if (q->baby) {
            q->rise = SK_MIN(1, q->rise + dt / 2);
            q->open = SK_MIN(1, q->open + dt * 0.1 * (1 - 0.8 * idle));
            bool gone = true;
            for (size_t k = 0; k < f->fronds.len; k++)
                if (f->fronds.ptr[k].id == q->parent) {
                    gone = f->fronds.ptr[k].wither != 0;
                    break;
                }
            if (!q->wither && gone)
                q->wither = 0.001;
        }
        if (q->wither)
            q->wither = SK_MIN(1, q->wither + dt / 5);
        q->shy *= sk_vm_exp(-dt / 2.5);
        for (int j = 0; j < NS; j++)
            if (q->tint[j] > 0.3)
                q->tint[j] = (float)SK_MAX(0.3, q->tint[j] * sk_vm_exp(-dt / 3));
    }
    SK_VIZ_KEEP(&f->fronds, WITHERED);

    /* The light fades fast at work, lingers at idle. */
    float fade = (float)sk_vm_exp(-dt / (0.06 + 0.5 * idle));
    size_t np = (size_t)f->bw * (size_t)f->bh;
    for (size_t i = 0; i < np; i++) {
        f->R[i] *= fade;
        f->G[i] *= fade;
        f->B[i] *= fade;
    }
    for (size_t i = 0; i < f->fronds.len; i++)
        draw_frond(f, i, L, idle, (double)(i + 1) / f->fronds.len);
    double S = f->bh / 380.0;
    for (size_t i = f->spores.len; i-- > 0;) {
        spore *p = &f->spores.ptr[i];
        double age = f->t - p->t;
        if (age > 5)
            continue;
        p->vy += 6 * S * dt;
        p->x += (p->vx + 14 * S * sk_vm_sin(1.3 * f->t + (double)i)) * dt;
        p->y += p->vy * dt;
        double al = 0.5 * (1 - age / 5);
        glow(f, p->x, p->y, 2.2 * S + 1, p->c[0] * al, p->c[1] * al, p->c[2] * al);
    }
    SK_VIZ_KEEP(&f->spores, YOUNG);
    if (f->spores.len > MAX_SPORES)
        SK_VIZ_SHIFT(&f->spores, f->spores.len - MAX_SPORES);
    /* Fireflies come out at idle. */
    for (int i = 0; i < 40; i++) {
        const fly *q = &f->flies[i];
        double al = (0.05 + 0.6 * idle) * SK_MAX(0, sk_vm_sin(1.7 * q->v * f->t + q->ph));
        if (al < 0.02)
            continue;
        glow(f, (q->x + 0.05 * sk_vm_sin(0.23 * f->t * q->v + q->ph)) * f->bw,
             (q->y + 0.04 * sk_vm_sin(0.31 * f->t + 2 * q->ph)) * f->bh, 2.5 * S + 1.2, al, 0.9 * al, 0.45 * al);
    }
    const double K = 0.6;
    for (int y = 0; y < f->bh; y++) {
        sk_px *row = sk_image_row(&f->img, y);
        for (int x = 0; x < f->bw; x++) {
            size_t i = (size_t)y * (size_t)f->bw + (size_t)x;
            double r = f->R[i], g = f->G[i], b = f->B[i];
            uint32_t cr = (uint32_t)SK_MIN(255, f->ground[3 * i] + 280 * r / (r + K));
            uint32_t cg = (uint32_t)SK_MIN(255, f->ground[3 * i + 1] + 280 * g / (g + K));
            uint32_t cb = (uint32_t)SK_MIN(255, f->ground[3 * i + 2] + 280 * b / (b + K));
            row[x] = 0xff000000u | cr << 16 | cg << 8 | cb;
        }
    }
    sk_vc_image(&m->vc, &f->img, 0, 0, w, h, true);
}

/* A press: the fronds near it curl up as a sensitive plant's do and
 * slowly unroll, and a puff of gold spores rises from it. */
static void fern_pointer(sk_vmode *m, int kind, double x, double y, bool down)
{
    (void)down;
    fern *f = (fern *)m;
    if (kind != SK_VIZ_DOWN || !f->cw)
        return;
    f->clicks++;
    for (size_t i = 0; i < f->fronds.len; i++)
        if (fabs(f->fronds.ptr[i].x * f->cw - x) < f->cw * 0.2)
            f->fronds.ptr[i].shy = 1;
    double S = f->bh / 380.0, c[3];
    sk_vc_hsl_rgb(48, 1, 0.6, c);
    for (int k = 0; k < 30; k++) {
        spore sp = {x * f->bw / f->cw, y * f->bh / f->ch, S * 70 * (rnd(f) - 0.5), S * (-20 - 50 * rnd(f)),
                    {c[0], c[1], c[2]}, f->t};
        sk_vec_push(&f->spores, sp);
    }
}

const sk_vmode_class sk_vmode_fern = {sizeof(fern), fern_init, fern_draw, fern_pointer, fern_fini};
