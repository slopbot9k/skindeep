/* The classic analyzer (decision 0015), app/viz/classic.js: drawn from
 * the look of the spectrum analyzer in the main window of the classic
 * desktop MP3 player, 19 thick bars on a black panel of square pixels
 * with a dotted grid, bars that rise at once and sink slowly, and a peak
 * cap over each bar that holds, then falls faster and faster.
 * analyzer: a pixel's colour is its height on the panel, green at the
 * foot to red at the top. fire: the colour runs down from each bar's
 * top. */
#include "internal.h"

/* The original panel: 76 pixels across, each bar 3 and a gap of 1. */
#define COLS 76
#define BARS 19
#define MAX_ROWS 4096

/* Top to foot: red, orange, amber, yellow-green, green, deep green. */
static const int STOPS[6][3] = {{238, 56, 24}, {230, 112, 10}, {224, 170, 28}, {158, 212, 38}, {52, 196, 24}, {24, 132, 10}};
#define PEAK SK_RGB(176, 180, 186)
#define GRID SK_RGB(34, 38, 46)

typedef struct {
    sk_vmode base;
    bool fire;
    float bars[BARS], peaks[BARS], fall[BARS], hold[BARS];
    sk_color *colours;
    int n_colours;
} classic;

/* t from 0, the top colour, to 1, the foot colour. */
static sk_color shade(double t)
{
    double x = sk_vm_clamp(t, 0, 1) * 5;
    int i = SK_MIN(4, (int)floor(x));
    double f = x - i;
    int c[3];
    for (int k = 0; k < 3; k++)
        c[k] = (int)sk_vm_round(STOPS[i][k] + (STOPS[i + 1][k] - STOPS[i][k]) * f);
    return SK_RGB(c[0], c[1], c[2]);
}

static double level(const sk_synth *s, int b)
{
    int n = SK_SYNTH_BANDS;
    int lo = b * n / BARS, hi = SK_MAX(lo + 1, (b + 1) * n / BARS);
    double m = 0;
    for (int k = lo; k < hi; k++)
        m = SK_MAX(m, s->bands[k]);
    return SK_MIN(1, sk_vm_log10(1 + 30 * m) / 1.2);
}

static void draw(sk_vmode *m, sk_synth *s, double dt)
{
    classic *c = (classic *)m;
    sk_vc *g = &m->vc;
    double w = m->w, h = m->h;
    /* Square pixels: as many rows as the pane's shape allows, at least
     * the original 16. */
    int rows = (int)SK_MIN(MAX_ROWS, SK_MAX(16, sk_vm_round(COLS * h / SK_MAX(1, w))));
    double px = SK_MIN(w / COLS, h / rows);
    double ox = floor((w - px * COLS) / 2), oy = floor((h - px * rows) / 2);
    double gap = px >= 4 ? 1 : 0;
    if (c->n_colours != rows) {
        c->colours = sk_xreallocarray(c->colours, (size_t)rows, sizeof *c->colours);
        for (int r = 0; r < rows; r++)
            c->colours[r] = shade((double)r / (rows - 1));
        c->n_colours = rows;
    }

    for (int b = 0; b < BARS; b++) {
        float v = (float)level(s, b);
        c->bars[b] = v >= c->bars[b] ? v : (float)SK_MAX(v, c->bars[b] - dt * 1.4);
        if (c->bars[b] >= c->peaks[b]) {
            c->peaks[b] = c->bars[b];
            c->fall[b] = 0;
            c->hold[b] = 0.4f;
        } else if (c->hold[b] > 0) {
            c->hold[b] -= (float)dt;
        } else {
            c->fall[b] += (float)(dt * 2.2);
            c->peaks[b] = (float)SK_MAX(c->bars[b], c->peaks[b] - c->fall[b] * dt);
        }
    }

    sk_vc_clear(g, SK_RGB(0, 0, 0));
    double dot = SK_MAX(1, sk_vm_round(px * 0.3));
    for (int r = 1; r < rows; r += 2)
        for (int col = 1; col < COLS; col += 2)
            sk_vc_square(g, ox + col * px, oy + r * px, dot, dot, GRID, 1);

    for (int b = 0; b < BARS; b++) {
        double x0 = ox + b * 4 * px;
        int n = (int)sk_vm_round(c->bars[b] * rows);
        for (int k = 0; k < n; k++) {
            /* k counts up from the foot; colours[0] is the top row. */
            int row = rows - 1 - k;
            sk_color col = c->fire ? c->colours[SK_MIN(rows - 1, n - 1 - k)] : c->colours[row];
            for (int i = 0; i < 3; i++)
                sk_vc_square(g, x0 + i * px, oy + row * px, px - gap, px - gap, col, 1);
        }
        int p = (int)sk_vm_round(c->peaks[b] * rows);
        if (p > 0) {
            int row = rows - p;
            for (int i = 0; i < 3; i++)
                sk_vc_square(g, x0 + i * px, oy + row * px, px - gap, px - gap, PEAK, 1);
        }
    }
}

static void init_fire(sk_vmode *m)
{
    ((classic *)m)->fire = true;
}

static void fini(sk_vmode *m)
{
    free(((classic *)m)->colours);
}

const sk_vmode_class sk_vmode_analyzer = {sizeof(classic), NULL, draw, NULL, fini};
const sk_vmode_class sk_vmode_fire = {sizeof(classic), init_fire, draw, NULL, fini};
