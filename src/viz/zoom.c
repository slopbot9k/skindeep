/* Zoom mode (decision 0015), app/viz/zoom.js: a camera that keeps
 * pulling back, each scene turning out to be a detail of a larger one,
 * R times its size: an atom is the heart of a snowflake, the snowflake
 * the glint in an eye's pupil, the eye the eyespot of a peacock's fan,
 * the fan a spot on a butterfly's wing, the butterfly a nebula in a
 * galaxy, and the galaxy the nucleus of an atom, where the loop closes.
 * Each scene has its surprise as it is revealed: the electrons light
 * up, the snowflake grows, the eye blinks, the fan opens, the wings
 * shimmer, a star goes supernova. The camera's depth z runs over [0, K)
 * and every frame depends only on z, so the loop is seamless. The bass
 * and the busy level speed the pull, a prompt surges it, an idle spell
 * all but stops it, and each event leaves a ring on the scene in view,
 * which recedes with it. A click leaves a white ring where it lands and
 * sets off the surprise of the scene in view. */
#include "internal.h"

#define R 4.0
#define K 6
#define MAX_MARKS 256

/* What a scene is drawn from: the time, the surprise's progress from 0
 * to 1 as the scene is revealed, an event's pulse from 1 down to 0, and
 * its hue. */
typedef struct {
    double t, u, p, hue;
} scene_state;

/* A bump from 0 up to 1 and back over [a, b]. */
static double bump(double x, double a, double b)
{
    return x <= a || x >= b ? 0 : sk_vm_sin(SK_PI * (x - a) / (b - a));
}

/* A soft disc of colour rgb, opaque to 0.6, clear at 1. */
static void backdrop(sk_vc *g, int r, int gr, int b)
{
    sk_vc_paint p = sk_vc_radial(0, 0, 0, 0, 0, 1);
    sk_vc_add_stop(&p, 0, SK_RGB(r, gr, b));
    sk_vc_add_stop(&p, 0.6, SK_RGB(r, gr, b));
    sk_vc_add_stop(&p, 1, SK_RGBA(r, gr, b, 0));
    sk_vc_fill_rect(g, -1, -1, 2, 2, &p);
}

static void glow(sk_vc *g, double x, double y, double rad, int r, int gr, int b, double a)
{
    sk_vc_paint p = sk_vc_radial(x, y, 0, x, y, rad);
    sk_vc_add_stop(&p, 0, sk_vc_rgba(r, gr, b, a));
    sk_vc_add_stop(&p, 1, SK_RGBA(r, gr, b, 0));
    sk_vc_fill_rect(g, x - rad, y - rad, 2 * rad, 2 * rad, &p);
}

static void fill_path(sk_vc *g, sk_color c)
{
    sk_vc_paint p = sk_vc_solid(c);
    sk_vc_fill(g, &p, SK_FILL_NONZERO);
}

static void stroke_path(sk_vc *g, sk_color c)
{
    sk_vc_paint p = sk_vc_solid(c);
    sk_vc_stroke(g, &p);
}

/* A fixed scatter of stars for the galaxy: two logarithmic arms. */
typedef struct {
    double x, y, s, ph;
    int c;
} star;

#define STARS 520
static const uint8_t STAR_RGB[3][3] = {{255, 225, 170}, {170, 200, 255}, {255, 170, 200}};

static void make_stars(star *out)
{
    int64_t seed = 7;
#define RND() (seed = (seed * 16807) % 2147483647, (double)seed / 2147483647)
    for (int k = 0; k < STARS; k++) {
        int arm = k % 2;
        double r = 0.06 + 0.9 * sk_vm_pow(RND(), 0.8);
        double th = arm * SK_PI + sk_vm_log(r) / 0.32;
        th += (RND() - 0.5) * 0.9 * (1 - r * 0.4);
        out[k].x = r * sk_vm_cos(th);
        out[k].y = r * sk_vm_sin(th) * 0.92;
        out[k].s = 0.004 + 0.008 * RND();
        out[k].c = r < 0.25 ? 0 : RND() < 0.75 ? 1 : 2;
        out[k].ph = RND() * SK_TAU;
    }
#undef RND
}

/* ---- The scenes, innermost first. Each draws in a disc of radius 1
 * and holds the scene before it at its anchor, shrunk by R. under()
 * draws beneath that inner scene, over() above it. ---- */

static void atom_under(sk_vc *g, const scene_state *s, const star *stars)
{
    (void)s;
    (void)stars;
    backdrop(g, 4, 6, 22);
    glow(g, 0, 0, 0.32, 255, 200, 140, 0.55);
}

static void atom_over(sk_vc *g, const scene_state *s)
{
    for (int i = 0; i < 3; i++) {
        sk_vc_save(g);
        sk_vc_rotate(g, i * SK_PI / 3);
        sk_vc_line_width(g, 0.012);
        sk_vc_begin(g);
        sk_vc_ellipse(g, 0, 0, 0.85, 0.28, 0, 0, SK_TAU * (0.15 + 0.85 * s->u), false);
        stroke_path(g, sk_vc_rgba(120, 200, 255, 0.25 + 0.4 * s->u));
        double a = s->t * (1.4 + 0.3 * i) + i * 2;
        double x = 0.85 * sk_vm_cos(a), y = 0.28 * sk_vm_sin(a);
        glow(g, x, y, 0.09 + 0.08 * s->p, 150, 230, 255, 0.5 + 0.5 * s->p);
        sk_color hot = s->p > 0 ? sk_vc_hsla(s->hue, 100, 70, 1) : SK_RGB(150, 230, 255);
        sk_vc_begin(g);
        sk_vc_arc(g, x, y, 0.03, 0, SK_TAU, false);
        fill_path(g, hot);
        /* A photon leaves an electron on an event. */
        if (s->p > 0) {
            sk_vc_line_width(g, 0.01);
            sk_vc_begin(g);
            double len = 0.6 * (1 - s->p), ca = sk_vm_cos(a), sa = sk_vm_sin(a);
            for (int k = 0; k <= 30; k++) {
                double d = k / 30.0 * len;
                double px = x + d * ca, py = y + d * sa + 0.025 * sk_vm_sin(k * 1.3 - s->t * 20);
                if (k)
                    sk_vc_line(g, px, py);
                else
                    sk_vc_move(g, px, py);
            }
            stroke_path(g, (hot & 0xffffffu) | (sk_vc_rgba(0, 0, 0, s->p) & 0xff000000u));
        }
        sk_vc_restore(g);
    }
}

static void snow_under(sk_vc *g, const scene_state *s, const star *stars)
{
    (void)stars;
    backdrop(g, 6, 18, 48);
    double grow = 0.35 + 0.65 * sk_viz_smooth(s->u);
    sk_color ice = sk_vc_rgba(225, 245, 255, 0.95);
    sk_vc_cap(g, SK_CAP_ROUND);
    static const double ats[] = {0.32, 0.47, 0.62, 0.77};
    for (int i = 0; i < 6; i++) {
        sk_vc_save(g);
        sk_vc_rotate(g, i * SK_PI / 3);
        sk_vc_line_width(g, 0.03);
        sk_vc_begin(g);
        sk_vc_move(g, 0.2, 0);
        sk_vc_line(g, 0.2 + 0.72 * grow, 0);
        for (int k = 0; k < 4; k++) {
            double at = ats[k];
            if (at > 0.2 + 0.72 * grow)
                continue;
            double len = SK_MIN(0.24 * (1.05 - at), 0.2 + 0.72 * grow - at);
            for (int sgn = -1; sgn <= 1; sgn += 2) {
                sk_vc_move(g, at, 0);
                sk_vc_line(g, at + len * 0.5, sgn * len * 0.866);
            }
        }
        stroke_path(g, ice);
        sk_vc_restore(g);
    }
    sk_vc_line_width(g, 0.02);
    sk_vc_begin(g);
    for (int i = 0; i <= 6; i++) {
        double a = i * SK_PI / 3;
        if (i)
            sk_vc_line(g, 0.2 * sk_vm_cos(a), 0.2 * sk_vm_sin(a));
        else
            sk_vc_move(g, 0.2, 0);
    }
    stroke_path(g, ice);
    /* Tips glint on an event. */
    if (s->p > 0) {
        sk_vc_paint p = sk_vc_solid(sk_vc_hsla(s->hue, 100, 75, s->p));
        for (int i = 0; i < 6; i++) {
            double a = i * SK_PI / 3, r = 0.2 + 0.72 * grow, ca = sk_vm_cos(a), sa = sk_vm_sin(a);
            sk_vc_fill_rect(g, r * ca - 0.04 * s->p, r * sa - 0.005, 0.08 * s->p, 0.01, &p);
            sk_vc_fill_rect(g, r * ca - 0.005, r * sa - 0.04 * s->p, 0.01, 0.08 * s->p, &p);
        }
    }
}

/* The almond of the eye, open from 0 to 1, added to the path. */
static void lids(sk_vc *g, double open)
{
    double h = 0.62 * SK_MAX(0.02, open);
    sk_vc_move(g, -0.95, 0);
    sk_vc_quad(g, 0, -1.3 * h, 0.95, 0);
    sk_vc_quad(g, 0, 1.3 * h, -0.95, 0);
    sk_vc_close(g);
}

static void eye_under(sk_vc *g, const scene_state *s, const star *stars)
{
    (void)stars;
    backdrop(g, 40, 22, 18);
    sk_vc_save(g);
    sk_vc_begin(g);
    lids(g, 1);
    sk_vc_clip(g, SK_FILL_NONZERO);
    sk_vc_paint p = sk_vc_solid(SK_RGB(0xef, 0xe9, 0xe0));
    sk_vc_fill_rect(g, -1, -1, 2, 2, &p);
    sk_vc_paint iris = sk_vc_radial(0, 0, 0.22, 0, 0, 0.44);
    sk_vc_add_stop(&iris, 0, s->p > 0 ? sk_vc_hsl(s->hue, 70, 45) : SK_RGB(0x3f, 0x7f, 0x5a));
    sk_vc_add_stop(&iris, 0.7, SK_RGB(0x5a, 0x8a, 0x3a));
    sk_vc_add_stop(&iris, 1, SK_RGB(0x1c, 0x2a, 0x16));
    sk_vc_begin(g);
    sk_vc_arc(g, 0, 0, 0.44, 0, SK_TAU, false);
    sk_vc_fill(g, &iris, SK_FILL_NONZERO);
    sk_vc_line_width(g, 0.008);
    sk_vc_begin(g);
    for (int k = 0; k < 48; k++) {
        double a = k / 48.0 * SK_TAU;
        sk_vc_move(g, 0.25 * sk_vm_cos(a), 0.25 * sk_vm_sin(a));
        sk_vc_line(g, 0.42 * sk_vm_cos(a + 0.05), 0.42 * sk_vm_sin(a + 0.05));
    }
    stroke_path(g, sk_vc_rgba(230, 210, 120, 0.35));
    sk_vc_begin(g);
    sk_vc_arc(g, 0, 0, 0.245, 0, SK_TAU, false);
    fill_path(g, SK_RGB(0x05, 0x06, 0x0a));
    sk_vc_restore(g);
}

static void eye_over(sk_vc *g, const scene_state *s)
{
    /* The lids: closed for a blink as the eye is revealed, and on an
     * event. */
    double shut = SK_MAX(bump(s->u, 0.55, 0.85), bump(1 - s->p, 0, 0.5));
    if (shut > 0.01) {
        sk_vc_save(g);
        sk_vc_begin(g);
        sk_vc_rect(g, -1, -1, 2, 2);
        lids(g, 1 - shut);
        sk_vc_clip(g, SK_FILL_EVENODD);
        sk_vc_begin(g);
        lids(g, 1);
        fill_path(g, SK_RGB(0x8a, 0x5a, 0x44));
        sk_vc_restore(g);
    }
    sk_vc_line_width(g, 0.025);
    sk_vc_begin(g);
    lids(g, 1 - shut);
    stroke_path(g, SK_RGB(0x2a, 0x14, 0x10));
}

/* An eyespot at (x, y), scaled by k. */
static void spot(sk_vc *g, double x, double y, double k, const scene_state *s)
{
    const double rings[4][2] = {{0.11, 0.14}, {0.085, 0.11}, {0.06, 0.08}, {0.035, 0.05}};
    sk_color cols[4] = {SK_RGB(0xb0, 0x8a, 0x2a), SK_RGB(0x2f, 0x9a, 0x5a),
                        s->p > 0 ? sk_vc_hsl(s->hue, 80, 50) : SK_RGB(0x2a, 0x5a, 0xd0), SK_RGB(0x0a, 0x16, 0x40)};
    for (int i = 0; i < 4; i++) {
        sk_vc_begin(g);
        sk_vc_ellipse(g, x, y, rings[i][0] * k, rings[i][1] * k, 0, 0, SK_TAU, false);
        fill_path(g, cols[i]);
    }
}

static void peacock_under(sk_vc *g, const scene_state *s, const star *stars)
{
    (void)stars;
    backdrop(g, 4, 26, 24);
    double spread = 0.12 + 0.88 * sk_viz_smooth(s->u);
    const int n = 21;
    for (int k = 0; k < n; k++) {
        double a = (k - (n - 1) / 2.0) / ((n - 1) / 2.0) * 1.35 * spread;
        double len = 0.95 - 0.12 * fabs(a);
        sk_vc_save(g);
        sk_vc_translate(g, 0, 0.9);
        sk_vc_rotate(g, a);
        sk_vc_line_width(g, 0.008);
        sk_vc_begin(g);
        sk_vc_move(g, 0, 0);
        sk_vc_line(g, 0, -len - 0.1);
        stroke_path(g, sk_vc_rgba(160, 190, 120, 0.6));
        sk_vc_begin(g);
        sk_vc_ellipse(g, 0, -len * 0.82, 0.075, len * 0.32, 0, 0, SK_TAU, false);
        fill_path(g, sk_vc_rgba(40, 120, 70, 0.55));
        if (k != (n - 1) / 2)
            spot(g, 0, -len, 1, s);
        if (k % 2 == 0)
            spot(g, 0, -len * 0.6, 0.6, s);
        sk_vc_restore(g);
    }
}

static void butterfly_under(sk_vc *g, const scene_state *s, const star *stars)
{
    (void)stars;
    backdrop(g, 24, 14, 34);
    double shine = bump(s->u, 0.4, 1) + s->p;
    for (int side = -1; side <= 1; side += 2) {
        sk_vc_save(g);
        sk_vc_scale(g, side, 1);
        /* The fore wing, then the hind wing. */
        sk_vc_begin(g);
        sk_vc_move(g, -0.04, -0.12);
        sk_vc_cubic(g, -0.3, -0.85, -0.9, -0.85, -0.95, -0.5);
        sk_vc_cubic(g, -0.95, -0.1, -0.5, 0.05, -0.04, 0.02);
        fill_path(g, sk_vc_rgba(150 + 60 * shine, 40 + 30 * shine, 30, 1));
        sk_vc_begin(g);
        sk_vc_move(g, -0.04, 0.02);
        sk_vc_cubic(g, -0.6, 0.0, -0.75, 0.55, -0.45, 0.72);
        sk_vc_cubic(g, -0.25, 0.8, -0.08, 0.5, -0.03, 0.15);
        fill_path(g, SK_RGB(0x5a, 0x24, 0x18));
        sk_vc_line_width(g, 0.02);
        sk_vc_begin(g);
        sk_vc_move(g, -0.04, -0.12);
        sk_vc_cubic(g, -0.3, -0.85, -0.9, -0.85, -0.95, -0.5);
        stroke_path(g, sk_vc_rgba(20, 8, 6, 0.8));
        sk_vc_paint dots = sk_vc_solid(sk_vc_rgba(240, 230, 220, 0.8));
        static const double at[3][2] = {{-0.82, -0.62}, {-0.88, -0.48}, {-0.7, -0.7}};
        for (int i = 0; i < 3; i++)
            sk_vc_fill_rect(g, at[i][0], at[i][1], 0.03, 0.03, &dots);
        sk_vc_restore(g);
    }
    /* The right wing's eyespot, the fan's twin in rings. */
    static const double rr[5] = {0.24, 0.2, 0.15, 0.09, 0.05};
    static const sk_color rc[5] = {SK_RGB(0x2a, 0x1a, 0x10), SK_RGB(0xc0, 0x8a, 0x3a), SK_RGB(0x2f, 0x8a, 0x6a),
                                   SK_RGB(0x30, 0x50, 0xc0), SK_RGB(0x08, 0x10, 0x28)};
    for (int i = 0; i < 5; i++) {
        sk_vc_begin(g);
        sk_vc_arc(g, 0.45, -0.3, rr[i], 0, SK_TAU, false);
        fill_path(g, rc[i]);
    }
    sk_vc_begin(g);
    sk_vc_ellipse(g, 0, 0.05, 0.045, 0.42, 0, 0, SK_TAU, false);
    fill_path(g, SK_RGB(0x1a, 0x0e, 0x0a));
    sk_vc_line_width(g, 0.015);
    double wave = 0.05 * sk_vm_sin(s->t * 3);
    sk_vc_begin(g);
    sk_vc_move(g, -0.01, -0.35);
    sk_vc_quad(g, -0.1, -0.6, -0.2 + wave, -0.75);
    sk_vc_move(g, 0.01, -0.35);
    sk_vc_quad(g, 0.1, -0.6, 0.2 - wave, -0.75);
    stroke_path(g, SK_RGB(0x1a, 0x0e, 0x0a));
}

static void galaxy_under(sk_vc *g, const scene_state *s, const star *stars)
{
    backdrop(g, 1, 1, 6);
    glow(g, 0, 0, 0.45, 255, 220, 170, 0.7);
    glow(g, 0.42, 0.22, 0.3, 255, 120, 170, 0.35);
    for (int c = 0; c < 3; c++) {
        sk_color col = SK_RGB(STAR_RGB[c][0], STAR_RGB[c][1], STAR_RGB[c][2]);
        for (int k = 0; k < STARS; k++) {
            const star *st = &stars[k];
            if (st->c != c)
                continue;
            double tw = 0.7 + 0.3 * sk_vm_sin(s->t * 2 + st->ph), z = st->s * tw;
            sk_vc_square(g, st->x - z / 2, st->y - z / 2, z, z, col, 0.9);
        }
    }
    /* A supernova as the galaxy is revealed, and on an event. */
    double nova = SK_MAX(bump(s->u, 0.5, 1), s->p);
    if (nova > 0.01) {
        double x = -0.38, y = -0.3;
        if (s->p > 0)
            glow(g, x, y, 0.25 * nova, 255, 255, 255, nova);
        else
            glow(g, x, y, 0.25 * nova, 255, 240, 200, nova);
        sk_vc_paint p = sk_vc_solid(sk_vc_rgba(255, 255, 255, nova));
        sk_vc_fill_rect(g, x - 0.3 * nova, y - 0.004, 0.6 * nova, 0.008, &p);
        sk_vc_fill_rect(g, x - 0.004, y - 0.3 * nova, 0.008, 0.6 * nova, &p);
    }
}

typedef struct {
    double anchor[2];
    void (*under)(sk_vc *g, const scene_state *s, const star *stars);
    void (*over)(sk_vc *g, const scene_state *s);
} scene;

static const scene SCENES[K] = {
    {{0, 0}, atom_under, atom_over},          {{0, 0}, snow_under, NULL},
    {{0, 0}, eye_under, eye_over},            {{0, -0.05}, peacock_under, NULL},
    {{-0.45, -0.3}, butterfly_under, NULL},   {{0.42, 0.22}, galaxy_under, NULL},
};

/* ---- The mode ---- */

typedef struct {
    int level;
    double x, y, hue, t;
    bool white;
} mark;

typedef struct {
    sk_vmode base;
    star stars[STARS];
    double z, surge, pulse, hue;
    SK_VEC(mark) marks;
    /* The scene in view as last drawn: its level, scale and origin on
     * the pane, the signal's clock then. */
    bool have_view;
    int view_level;
    double view_s, view_x, view_y, view_t;
    int clicks;
    bool round;
    /* The picture when the pane's pixels pass PICTURE: drawn at a
     * smaller scale and stretched over them, as fern.js draws its light
     * into a buffer of at most 160000 pixels. Every level is a full
     * scene of gradients and large fills, and the camera never stops. */
    sk_image pic;
    sk_vc pvc;
} zoom;

/* The most pixels the scenes are drawn in. */
#define PICTURE 480000.0

static int wrap(int k)
{
    return ((k % K) + K) % K;
}

static void zoom_init(sk_vmode *m)
{
    zoom *z = (zoom *)m;
    make_stars(z->stars);
    z->hue = 200;
}

static void zoom_fini(sk_vmode *m)
{
    zoom *zm = (zoom *)m;
    sk_vec_free(&zm->marks);
    sk_vc_free(&zm->pvc);
    sk_image_free(&zm->pic);
}

#define RECENT(mk) (t - (mk).t < 6)

static void zoom_draw(sk_vmode *m, sk_synth *s, double dt)
{
    zoom *zm = (zoom *)m;
    sk_vc *g = &m->vc;
    double w = m->w, h = m->h;
    if (w < 4 || h < 4)
        return;
    double scale = SK_MIN(m->scale, SK_MAX(1, sqrt(PICTURE / (w * h))));
    bool small = scale < m->scale;
    if (small) {
        int pw = (int)floor(w * scale + 0.5), ph = (int)floor(h * scale + 0.5);
        if (zm->pic.w != pw || zm->pic.h != ph) {
            sk_image_free(&zm->pic);
            sk_image_init(&zm->pic, pw, ph);
            if (!zm->pvc.path)
                sk_vc_init(&zm->pvc, &zm->pic, scale);
        }
        sk_vc_target(&zm->pvc, &zm->pic, scale);
        g = &zm->pvc;
    }
    const sk_synth_levels *L = &s->level;
    double t = s->t;
    int n = (int)floor(zm->z);
    double f = zm->z - n;
    sk_vmode_events(m, s);
    for (size_t k = 0; k < m->ev.len; k++) {
        const sk_viz_event *e = &m->ev.ptr[k];
        if (e->kind == SK_EV_PROMPT)
            zm->surge = 1;
        zm->hue = e->kind == SK_EV_TOOL     ? sk_viz_class_hue(e->cls)
                  : e->kind == SK_EV_PROMPT ? 50
                  : e->kind == SK_EV_AGENT  ? 300
                                            : 180;
        zm->pulse = 1;
        /* A ring on the scene in view. */
        mark mk = {n, (sk_vmode_rand(m) - 0.5) * 0.8, (sk_vmode_rand(m) - 0.5) * 0.8, zm->hue, t, false};
        if (zm->marks.len < MAX_MARKS)
            sk_vec_push(&zm->marks, mk);
    }
    SK_VIZ_KEEP(&zm->marks, RECENT);
    zm->surge *= sk_vm_exp(-dt / 2);
    zm->pulse = SK_MAX(0, zm->pulse - dt / 1.2);
    zm->z += dt * (0.05 + 0.12 * SK_MIN(1, L->bass) + 0.1 * L->busy + 0.35 * zm->surge) * (1 - 0.92 * L->idle);
    zm->z = fmod(zm->z, K);

    sk_vc_reset_transform(g);
    sk_vc_clear(g, SK_RGB(0, 0, 0));
    /* The canvas keeps its state from frame to frame: once the
     * snowflake has set round caps, every stroke after has them. */
    if (zm->round)
        sk_vc_cap(g, SK_CAP_ROUND);
    /* Each level's scale and offset on the pane: n + 1 is the
     * reference, the camera easing from n's anchor in it to its centre
     * as f runs from 0 to 1. at[k - n + 1] is level k's. */
    double base = 0.5 * SK_MIN(w, h) * 0.95, e = sk_viz_smooth(f);
    double S1 = base * sk_vm_pow(R, 1 - f);
    const double *c = SCENES[wrap(n + 1)].anchor, *c2 = SCENES[wrap(n + 2)].anchor, *a0 = SCENES[wrap(n)].anchor;
    double at[4][3];
    at[2][0] = S1;
    at[2][1] = w / 2 - S1 * c[0] * (1 - e);
    at[2][2] = h / 2 - S1 * c[1] * (1 - e);
    at[3][0] = S1 * R;
    at[3][1] = at[2][1] - S1 * R * c2[0];
    at[3][2] = at[2][2] - S1 * R * c2[1];
    at[1][0] = S1 / R;
    at[1][1] = at[2][1] + S1 * c[0];
    at[1][2] = at[2][2] + S1 * c[1];
    zm->have_view = true;
    zm->view_level = n;
    zm->view_s = at[1][0];
    zm->view_x = at[1][1];
    zm->view_y = at[1][2];
    zm->view_t = t;
    at[0][0] = S1 / R / R;
    at[0][1] = at[1][1] + at[1][0] * a0[0];
    at[0][2] = at[1][2] + at[1][0] * a0[1];
    double alpha[4] = {1 - f, 1, 1, f};
    /* The levels to draw, n + 2 down to n - 1. */
    int levels[4], nl = 0;
    for (int k = 3; k >= 0; k--)
        if (alpha[k] > 0.01 && at[k][0] > 2)
            levels[nl++] = k;
    /* Level n + 1's backdrop is opaque out to 0.6 of its radius, and
     * nothing level n + 2 draws beneath that disc shows: its scene is
     * drawn only in the box of each corner of the pane that the disc
     * leaves, and not at all where the disc holds the pane. A scene
     * magnified fourfold or more fills the pane many times over. */
    double ox = at[2][1], oy = at[2][2], hr = 0.6 * at[2][0] - 1;
    double boxes[4][4];
    int n_boxes = 0;
    for (int corner = 0; corner < 4; corner++) {
        /* The quadrant from the disc's centre toward this corner, within
         * the pane, then the part of it the disc leaves. */
        bool right = corner & 1, down = corner & 2;
        double x0 = right ? sk_vm_clamp(ox, 0, w) : 0, x1 = right ? w : sk_vm_clamp(ox, 0, w);
        double y0 = down ? sk_vm_clamp(oy, 0, h) : 0, y1 = down ? h : sk_vm_clamp(oy, 0, h);
        if (x1 <= x0 || y1 <= y0)
            continue;
        double ex = right ? x1 : x0, ey = down ? y1 : y0;
        if (hr > 0 && (ex - ox) * (ex - ox) + (ey - oy) * (ey - oy) < hr * hr)
            continue;
        /* Along the pane's edges the disc leaves x beyond its chord at
         * the edge's y, and y beyond its chord at the edge's x. */
        if (hr > 0) {
            double dy = ey - oy, dx = ex - ox;
            if (hr > fabs(dy)) {
                double ch = sqrt(hr * hr - dy * dy);
                if (right)
                    x0 = SK_MAX(x0, ox + ch);
                else
                    x1 = SK_MIN(x1, ox - ch);
            }
            if (hr > fabs(dx)) {
                double ch = sqrt(hr * hr - dx * dx);
                if (down)
                    y0 = SK_MAX(y0, oy + ch);
                else
                    y1 = SK_MIN(y1, oy - ch);
            }
        }
        if (x1 <= x0 || y1 <= y0)
            continue;
        boxes[n_boxes][0] = floor(x0);
        boxes[n_boxes][1] = floor(y0);
        boxes[n_boxes][2] = ceil(x1);
        boxes[n_boxes][3] = ceil(y1);
        n_boxes++;
    }
    for (int pass = 0; pass < 2; pass++)
        for (int li = 0; li < nl; li++) {
            int k = levels[pass ? nl - 1 - li : li], role = k - 1, lv = n + role;
            const scene *sc = &SCENES[wrap(lv)];
            scene_state st = {t, role <= 0 ? 1 : role == 1 ? f : 0, role == 0 ? zm->pulse : 0, zm->hue};
            sk_vc_set_transform(g, at[k][0], 0, 0, at[k][0], at[k][1], at[k][2]);
            sk_vc_alpha(g, alpha[k]);
            if (!pass) {
                double area = 0;
                for (int b = 0; b < n_boxes; b++)
                    area += (boxes[b][2] - boxes[b][0]) * (boxes[b][3] - boxes[b][1]);
                if (k != 3 || area > 0.6 * w * h) {
                    sc->under(g, &st, zm->stars);
                    continue;
                }
                for (int b = 0; b < n_boxes; b++) {
                    sk_vc_save(g);
                    sk_vc_reset_transform(g);
                    sk_vc_begin(g);
                    sk_vc_rect(g, boxes[b][0], boxes[b][1], boxes[b][2] - boxes[b][0], boxes[b][3] - boxes[b][1]);
                    sk_vc_clip(g, SK_FILL_NONZERO);
                    sk_vc_set_transform(g, at[k][0], 0, 0, at[k][0], at[k][1], at[k][2]);
                    sc->under(g, &st, zm->stars);
                    sk_vc_restore(g);
                }
                continue;
            }
            if (sc->over)
                sc->over(g, &st);
            sk_vc_line_width(g, 0.012);
            for (size_t i = 0; i < zm->marks.len; i++) {
                const mark *mk = &zm->marks.ptr[i];
                if (wrap(mk->level - lv) != 0)
                    continue;
                double age = (t - mk->t) / 6;
                sk_vc_begin(g);
                sk_vc_arc(g, mk->x, mk->y, 0.04 + 0.3 * age, 0, SK_TAU, false);
                stroke_path(g, mk->white ? sk_vc_rgba(255, 255, 255, 1 - age) : sk_vc_hsla(mk->hue, 100, 65, 1 - age));
            }
        }
    sk_vc_alpha(g, 1);
    zm->round = zm->round || g->st.cap == SK_CAP_ROUND;
    sk_vc_reset_transform(g);
    if (small)
        sk_blit_scaled(&m->canvas, sk_image_bounds(&m->canvas), &zm->pic, sk_image_bounds(&zm->pic), SK_FILTER_BILINEAR,
                       SK_BLEND_SRC, 255, NULL);
}

static void zoom_pointer(sk_vmode *m, int kind, double x, double y, bool down)
{
    (void)down;
    zoom *zm = (zoom *)m;
    if (kind != SK_VIZ_DOWN || !zm->have_view)
        return;
    mark mk = {zm->view_level, (x - zm->view_x) / zm->view_s, (y - zm->view_y) / zm->view_s, 0, zm->view_t, true};
    if (zm->marks.len < MAX_MARKS)
        sk_vec_push(&zm->marks, mk);
    zm->pulse = 1;
    zm->clicks++;
}

const sk_vmode_class sk_vmode_zoom = {sizeof(zoom), zoom_init, zoom_draw, zoom_pointer, zoom_fini};
