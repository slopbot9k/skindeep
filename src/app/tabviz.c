#include "tabviz.h"

#include "../gfx/draw.h"
#include "../viz/canvas.h"
#include "../viz/vmath.h"

#include <math.h>
#include <string.h>

#define W SK_TABVIZ_W
#define H SK_TABVIZ_H
/* Quiet this long, and not busy: done. */
#define DONE_S 2.0
/* A column below this height is drawn grey. */
#define QUIET 0.15f

enum { BASS, TREBLE, BUSY, LEVELS };

/* The scope's colours: a tool call's by its class, another event's by
 * its kind (bus.h's SK_EV_). */
static const double CLASS_HUE[] = {200, 120, 40, 0, 280, 320};

static double kind_hue(int kind)
{
    switch (kind) {
    case SK_EV_OUTPUT:
        return 160;
    case SK_EV_PROMPT:
        return 60;
    case SK_EV_STOP:
        return 190;
    case SK_EV_AGENT:
        return 300;
    case SK_EV_COMPACT:
        return 0;
    default:
        return 160;
    }
}

typedef struct {
    int id;
    double level[LEVELS], target[LEVELS];
    double done, done_target;
    /* The columns, the oldest first, as tabviz.js's Float32Arrays. */
    float amp[W], hue[W], lit[W];
    /* The open column. */
    double cur_amp, cur_hue, cur_lit;
    double flash;
    bool settled; /* drawn settled: the flat line alone */
    bool painted; /* a column has closed: there is a picture */
    bool dirty;   /* the picture changed since it was rendered */
    sk_image img;
    double img_scale;
} band;

struct sk_tabviz {
    SK_VEC(band *) bands;
    double acc; /* ms into the open column */
    sk_rng rng;
    sk_vc vc;
    sk_image none;
};

sk_tabviz *sk_tabviz_new(uint64_t seed)
{
    sk_tabviz *t = sk_xcalloc(1, sizeof *t);
    sk_rng_seed(&t->rng, seed);
    sk_image_init(&t->none, 1, 1);
    sk_vc_init(&t->vc, &t->none, 1);
    return t;
}

static void band_free(band *b)
{
    sk_image_free(&b->img);
    free(b);
}

void sk_tabviz_free(sk_tabviz *t)
{
    if (!t)
        return;
    for (size_t i = 0; i < t->bands.len; i++)
        band_free(t->bands.ptr[i]);
    sk_vec_free(&t->bands);
    sk_vc_free(&t->vc);
    sk_image_free(&t->none);
    free(t);
}

static band *band_of(const sk_tabviz *t, int id)
{
    for (size_t i = 0; i < t->bands.len; i++)
        if (t->bands.ptr[i]->id == id)
            return t->bands.ptr[i];
    return NULL;
}

void sk_tabviz_keep(sk_tabviz *t, const int *ids, size_t n)
{
    size_t k = 0;
    for (size_t i = 0; i < t->bands.len; i++) {
        bool kept = false;
        for (size_t j = 0; j < n && !kept; j++)
            kept = ids[j] == t->bands.ptr[i]->id;
        if (kept)
            t->bands.ptr[k++] = t->bands.ptr[i];
        else
            band_free(t->bands.ptr[i]);
    }
    t->bands.len = k;
    for (size_t j = 0; j < n; j++) {
        if (band_of(t, ids[j]))
            continue;
        band *b = sk_xcalloc(1, sizeof *b);
        b->id = ids[j];
        b->cur_hue = 150;
        sk_vec_push(&t->bands, b);
    }
}

/* tabviz.js's feed(): the targets as the synthesiser's input sets them;
 * with no busy state, a token rate over its 10 s window counts as work
 * too. */
void sk_tabviz_feed(sk_tabviz *t, const sk_vizfeed_msg *m)
{
    for (size_t k = 0; k < m->frame.n_panes; k++) {
        const sk_viz_pane_frame *f = &m->frame.panes[k];
        band *b = band_of(t, f->tab);
        if (!b)
            continue;
        double out_bps = f->n[SK_VIZ_OUT_BPS], in_kps = f->n[SK_VIZ_IN_KPS], rate = f->n[SK_VIZ_OUT_RATE],
               busy = f->n[SK_VIZ_BUSY];
        b->target[BASS] = SK_MIN(1, out_bps / 2500) + SK_MIN(0.3, in_kps / 30);
        b->target[TREBLE] = SK_MIN(1, rate / 120);
        b->target[BUSY] = busy == 1 ? 1 : 0;
        bool no_busy = m->no_busy && m->no_busy[k];
        double quiet = m->quiet ? m->quiet[k] : 0;
        b->done_target = busy != 1 && quiet >= DONE_S && !(no_busy && rate > 0) ? 1 : 0;
        for (size_t i = 0; i + 1 < f->n_events; i += 2) {
            int kind = f->events[i], cls = f->events[i + 1];
            b->cur_hue = kind == SK_EV_TOOL ? (cls >= 0 && cls < (int)SK_LEN(CLASS_HUE) ? CLASS_HUE[cls] : 200)
                                            : kind_hue(kind);
            b->cur_lit = 1;
            b->flash = 1;
        }
    }
}

/* Advance by dt seconds: ease the levels and add to the open column. */
static void step_band(sk_tabviz *t, band *b, double dt)
{
    double k = 1 - sk_vm_exp(-dt * 6);
    for (int i = 0; i < LEVELS; i++)
        b->level[i] += (b->target[i] - b->level[i]) * k;
    b->done += (b->done_target - b->done) * (1 - sk_vm_exp(-dt * 5));
    b->flash *= sk_vm_exp(-dt * 6);
    double quiet = 1 - b->done;
    double r1 = sk_rng_next(&t->rng), r2 = sk_rng_next(&t->rng);
    double a = 0.08 + quiet * (0.7 * SK_MIN(1, b->level[BASS]) + 0.3 * b->level[TREBLE] * r1 +
                               b->level[BUSY] * (0.2 + 0.35 * r2) + 0.6 * b->flash);
    b->cur_amp = SK_MAX(b->cur_amp, SK_MIN(1, a));
    if (!b->cur_lit)
        b->cur_hue = 150 - 110 * b->level[TREBLE] + 40 * b->level[BUSY];
}

/* Close the open column, the oldest scrolling out. */
static void close_column(band *b)
{
    memmove(b->amp, b->amp + 1, (W - 1) * sizeof *b->amp);
    memmove(b->hue, b->hue + 1, (W - 1) * sizeof *b->hue);
    memmove(b->lit, b->lit + 1, (W - 1) * sizeof *b->lit);
    b->amp[W - 1] = (float)b->cur_amp;
    b->hue[W - 1] = (float)b->cur_hue;
    b->lit[W - 1] = (float)b->cur_lit;
    b->cur_amp = 0;
    b->cur_lit = 0;
}

/* Done, and every column quiet: the picture is the flat line alone. */
static bool settled(const band *b)
{
    if (!(b->done > 0.99))
        return false;
    for (int x = 0; x < W; x++)
        if (!(b->amp[x] < QUIET) || b->lit[x])
            return false;
    return true;
}

bool sk_tabviz_step(sk_tabviz *t, double dt)
{
    if (!(dt >= 0))
        dt = 0;
    dt = SK_MIN(0.1, dt);
    for (size_t i = 0; i < t->bands.len; i++)
        step_band(t, t->bands.ptr[i], dt);
    t->acc += dt * 1000;
    if (t->acc < SK_TABVIZ_COL_MS)
        return false;
    t->acc = fmod(t->acc, SK_TABVIZ_COL_MS);
    bool changed = false;
    for (size_t i = 0; i < t->bands.len; i++) {
        band *b = t->bands.ptr[i];
        close_column(b);
        bool still = settled(b);
        if (!(still && b->settled) || !b->painted) {
            b->dirty = true;
            changed = true;
        }
        b->settled = still;
        b->painted = true;
    }
    return changed;
}

/* tabviz.js's draw(): the band at scale into its own picture. */
static void render(sk_tabviz *t, band *b, double scale)
{
    int pw = (int)lround(W * scale), ph = (int)lround(H * scale);
    if (b->img.w != pw || b->img.h != ph) {
        sk_image_free(&b->img);
        sk_image_init(&b->img, pw, ph);
    }
    sk_vc *vc = &t->vc;
    sk_vc_target(vc, &b->img, scale);
    sk_vc_clear(vc, 0);
    b->img_scale = scale;
    b->dirty = false;
    if (!b->painted)
        return;
    double mid = H / 2.0, fade = 1 - 0.75 * b->done;
    for (int x = 0; x < W; x++) {
        double amp = b->amp[x], lit = b->lit[x];
        double h = SK_MAX(0.5, amp * (mid - 1));
        /* A quiet column is grey, so colour means activity and green
         * alone means done. */
        bool quiet = amp < QUIET && !lit;
        sk_vc_paint p =
            sk_vc_solid(sk_vc_hsla(b->hue[x], quiet ? 0 : 90 - 70 * b->done, quiet ? 38 : 48 + 22 * lit, fade));
        sk_vc_fill_rect(vc, x, mid - h, 1, 2 * h, &p);
    }
    if (b->done > 0.02) {
        sk_vc_paint p = sk_vc_solid(sk_vc_rgba(143, 209, 143, b->done));
        sk_vc_fill_rect(vc, 0, mid - 1, W, 2, &p);
    }
}

static uint32_t mul(uint32_t x, uint32_t y)
{
    uint32_t t = x * y + 128;
    return (t + (t >> 8)) >> 8;
}

void sk_tabviz_draw(sk_tabviz *t, int id, sk_image *dst, sk_rect r, double scale, const sk_rect *clip)
{
    band *b = band_of(t, id);
    if (!b || r.w <= 0 || r.h <= 0)
        return;
    if (!(scale > 0 && scale <= 8))
        scale = 1;
    if (b->dirty || b->img_scale != scale || !b->img.px)
        render(t, b, scale);
    /* The canvas on its #000 box, stretched to the box as the strip
     * narrows it, the box's corners rounded. */
    sk_image box;
    sk_image_init(&box, r.w, r.h);
    sk_fill_rect(&box, sk_image_bounds(&box), SK_RGB(0, 0, 0), SK_BLEND_SRC, NULL);
    if (b->img.w == r.w && b->img.h == r.h)
        sk_blit(&box, 0, 0, &b->img, sk_image_bounds(&b->img), SK_BLEND_SRC_OVER, 255, NULL);
    else
        sk_blit_scaled(&box, sk_image_bounds(&box), &b->img, sk_image_bounds(&b->img), SK_FILTER_BILINEAR,
                       SK_BLEND_SRC_OVER, 255, NULL);
    sk_path *path = sk_path_new();
    sk_path_round_rect(path, 0, 0, (float)r.w, (float)r.h, (float)(SK_TABVIZ_RADIUS * scale));
    uint8_t *mask = sk_path_mask(path, NULL, SK_FILL_NONZERO, r.w, r.h);
    sk_path_free(path);
    sk_rect a = sk_rect_intersect(r, sk_image_bounds(dst));
    if (clip)
        a = sk_rect_intersect(a, *clip);
    for (int y = a.y; mask && y < a.y + a.h; y++) {
        sk_px *d = sk_image_row(dst, y);
        const sk_px *s = sk_image_row(&box, y - r.y);
        const uint8_t *m = mask + (size_t)(y - r.y) * (size_t)r.w;
        for (int x = a.x; x < a.x + a.w; x++) {
            uint32_t k = m[x - r.x];
            if (!k)
                continue;
            sk_px sp = s[x - r.x], dp = d[x];
            if (k == 255) {
                d[x] = sp;
                continue;
            }
            uint32_t out = 0;
            for (int c = 0; c < 32; c += 8)
                out |= (mul((sp >> c) & 255, k) + mul((dp >> c) & 255, 255 - k)) << c;
            d[x] = out;
        }
    }
    free(mask);
    sk_image_free(&box);
}

double sk_tabviz_done(const sk_tabviz *t, int id)
{
    const band *b = band_of(t, id);
    return b ? b->done : -1;
}

bool sk_tabviz_column(const sk_tabviz *t, int id, int x, double *amp, double *hue, bool *lit)
{
    const band *b = band_of(t, id);
    if (!b || x < 0 || x >= W)
        return false;
    *amp = b->amp[x];
    *hue = b->hue[x];
    *lit = b->lit[x] != 0;
    return true;
}
