/* Quantum mode (decision 0015), app/viz/quantum.js: a wave function on
 * a 128 by 64 grid evolved by the time-dependent Schroedinger equation,
 * hbar = m = 1, by the split-step Fourier method: half a step of the
 * potential, a step of the kinetic term in momentum space, half a step
 * of the potential. Each factor is a phase, so the step is unitary.
 * Drawn by domain colouring: the hue is the phase, the brightness the
 * amplitude. Wave packets fly at a wall with slits, and the far edge is
 * a detector screen that records each detection as a dot, drawn from
 * the probability reaching it, so the interference fringes build up dot
 * by dot. A tool call fires a packet whose wavelength is its class's, a
 * prompt changes the slits and fires, a subagent fires two at once, a
 * stop or a compaction measures the position and collapses the state to
 * a narrow packet there, and an output event fires one. While the agent
 * idles the wall fades into a harmonic trap holding a coherent state
 * that circles in it, an orbital at rest. A press and drag of the
 * pointer paints a barrier along its path, a ridge of potential above
 * the packets' energy that they scatter off, and leak through as it
 * fades. A click drops a short one. */
#include "quantum.h"

#include "internal.h"

/* ---- The wave ---- */

bool sk_wave_init(sk_wave *w, int nx, int ny, double dt)
{
    memset(w, 0, sizeof *w);
    if (!sk_fft_plan_init(&w->px, nx) || !sk_fft_plan_init(&w->py, ny)) {
        sk_fft_plan_free(&w->px);
        return false;
    }
    w->nx = nx;
    w->ny = ny;
    w->dt = dt;
    size_t n = (size_t)nx * (size_t)ny;
    w->re = sk_xcalloc(n, sizeof *w->re);
    w->im = sk_xcalloc(n, sizeof *w->im);
    w->vr = sk_xcalloc(n, sizeof *w->vr);
    w->vi = sk_xcalloc(n, sizeof *w->vi);
    w->kr = sk_xcalloc(n, sizeof *w->kr);
    w->ki = sk_xcalloc(n, sizeof *w->ki);
    for (size_t i = 0; i < n; i++)
        w->vr[i] = 1;
    for (int y = 0; y < ny; y++) {
        double ky = SK_TAU * (y < ny / 2 ? y : y - ny) / ny;
        for (int x = 0; x < nx; x++) {
            double kx = SK_TAU * (x < nx / 2 ? x : x - nx) / nx;
            double ph = -(kx * kx + ky * ky) / 2 * dt, s, c;
            sk_vm_sincos(ph, &s, &c);
            w->kr[y * nx + x] = c / (double)n;
            w->ki[y * nx + x] = s / (double)n;
        }
    }
    return true;
}

void sk_wave_free(sk_wave *w)
{
    free(w->re);
    free(w->im);
    free(w->vr);
    free(w->vi);
    free(w->kr);
    free(w->ki);
    sk_fft_plan_free(&w->px);
    sk_fft_plan_free(&w->py);
    memset(w, 0, sizeof *w);
}

void sk_wave_potential(sk_wave *w, const double *V)
{
    size_t n = (size_t)w->nx * (size_t)w->ny;
    for (size_t i = 0; i < n; i++)
        sk_vm_sincos(-V[i] * w->dt / 2, &w->vi[i], &w->vr[i]);
}

static void mul(sk_wave *w, const double *ar, const double *ai)
{
    size_t n = (size_t)w->nx * (size_t)w->ny;
    double *re = w->re, *im = w->im;
    for (size_t i = 0; i < n; i++) {
        double r = re[i] * ar[i] - im[i] * ai[i];
        im[i] = re[i] * ai[i] + im[i] * ar[i];
        re[i] = r;
    }
}

void sk_wave_damp(sk_wave *w, const double *m)
{
    size_t n = (size_t)w->nx * (size_t)w->ny;
    for (size_t i = 0; i < n; i++) {
        w->re[i] *= m[i];
        w->im[i] *= m[i];
    }
}

static void fft2(sk_wave *w, int sign)
{
    for (int y = 0; y < w->ny; y++)
        sk_fft(w->re, w->im, (size_t)y * (size_t)w->nx, 1, &w->px, sign);
    for (int x = 0; x < w->nx; x++)
        sk_fft(w->re, w->im, (size_t)x, (size_t)w->nx, &w->py, sign);
}

void sk_wave_step(sk_wave *w)
{
    mul(w, w->vr, w->vi);
    fft2(w, -1);
    mul(w, w->kr, w->ki);
    fft2(w, 1);
    mul(w, w->vr, w->vi);
}

double sk_wave_norm(const sk_wave *w)
{
    size_t n = (size_t)w->nx * (size_t)w->ny;
    double s = 0;
    for (size_t i = 0; i < n; i++)
        s += w->re[i] * w->re[i] + w->im[i] * w->im[i];
    return s;
}

void sk_wave_packet(sk_wave *w, double x0, double y0, double sx, double sy, double kx, double ky, double a)
{
    double c = sqrt(a / (2 * SK_PI * sx * sy));
    for (int y = 0; y < w->ny; y++)
        for (int x = 0; x < w->nx; x++) {
            double e = c * sk_vm_exp(-((x - x0) * (x - x0)) / (4 * sx * sx) - ((y - y0) * (y - y0)) / (4 * sy * sy));
            if (e < 1e-9)
                continue;
            double s, co;
            sk_vm_sincos(kx * x + ky * y, &s, &co);
            int i = y * w->nx + x;
            w->re[i] += e * co;
            w->im[i] += e * s;
        }
}

int sk_wave_sample(const sk_wave *w, double u01)
{
    double total = sk_wave_norm(w);
    if (total < 1e-6)
        return -1;
    double u = u01 * total;
    int n = w->nx * w->ny;
    for (int i = 0; i < n; i++) {
        u -= w->re[i] * w->re[i] + w->im[i] * w->im[i];
        if (u <= 0)
            return i;
    }
    return n - 1;
}

/* ---- The mode ---- */

#define NX 128
#define NY 64
#define DT 0.5
#define WALL 52
#define V0 4
#define SCREEN (NX - 14)
#define OMEGA 0.05
/* The painted barrier: its height, its radius in grid points, and its
 * fade's time constant in seconds. A packet's energy is k^2 / 2, about
 * 0.4 to 1. */
#define BAR_H 2.5
#define BAR_R 1.6
#define BAR_TAU 4
#define MAX_DOTS 20000

/* The slits' centres and half widths, by layout; a circle for the last. */
static const int SLITS[3][3][2] = {{{24, 2}, {40, 2}}, {{32, 3}}, {{20, 2}, {32, 2}, {44, 2}}};
static const int N_SLITS[3] = {2, 1, 3};
static const int DISC[3] = {62, 32, 7};
#define LAYOUTS 4

typedef struct {
    double x, y, t;
} dot;

typedef struct {
    sk_vmode base;
    sk_wave wave;
    double V[NX * NY], wall[NX * NY], trap[NX * NY], mask[NX * NY], bar[NX * NY], flat[NX * NY];
    sk_image buf, turned;
    /* The barrier's tallest point, the pointer's last grid point while
     * pressed, the grid's place on the pane as last drawn, and the
     * strokes painted so far. */
    double bar_top;
    bool pen;
    double pen_x, pen_y;
    bool have_view, turn;
    double vs, vsy, vox, voy;
    int strokes;
    double acc[NY], hist[NY], caught;
    int layout;
    double held, time, clock;
    SK_VEC(dot) dots;
    SK_VEC(dot) flashes;
} quantum;

static void build(quantum *q)
{
    for (int y = 0; y < NY; y++)
        for (int x = 0; x < NX; x++) {
            double v = 0;
            if (q->layout < 3) {
                bool open = false;
                for (int s = 0; s < N_SLITS[q->layout]; s++)
                    open = open || abs(y - SLITS[q->layout][s][0]) <= SLITS[q->layout][s][1];
                if (x >= WALL && x < WALL + 3 && !open)
                    v = V0;
            } else {
                int cx = DISC[0], cy = DISC[1], r = DISC[2];
                if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r)
                    v = V0;
            }
            q->wall[y * NX + x] = v;
        }
    q->held = -1;
}

/* The potential for the idle level f: the wall fading into the trap,
 * and the painted barrier. */
static void blend(quantum *q, double f)
{
    for (int i = 0; i < NX * NY; i++)
        q->V[i] = (1 - f) * q->wall[i] + f * q->trap[i] + q->bar[i];
    sk_wave_potential(&q->wave, q->V);
    q->held = f;
}

/* Raise the barrier in a disc about the grid point (gx, gy). */
static void paint_bar(quantum *q, double gx, double gy)
{
    double r2 = BAR_R * BAR_R;
    for (int y = (int)SK_MAX(0, floor(gy - BAR_R)); y <= (int)SK_MIN(NY - 1, ceil(gy + BAR_R)); y++)
        for (int x = (int)SK_MAX(0, floor(gx - BAR_R)); x <= (int)SK_MIN(NX - 1, ceil(gx + BAR_R)); x++) {
            double d2 = (x - gx) * (x - gx) + (y - gy) * (y - gy);
            if (d2 > r2)
                continue;
            int i = y * NX + x;
            q->bar[i] = SK_MAX(q->bar[i], BAR_H * (1 - 0.5 * d2 / r2));
        }
    q->bar_top = BAR_H;
    q->held = -1;
}

static void fire(quantum *q, double y0, double k)
{
    sk_wave_packet(&q->wave, 20, y0, 6, 7, k, 0, 1);
}

static void measure(quantum *q)
{
    int i = sk_wave_sample(&q->wave, sk_vmode_rand(&q->base));
    if (i < 0)
        return;
    double n = sk_wave_norm(&q->wave);
    int x = i % NX, y = i / NX;
    memset(q->wave.re, 0, NX * NY * sizeof *q->wave.re);
    memset(q->wave.im, 0, NX * NY * sizeof *q->wave.im);
    sk_wave_packet(&q->wave, x, y, 2, 2, 0, 0, n);
    dot f = {x, y, q->time};
    sk_vec_push(&q->flashes, f);
}

static void react(quantum *q, const sk_viz_event *e, double bass)
{
    sk_vmode *m = &q->base;
    if (e->kind == SK_EV_TOOL) {
        fire(q, NY / 2 + (sk_vmode_rand(m) - 0.5) * 6, 1 + 0.1 * e->cls);
    } else if (e->kind == SK_EV_PROMPT) {
        q->layout = (q->layout + 1) % LAYOUTS;
        build(q);
        fire(q, NY / 2, 1.1);
    } else if (e->kind == SK_EV_AGENT) {
        fire(q, NY / 2 - 12, 1.2);
        fire(q, NY / 2 + 12, 1.2);
    } else if (e->kind == SK_EV_STOP || e->kind == SK_EV_COMPACT) {
        measure(q);
    } else {
        fire(q, NY / 2 + (sk_vmode_rand(m) - 0.5) * 6, 0.9 + 0.5 * SK_MIN(1, bass));
    }
}

/* Probability absorbed by the screen becomes detections, each where the
 * absorbed probability says. */
static void detect(quantum *q)
{
    const double *re = q->wave.re, *im = q->wave.im;
    double sum = 0;
    for (int y = 0; y < NY; y++)
        for (int x = SCREEN; x < NX; x++) {
            int i = y * NX + x;
            double p = (re[i] * re[i] + im[i] * im[i]) * (1 - q->mask[i] * q->mask[i]);
            q->acc[y] += p;
            sum += p;
        }
    q->caught += sum;
    const double Q = 0.03;
    while (q->caught >= Q) {
        q->caught -= Q;
        double total = 0;
        for (int y = 0; y < NY; y++)
            total += q->acc[y];
        double u = sk_vmode_rand(&q->base) * total;
        int y = 0;
        while (y < NY - 1 && (u -= q->acc[y]) > 0)
            y++;
        q->hist[y] += 1;
        dot d = {SCREEN + 2 + sk_vmode_rand(&q->base) * (NX - SCREEN - 4), y + sk_vmode_rand(&q->base) - 0.5, q->time};
        if (q->dots.len < MAX_DOTS)
            sk_vec_push(&q->dots, d);
    }
    if (q->caught < 1e-4)
        memset(q->acc, 0, sizeof q->acc);
}

static void quantum_init(sk_vmode *m)
{
    quantum *q = (quantum *)m;
    sk_wave_init(&q->wave, NX, NY, DT);
    for (int y = 0; y < NY; y++)
        for (int x = 0; x < NX; x++) {
            int i = y * NX + x;
            q->trap[i] = 0.5 * OMEGA * OMEGA * ((x - NX / 2) * (x - NX / 2) + (y - NY / 2) * (y - NY / 2));
            double ex = x < 8 ? (8 - x) / 8.0 : x >= SCREEN ? (x - SCREEN + 1) / (double)(NX - SCREEN) : 0;
            double ey = y < 5 ? (5 - y) / 5.0 : y >= NY - 5 ? (y - NY + 6) / 5.0 : 0;
            q->mask[i] = (1 - 0.3 * ex * ex) * (1 - 0.2 * ey * ey);
        }
    sk_image_init(&q->buf, NX, NY);
    sk_image_init(&q->turned, NY, NX);
    build(q);
    blend(q, 0);
    fire(q, NY / 2, 1.1);
}

static void quantum_fini(sk_vmode *m)
{
    quantum *q = (quantum *)m;
    sk_wave_free(&q->wave);
    sk_image_free(&q->buf);
    sk_image_free(&q->turned);
    sk_vec_free(&q->dots);
    sk_vec_free(&q->flashes);
}

/* A grid rectangle filled, under the grid's transform, as a square of
 * the pane: the grid turned a quarter on a tall pane. */
static void grid_square(quantum *q, sk_vc *g, double x, double y, double w, double h, sk_color c, double a)
{
    if (q->turn)
        sk_vc_square(g, q->vsy * y + q->voy, q->vs * x + q->vox, q->vsy * h, q->vs * w, c, a);
    else
        sk_vc_square(g, q->vs * x + q->vox, q->vsy * y + q->voy, q->vs * w, q->vsy * h, c, a);
}

#define DOT_LIVE(d) (q->time - (d).t < 400)
#define FLASH_LIVE(d) (q->time - (d).t < 12)

static void quantum_draw(sk_vmode *m, sk_synth *s, double dt)
{
    quantum *q = (quantum *)m;
    int w = m->w, h = m->h;
    if (w < 4 || h < 4)
        return;
    const sk_synth_levels *L = &s->level;
    sk_vmode_events(m, s);
    for (size_t k = 0; k < m->ev.len; k++)
        react(q, &m->ev.ptr[k], L->bass);
    double f = SK_MIN(1, L->idle * 1.4);
    if (q->bar_top > 0) {
        double fade = sk_vm_exp(-SK_MIN(0.1, dt) / BAR_TAU);
        for (int i = 0; i < NX * NY; i++)
            q->bar[i] *= fade;
        q->bar_top *= fade;
        if (q->bar_top < 0.05) {
            memset(q->bar, 0, sizeof q->bar);
            q->bar_top = 0;
        }
        q->held = -1;
    }
    if (q->held < 0 || fabs(f - q->held) > 0.02 || (f == 0 && q->held != 0))
        blend(q, f);
    /* An orbital at rest: a coherent state set circling in the trap. */
    if (f > 0.6 && sk_wave_norm(&q->wave) < 0.3)
        sk_wave_packet(&q->wave, NX / 2 + 14, NY / 2, 1 / sqrt(2 * OMEGA), 1 / sqrt(2 * OMEGA), 0, OMEGA * 14, 1);
    q->clock += SK_MIN(0.1, dt) * (8 + 16 * SK_MIN(1, L->bass) + 8 * L->busy) * (1 - 0.4 * L->idle);
    int steps = 0;
    while (q->clock >= DT && steps < 4) {
        sk_wave_step(&q->wave);
        detect(q);
        sk_wave_damp(&q->wave, q->mask);
        q->clock -= DT;
        q->time += DT;
        steps++;
    }
    if (steps == 4)
        q->clock = 0;
    double n = sk_wave_norm(&q->wave);
    if (n > 3) {
        double k = sqrt(3 / n);
        for (int i = 0; i < NX * NY; i++)
            q->flat[i] = k;
        sk_wave_damp(&q->wave, q->flat);
    }

    double top = 1;
    for (int y = 0; y < NY; y++) {
        q->hist[y] *= 0.9993;
        top = SK_MAX(top, q->hist[y]);
    }
    const double *re = q->wave.re, *im = q->wave.im;
    for (int y = 0; y < NY; y++) {
        double hy = q->hist[y] / top;
        sk_px *row = sk_image_row(&q->buf, y);
        for (int x = 0; x < NX; x++) {
            int i = y * NX + x;
            double a = sqrt(re[i] * re[i] + im[i] * im[i]);
            double v = 1 - sk_vm_exp(-a / 0.03);
            double r = 0, gg = 0, b = 0;
            if (a > 1e-9) {
                double c = re[i] / a, sn = im[i] / a;
                r = v * (0.5 + 0.5 * c);
                gg = v * (0.5 - 0.25 * c + 0.433 * sn);
                b = v * (0.5 - 0.25 * c - 0.433 * sn);
            }
            double wl = q->wall[i] / V0 * (1 - f), tp = SK_MIN(1, q->trap[i] / 2) * f, br = q->bar[i] / BAR_H;
            r += 0.42 * wl + 0.55 * br;
            gg += 0.42 * wl + 0.05 * tp + 0.3 * br;
            b += 0.45 * wl + 0.22 * tp + 0.08 * br;
            if (x >= SCREEN) {
                r += 0.05 + 0.25 * hy;
                gg += 0.06 + 0.3 * hy;
                b += 0.05 + 0.15 * hy;
            }
            /* JavaScript's << truncates toward 0. */
            uint32_t cr = (uint32_t)sk_vm_clamp(r * 255, 0, 255), cg = (uint32_t)sk_vm_clamp(gg * 255, 0, 255);
            uint32_t cb = (uint32_t)sk_vm_clamp(b * 255, 0, 255);
            row[x] = 0xff000000u | cr << 16 | cg << 8 | cb;
        }
    }

    /* The grid on the pane: the long way along the pane's long side.
     * Cells stretched across the grid's short side up to 1.5 times, to
     * fill more of a squarish pane. */
    bool turn = SK_MIN((double)w / NY, (double)h / NX) > SK_MIN((double)w / NX, (double)h / NY);
    double W = turn ? h : w, H = turn ? w : h;
    double sc = SK_MIN(W / NX, H / NY), sy = SK_MIN(H / NY, 1.5 * sc);
    double ox = (W - NX * sc) / 2, oy = (H - NY * sy) / 2;
    q->have_view = true;
    q->turn = turn;
    q->vs = sc;
    q->vsy = sy;
    q->vox = ox;
    q->voy = oy;
    sk_vc *g = &m->vc;
    if (turn) {
        for (int y = 0; y < NX; y++) {
            sk_px *row = sk_image_row(&q->turned, y);
            for (int x = 0; x < NY; x++)
                row[x] = sk_image_row(&q->buf, x)[y];
        }
        sk_vc_set_transform(g, sy, 0, 0, sc, oy, ox);
        sk_vc_image(g, &q->turned, 0, 0, NY, NX, true);
    } else {
        sk_vc_set_transform(g, sc, 0, 0, sy, ox, oy);
        sk_vc_image(g, &q->buf, 0, 0, NX, NY, true);
    }
    if (turn)
        sk_vc_set_transform(g, 0, sc, sy, 0, oy, ox);
    sk_vc_save(g);
    sk_vc_begin(g);
    sk_vc_rect(g, 0, 0, NX, NY);
    sk_vc_clip(g, SK_FILL_NONZERO);
    sk_vc_reset_transform(g);
    SK_VIZ_KEEP(&q->dots, DOT_LIVE);
    for (size_t i = 0; i < q->dots.len; i++) {
        const dot *d = &q->dots.ptr[i];
        double a = SK_MAX(0, 1 - (q->time - d->t) / 400);
        if (q->time - d->t < 3)
            grid_square(q, g, d->x - 0.35, d->y - 0.35, 0.7, 0.7, SK_RGB(255, 255, 255), a);
        else
            grid_square(q, g, d->x - 0.35, d->y - 0.35, 0.7, 0.7, SK_RGB(170, 255, 200), 0.8 * a);
    }
    if (turn)
        sk_vc_set_transform(g, 0, sc, sy, 0, oy, ox);
    else
        sk_vc_set_transform(g, sc, 0, 0, sy, ox, oy);
    SK_VIZ_KEEP(&q->flashes, FLASH_LIVE);
    sk_vc_line_width(g, 0.4);
    for (size_t i = 0; i < q->flashes.len; i++) {
        const dot *fl = &q->flashes.ptr[i];
        double k = (q->time - fl->t) / 12;
        sk_vc_paint p = sk_vc_solid(sk_vc_rgba(255, 255, 255, 1 - k));
        sk_vc_begin(g);
        sk_vc_arc(g, fl->x, fl->y, 2 + 14 * k, 0, SK_TAU, false);
        sk_vc_stroke(g, &p);
    }
    sk_vc_restore(g);
    sk_vc_reset_transform(g);
}

/* Paint along the pointer's path while it is pressed, the gaps between
 * its reports filled in steps of half a point. */
static void quantum_pointer(sk_vmode *m, int kind, double x, double y, bool down)
{
    quantum *q = (quantum *)m;
    if (!down || !q->have_view) {
        q->pen = false;
        return;
    }
    double gx = q->turn ? (y - q->vox) / q->vs : (x - q->vox) / q->vs;
    double gy = q->turn ? (x - q->voy) / q->vsy : (y - q->voy) / q->vsy;
    if (kind == SK_VIZ_DOWN || !q->pen) {
        q->strokes++;
        paint_bar(q, gx, gy);
    } else {
        double dx = gx - q->pen_x, dy = gy - q->pen_y;
        int n = (int)SK_MAX(1, ceil(sqrt(dx * dx + dy * dy) / 0.5));
        for (int k = 1; k <= n; k++)
            paint_bar(q, q->pen_x + dx * k / n, q->pen_y + dy * k / n);
    }
    q->pen = true;
    q->pen_x = gx;
    q->pen_y = gy;
}

const sk_vmode_class sk_vmode_quantum = {sizeof(quantum), quantum_init, quantum_draw, quantum_pointer, quantum_fini};
