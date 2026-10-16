/* Paths (draw.h): building them, flattening their curves to edges, and
 * filling the edges with exact-area coverage. */
#include "internal.h"

#include <string.h>

/* ---- Building ---- */

/* 4/3 (sqrt 2 - 1): a cubic through a quarter circle of radius 1 puts
 * its control points this far along the tangents. */
#define KAPPA 0.5522847498307936

sk_path *sk_path_new(void)
{
    return sk_xcalloc(1, sizeof(sk_path));
}

void sk_path_free(sk_path *p)
{
    if (!p)
        return;
    sk_vec_free(&p->verbs);
    sk_vec_free(&p->pts);
    free(p);
}

void sk_path_clear(sk_path *p)
{
    p->verbs.len = p->pts.len = 0;
    p->open = p->closed = false;
}

static void put(sk_path *p, float x, float y)
{
    sk_vec_push(&p->pts, x);
    sk_vec_push(&p->pts, y);
}

void sk_path_move(sk_path *p, float x, float y)
{
    /* A move after a move only moves the start. */
    if (p->open && p->verbs.ptr[p->verbs.len - 1] == SK_PATH_MOVE) {
        p->pts.ptr[p->pts.len - 2] = x;
        p->pts.ptr[p->pts.len - 1] = y;
    } else {
        sk_vec_push(&p->verbs, SK_PATH_MOVE);
        put(p, x, y);
    }
    p->sx = x;
    p->sy = y;
    p->open = true;
    p->closed = false;
}

/* Before a line or a curve, as canvas has it: after a close the next
 * subpath starts where the closed one did; with no point at all, the
 * first point given is the start. */
static void begin(sk_path *p, float x, float y)
{
    if (!p->open)
        sk_path_move(p, p->closed ? p->sx : x, p->closed ? p->sy : y);
}

void sk_path_line(sk_path *p, float x, float y)
{
    begin(p, x, y);
    sk_vec_push(&p->verbs, SK_PATH_LINE);
    put(p, x, y);
}

void sk_path_quad(sk_path *p, float cx, float cy, float x, float y)
{
    begin(p, cx, cy);
    sk_vec_push(&p->verbs, SK_PATH_QUAD);
    put(p, cx, cy);
    put(p, x, y);
}

void sk_path_cubic(sk_path *p, float c1x, float c1y, float c2x, float c2y, float x, float y)
{
    begin(p, c1x, c1y);
    sk_vec_push(&p->verbs, SK_PATH_CUBIC);
    put(p, c1x, c1y);
    put(p, c2x, c2y);
    put(p, x, y);
}

void sk_path_close(sk_path *p)
{
    if (!p->open)
        return;
    sk_vec_push(&p->verbs, SK_PATH_CLOSE);
    p->open = false;
    p->closed = true;
}

void sk_path_rect(sk_path *p, float x, float y, float w, float h)
{
    sk_path_move(p, x, y);
    sk_path_line(p, x + w, y);
    sk_path_line(p, x + w, y + h);
    sk_path_line(p, x, y + h);
    sk_path_close(p);
}

/* A quarter of an ellipse from (x0, y0) to (x1, y1), its corner at (cx,
 * cy): the control points lie along the sides that meet there. */
static void quarter(sk_path *p, double x0, double y0, double cx, double cy, double x1, double y1)
{
    sk_path_cubic(p, (float)(x0 + (cx - x0) * KAPPA), (float)(y0 + (cy - y0) * KAPPA),
                  (float)(x1 + (cx - x1) * KAPPA), (float)(y1 + (cy - y1) * KAPPA), (float)x1, (float)y1);
}

void sk_path_round_rect(sk_path *p, float x, float y, float w, float h, float radius)
{
    double r = SK_MIN(fabs((double)w), fabs((double)h)) / 2;
    if (radius < r)
        r = radius;
    if (!(r > 0) || !(radius > 0)) {
        sk_path_rect(p, x, y, w, h);
        return;
    }
    /* A negative size draws the mirror image, as sk_path_rect does. */
    double rx = w < 0 ? -r : r, ry = h < 0 ? -r : r;
    double l = x, t = y, rt = (double)x + w, b = (double)y + h;
    sk_path_move(p, (float)(l + rx), (float)t);
    sk_path_line(p, (float)(rt - rx), (float)t);
    quarter(p, rt - rx, t, rt, t, rt, t + ry);
    sk_path_line(p, (float)rt, (float)(b - ry));
    quarter(p, rt, b - ry, rt, b, rt - rx, b);
    sk_path_line(p, (float)(l + rx), (float)b);
    quarter(p, l + rx, b, l, b, l, b - ry);
    sk_path_line(p, (float)l, (float)(t + ry));
    quarter(p, l, t + ry, l, t, l + rx, t);
    sk_path_close(p);
}

void sk_path_ellipse(sk_path *p, float x, float y, float w, float h)
{
    double rx = (double)w / 2, ry = (double)h / 2, cx = x + rx, cy = y + ry;
    sk_path_move(p, (float)(cx + rx), (float)cy);
    quarter(p, cx + rx, cy, cx + rx, cy + ry, cx, cy + ry);
    quarter(p, cx, cy + ry, cx - rx, cy + ry, cx - rx, cy);
    quarter(p, cx - rx, cy, cx - rx, cy - ry, cx, cy - ry);
    quarter(p, cx, cy - ry, cx + rx, cy - ry, cx + rx, cy);
    sk_path_close(p);
}

/* ---- Flattening ---- */

/* Coverage is worked out at 1/SUB of a pixel, in integers. */
#define SUB 256
/* Edges are cut to this many pixels either side of the origin, so that
 * in SUBths a coordinate fits 31 bits and a product of two 63. */
#define LIM 4194304.0

/* A line of the flattened path in SUBths, y0 < y1, and which way the
 * path ran along it: rounded once, it is the same line whatever part of
 * it a band or a clip asks for. */
typedef struct {
    int32_t x0, y0, x1, y1, dir;
} edge;

typedef struct {
    SK_VEC(edge) edges;
    /* The pixels drawn: an edge wholly above, below or right of them
     * changes none, and is dropped. */
    double l, t, r, b;
} flat;

/* v in SUBths, rounded; v is within LIM. */
static int32_t fix(double v)
{
    return (int32_t)floor(v * SUB + 0.5);
}

static void put_edge(flat *f, double x0, double y0, double x1, double y1, int dir)
{
    edge e = {fix(x0), fix(y0), fix(x1), fix(y1), dir};
    if (e.y0 < e.y1)
        sk_vec_push(&f->edges, e);
}

/* Where the line through (x0, y0) and (x1, y1), which differ in x, meets
 * x, kept between y0 and y1. */
static double y_at(double x0, double y0, double x1, double y1, double x)
{
    double y = y0 + (x - x0) * (y1 - y0) / (x1 - x0);
    return y < y0 ? y0 : y > y1 ? y1 : y;
}

static void add_edge(flat *f, double x0, double y0, double x1, double y1)
{
    int dir = 1;
    if (y0 == y1)
        return;
    if (y0 > y1) {
        double t = x0;
        x0 = x1;
        x1 = t;
        t = y0;
        y0 = y1;
        y1 = t;
        dir = -1;
    }
    if (y1 <= f->t || y0 >= f->b || (x0 >= f->r && x1 >= f->r))
        return;
    /* Cut to LIM. Above and below it nothing is drawn; right of it an
     * edge counts for nothing; left of it, it still counts for every
     * pixel to its right, so it moves onto the limit. */
    if (y0 < -LIM) {
        x0 = x0 + (-LIM - y0) * (x1 - x0) / (y1 - y0);
        y0 = -LIM;
    }
    if (y1 > LIM) {
        x1 = x1 - (y1 - LIM) * (x1 - x0) / (y1 - y0);
        y1 = LIM;
    }
    if (x0 >= LIM && x1 >= LIM)
        return;
    if (x0 <= -LIM && x1 <= -LIM) {
        put_edge(f, -LIM, y0, -LIM, y1, dir);
        return;
    }
    if (x0 < -LIM || x1 < -LIM) {
        double ym = y_at(x0, y0, x1, y1, -LIM);
        if (x0 < -LIM) {
            put_edge(f, -LIM, y0, -LIM, ym, dir);
            x0 = -LIM;
            y0 = ym;
        } else {
            put_edge(f, -LIM, ym, -LIM, y1, dir);
            x1 = -LIM;
            y1 = ym;
        }
    }
    if (x0 > LIM || x1 > LIM) {
        double ym = y_at(x0, y0, x1, y1, LIM);
        if (x0 > LIM) {
            x0 = LIM;
            y0 = ym;
        } else {
            x1 = LIM;
            y1 = ym;
        }
    }
    put_edge(f, x0, y0, x1, y1, dir);
}

/* A curve whose control points all lie on one side of the pixels drawn
 * counts there only by where it starts and ends: its chord will do. */
static bool beside(const flat *f, const double *x, const double *y, int n)
{
    double x0 = x[0], x1 = x[0], y0 = y[0], y1 = y[0];
    for (int i = 1; i < n; i++) {
        x0 = fmin(x0, x[i]);
        x1 = fmax(x1, x[i]);
        y0 = fmin(y0, y[i]);
        y1 = fmax(y1, y[i]);
    }
    return y1 <= f->t || y0 >= f->b || x0 >= f->r || x1 <= f->l;
}

/* A curve needing more chords than this is halved first, so that halves
 * beside the pixels drawn become chords: a huge curve crossing a small
 * image costs a few dozen edges, not SK_GFX_MAX_STEPS. */
#define SPLIT_STEPS 16
#define MAX_DEPTH 24

static void add_quad(flat *f, const double *x, const double *y, int depth)
{
    if (beside(f, x, y, 3)) {
        add_edge(f, x[0], y[0], x[2], y[2]);
        return;
    }
    int n = sk_gfx_steps(sk_gfx_hypot(x[0] - 2 * x[1] + x[2], y[0] - 2 * y[1] + y[2]), false);
    if (n > SPLIT_STEPS && depth < MAX_DEPTH) {
        double ax = (x[0] + x[1]) / 2, bx = (x[1] + x[2]) / 2, mx = (ax + bx) / 2;
        double ay = (y[0] + y[1]) / 2, by = (y[1] + y[2]) / 2, my = (ay + by) / 2;
        double lx[3] = {x[0], ax, mx}, ly[3] = {y[0], ay, my}, rx[3] = {mx, bx, x[2]}, ry[3] = {my, by, y[2]};
        add_quad(f, lx, ly, depth + 1);
        add_quad(f, rx, ry, depth + 1);
        return;
    }
    double px = x[0], py = y[0];
    for (int i = 1; i < n; i++) {
        double t = (double)i / n;
        double ax = sk_gfx_lerp(x[0], x[1], t), bx = sk_gfx_lerp(x[1], x[2], t);
        double ay = sk_gfx_lerp(y[0], y[1], t), by = sk_gfx_lerp(y[1], y[2], t);
        double qx = sk_gfx_lerp(ax, bx, t), qy = sk_gfx_lerp(ay, by, t);
        add_edge(f, px, py, qx, qy);
        px = qx;
        py = qy;
    }
    add_edge(f, px, py, x[2], y[2]);
}

static void add_cubic(flat *f, const double *x, const double *y, int depth)
{
    if (beside(f, x, y, 4)) {
        add_edge(f, x[0], y[0], x[3], y[3]);
        return;
    }
    double dd = fmax(sk_gfx_hypot(x[0] - 2 * x[1] + x[2], y[0] - 2 * y[1] + y[2]),
                     sk_gfx_hypot(x[1] - 2 * x[2] + x[3], y[1] - 2 * y[2] + y[3]));
    int n = sk_gfx_steps(dd, true);
    if (n > SPLIT_STEPS && depth < MAX_DEPTH) {
        double ax = (x[0] + x[1]) / 2, bx = (x[1] + x[2]) / 2, cx = (x[2] + x[3]) / 2;
        double ay = (y[0] + y[1]) / 2, by = (y[1] + y[2]) / 2, cy = (y[2] + y[3]) / 2;
        double abx = (ax + bx) / 2, bcx = (bx + cx) / 2, mx = (abx + bcx) / 2;
        double aby = (ay + by) / 2, bcy = (by + cy) / 2, my = (aby + bcy) / 2;
        double lx[4] = {x[0], ax, abx, mx}, ly[4] = {y[0], ay, aby, my};
        double rx[4] = {mx, bcx, cx, x[3]}, ry[4] = {my, bcy, cy, y[3]};
        add_cubic(f, lx, ly, depth + 1);
        add_cubic(f, rx, ry, depth + 1);
        return;
    }
    double px = x[0], py = y[0];
    for (int i = 1; i < n; i++) {
        double t = (double)i / n;
        double ax = sk_gfx_lerp(x[0], x[1], t), bx = sk_gfx_lerp(x[1], x[2], t), cx = sk_gfx_lerp(x[2], x[3], t);
        double ay = sk_gfx_lerp(y[0], y[1], t), by = sk_gfx_lerp(y[1], y[2], t), cy = sk_gfx_lerp(y[2], y[3], t);
        double abx = sk_gfx_lerp(ax, bx, t), bcx = sk_gfx_lerp(bx, cx, t);
        double aby = sk_gfx_lerp(ay, by, t), bcy = sk_gfx_lerp(by, cy, t);
        double qx = sk_gfx_lerp(abx, bcx, t), qy = sk_gfx_lerp(aby, bcy, t);
        add_edge(f, px, py, qx, qy);
        px = qx;
        py = qy;
    }
    add_edge(f, px, py, x[3], y[3]);
}

/* The path through m as edges, each subpath closed, and the pixels of
 * area it may touch in *region. False when it touches none, or when a
 * point is NaN or infinite, which draws nothing. */
static bool flatten(const sk_path *p, const sk_xform *m, sk_rect area, flat *f, sk_rect *region)
{
    size_t n = p->pts.len / 2;
    if (!n)
        return false;
    double *x = sk_xreallocarray(NULL, n, 2 * sizeof *x), *y = x + n;
    double x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
    bool finite = true;
    for (size_t i = 0; i < n; i++) {
        double px = p->pts.ptr[2 * i], py = p->pts.ptr[2 * i + 1];
        /* A product of two floats is exact in a double, so each sum
         * rounds the same whether or not the compiler fuses it. */
        double ax = m->a * px, cy = m->c * py, bx = m->b * px, dy = m->d * py;
        x[i] = ax + cy + m->e;
        y[i] = bx + dy + m->f;
        finite = finite && isfinite(x[i]) && isfinite(y[i]);
        x0 = fmin(x0, x[i]);
        x1 = fmax(x1, x[i]);
        y0 = fmin(y0, y[i]);
        y1 = fmax(y1, y[i]);
    }
    f->l = fmax(fmax(floor(x0), area.x), -LIM);
    f->t = fmax(fmax(floor(y0), area.y), -LIM);
    f->r = fmin(fmin(ceil(x1), (double)area.x + area.w), LIM);
    f->b = fmin(fmin(ceil(y1), (double)area.y + area.h), LIM);
    if (!finite || !(f->r > f->l && f->b > f->t)) {
        free(x);
        return false;
    }
    *region = sk_rect_make((int)f->l, (int)f->t, (int)(f->r - f->l), (int)(f->b - f->t));

    size_t k = 0, start = 0;
    bool have = false;
    for (size_t v = 0; v < p->verbs.len; v++) {
        switch (p->verbs.ptr[v]) {
        case SK_PATH_MOVE:
            if (have)
                add_edge(f, x[k - 1], y[k - 1], x[start], y[start]);
            start = k++;
            have = true;
            break;
        case SK_PATH_LINE:
            add_edge(f, x[k - 1], y[k - 1], x[k], y[k]);
            k++;
            break;
        case SK_PATH_QUAD:
            add_quad(f, x + k - 1, y + k - 1, 0);
            k += 2;
            break;
        case SK_PATH_CUBIC:
            add_cubic(f, x + k - 1, y + k - 1, 0);
            k += 3;
            break;
        default: /* SK_PATH_CLOSE */
            add_edge(f, x[k - 1], y[k - 1], x[start], y[start]);
            have = false;
            break;
        }
    }
    if (have)
        add_edge(f, x[k - 1], y[k - 1], x[start], y[start]);
    free(x);
    return true;
}

/* ---- Rasterizing ---- */

/* Coverage is exact area, summed in cells, a cell to a pixel. A piece of
 * an edge crossing a cell from (x0, y0) to (x1, y1), in SUBths within
 * it, spans dy = y1 - y0 of its height and leaves (x0 + x1) dy / 2 of
 * its area to the left. The cell gains twice the area right of the
 * piece, 2 SUB dy - (x0 + x1) dy, and the next cell the rest, (x0 + x1)
 * dy, so a row's running sum is twice the signed area of each pixel
 * inside the path: FULL is a whole pixel. The fill rule then applies to
 * the sum, not to a winding number.
 *
 * Where an edge crosses from row to row, or from cell to cell, comes
 * from its own endpoints by one formula, wherever a band or a clip
 * begins: so the pixels are the same however the region is cut up. */
#define FULL (2 * SUB * SUB)
/* The widest band done at once. */
#define MAX_DIM (1 << 20)
/* The cells a band of rows holds. */
#define BAND_CELLS (1 << 12)

typedef struct {
    int64_t *acc;   /* w + 1 cells a row */
    uint64_t *seen; /* a bit for each cell acc has touched */
    size_t words;   /* seen's words a row */
    int x, y, w, h; /* the band, in pixels */
} band;

/* The piece of an edge in cell c of row r of the band, at fx0 and fx1
 * within it, spanning dy of its height. */
static void cell(band *b, int c, int r, int fx0, int fx1, int dy)
{
    int64_t *row = b->acc + (size_t)r * (size_t)(b->w + 1);
    uint64_t *seen = b->seen + (size_t)r * b->words;
    int64_t area = (int64_t)(fx0 + fx1) * dy;
    row[c] += (int64_t)2 * SUB * dy - area;
    row[c + 1] += area;
    seen[c >> 6] |= (uint64_t)1 << (c & 63);
    seen[(c + 1) >> 6] |= (uint64_t)1 << ((c + 1) & 63);
}

/* y where a piece from (xa, ya) to (xb, yb), xa != xb, crosses x: one
 * formula, wherever the piece is cut. */
static int y_across(int64_t xa, int ya, int64_t xb, int yb, int64_t x)
{
    return (int)(ya + (int64_t)(yb - ya) * (x - xa) / (xb - xa));
}

/* The piece of an edge in row r of the band, from (xa, ya) to (xb, yb):
 * x in SUBths of the image, y in SUBths of the row, ya < yb. Cut where
 * it crosses from cell to cell; left of the band it counts as if on the
 * band's left side, and right of the band not at all. */
static void piece(band *b, int r, int64_t xa, int ya, int64_t xb, int yb, int dir)
{
    int64_t left = (int64_t)b->x * SUB, right = (int64_t)(b->x + b->w) * SUB;
    if (xa >= right && xb >= right)
        return;
    if (xa <= left && xb <= left) {
        cell(b, 0, r, 0, 0, (yb - ya) * dir);
        return;
    }
    if (xa == xb) {
        int c = (int)((xa - left) / SUB), fx = (int)(xa - left - (int64_t)c * SUB);
        cell(b, c, r, fx, fx, (yb - ya) * dir);
        return;
    }
    int64_t px = xa;
    int py = ya;
    if (xa < xb) {
        if (px < left) {
            int ny = y_across(xa, ya, xb, yb, left);
            cell(b, 0, r, 0, 0, (ny - py) * dir);
            px = left;
            py = ny;
        }
        for (int c = (int)((px - left) / SUB);; c++) {
            int64_t cx = left + (int64_t)c * SUB, nx = SK_MIN(cx + SUB, xb);
            int ny = nx == xb ? yb : y_across(xa, ya, xb, yb, nx);
            cell(b, c, r, (int)(px - cx), (int)(nx - cx), (ny - py) * dir);
            if (nx == xb || c + 1 == b->w)
                break;
            px = nx;
            py = ny;
        }
    } else {
        if (px > right) {
            py = y_across(xa, ya, xb, yb, right);
            px = right;
        }
        for (int c = (int)((px - left - 1) / SUB);; c--) {
            int64_t cx = left + (int64_t)c * SUB, nx = SK_MAX(cx, xb);
            int ny = nx == xb ? yb : y_across(xa, ya, xb, yb, nx);
            cell(b, c, r, (int)(px - cx), (int)(nx - cx), (ny - py) * dir);
            if (nx == xb)
                break;
            if (c == 0) {
                cell(b, 0, r, 0, 0, (yb - ny) * dir);
                break;
            }
            px = nx;
            py = ny;
        }
    }
}

/* x where e crosses y: one formula, wherever a band begins. */
static int64_t x_across(const edge *e, int64_t y)
{
    return e->x0 + ((int64_t)e->x1 - e->x0) * (y - e->y0) / ((int64_t)e->y1 - e->y0);
}

/* The rows of e within the band, each cut where e crosses into the next
 * row. */
static void walk(band *b, const edge *e)
{
    int64_t top = (int64_t)b->y * SUB, bot = (int64_t)(b->y + b->h) * SUB;
    if (e->y1 <= top || e->y0 >= bot)
        return;
    int64_t ya = SK_MAX(e->y0, top), xa = x_across(e, ya);
    for (int r = (int)(ya / SUB); r < b->y + b->h; r++) {
        int64_t base = (int64_t)r * SUB, yb = SK_MIN(e->y1, base + SUB), xb = x_across(e, yb);
        piece(b, r - b->y, xa, (int)(ya - base), xb, (int)(yb - base), e->dir);
        if (yb == e->y1)
            break;
        xa = xb;
        ya = yb;
    }
}

/* Where the coverage goes: an image or a mask. */
typedef struct sink sink;
struct sink {
    void (*span)(const sink *s, int x, int y, int n, uint32_t cover);
    sk_image *im;
    sk_px p;
    uint8_t *mask;
    int stride;
};

static void to_image(const sink *s, int x, int y, int n, uint32_t cover)
{
    sk_gfx_span(sk_image_row(s->im, y) + x, n, s->p, cover);
}

static void to_mask(const sink *s, int x, int y, int n, uint32_t cover)
{
    memset(s->mask + (size_t)y * (size_t)s->stride + (size_t)x, (int)cover, (size_t)n);
}

/* The fill rule on a running sum, as 0..255: nonzero takes its size up
 * to a whole pixel, even-odd folds it into 0..FULL. */
static uint32_t coverage(int64_t a, sk_fill_rule rule)
{
    if (a < 0)
        a = -a;
    if (rule == SK_FILL_EVENODD) {
        a &= 2 * FULL - 1;
        if (a > FULL)
            a = 2 * FULL - a;
    } else if (a > FULL) {
        a = FULL;
    }
    return (uint32_t)((a * 255 + FULL / 2) / FULL);
}

static int lowest_bit(uint64_t v)
{
    int n = 0;
    for (int s = 32; s; s >>= 1)
        if (!(v & (((uint64_t)1 << s) - 1))) {
            v >>= s;
            n += s;
        }
    return n;
}

/* Row r of the band to the sink: a pixel for each cell an edge touched,
 * a span for each run between them. Leaves the row's cells zero. */
static void sweep(band *b, int r, sk_fill_rule rule, const sink *s)
{
    int64_t *acc = b->acc + (size_t)r * (size_t)(b->w + 1);
    uint64_t *seen = b->seen + (size_t)r * b->words;
    int64_t sum = 0;
    int x = 0, y = b->y + r;
    uint32_t cover = 0;
    for (size_t i = 0; i < b->words; i++) {
        uint64_t bits = seen[i];
        seen[i] = 0;
        for (; bits; bits &= bits - 1) {
            int c = (int)(i * 64) + lowest_bit(bits);
            int64_t delta = acc[c];
            acc[c] = 0;
            if (c == b->w)
                break;
            if (c > x && sum && (cover = coverage(sum, rule)))
                s->span(s, b->x + x, y, c - x, cover);
            sum += delta;
            if ((cover = coverage(sum, rule)))
                s->span(s, b->x + c, y, 1, cover);
            x = c + 1;
        }
    }
    if (x < b->w && sum && (cover = coverage(sum, rule)))
        s->span(s, b->x + x, y, b->w - x, cover);
}

static int by_top(const void *a, const void *b)
{
    int32_t ya = ((const edge *)a)->y0, yb = ((const edge *)b)->y0;
    return (ya > yb) - (ya < yb);
}

/* The edges filled over region, in bands of rows: an edge joins the
 * active ones at the band its top is in, and leaves after its bottom. */
static void raster(edge *e, size_t n, sk_rect region, sk_fill_rule rule, const sink *s)
{
    qsort(e, n, sizeof *e, by_top);
    size_t *active = sk_xreallocarray(NULL, n ? n : 1, sizeof *active);
    for (int x = 0; x < region.w; x += MAX_DIM) {
        band b = {0};
        b.x = region.x + x;
        b.w = SK_MIN(region.w - x, MAX_DIM);
        int rows = SK_MAX(1, SK_MIN(region.h, BAND_CELLS / (b.w + 1)));
        b.words = (size_t)b.w / 64 + 1;
        b.acc = sk_xcalloc((size_t)(b.w + 1) * (size_t)rows, sizeof *b.acc);
        b.seen = sk_xcalloc(b.words * (size_t)rows, sizeof *b.seen);
        size_t next = 0, live = 0;
        for (int y = region.y; y < region.y + region.h; y += rows) {
            b.y = y;
            b.h = SK_MIN(rows, region.y + region.h - y);
            while (next < n && e[next].y0 < (int64_t)(b.y + b.h) * SUB)
                active[live++] = next++;
            size_t kept = 0;
            for (size_t i = 0; i < live; i++)
                if (e[active[i]].y1 > (int64_t)b.y * SUB)
                    active[kept++] = active[i];
            live = kept;
            for (size_t i = 0; i < live; i++)
                walk(&b, &e[active[i]]);
            for (int r = 0; r < b.h; r++)
                sweep(&b, r, rule, s);
        }
        free(b.acc);
        free(b.seen);
    }
    free(active);
}

static void fill(const sk_path *p, const sk_xform *m, sk_fill_rule rule, sk_rect area, const sink *s)
{
    static const sk_xform identity = {1, 0, 0, 1, 0, 0};
    flat f = {0};
    sk_rect region;
    if (p && flatten(p, m ? m : &identity, area, &f, &region))
        raster(f.edges.ptr, f.edges.len, region, rule, s);
    sk_vec_free(&f.edges);
}

void sk_fill_path(sk_image *dst, const sk_path *p, const sk_xform *m, sk_fill_rule rule, sk_color c,
                  const sk_rect *clip)
{
    sk_rect area = sk_image_bounds(dst);
    if (clip)
        area = sk_rect_intersect(area, *clip);
    sink s = {to_image, dst, sk_premul(c), NULL, 0};
    if (s.p && !sk_rect_empty(area))
        fill(p, m, rule, area, &s);
}

uint8_t *sk_path_mask(const sk_path *p, const sk_xform *m, sk_fill_rule rule, int dst_w, int dst_h)
{
    if (dst_w <= 0 || dst_h <= 0)
        return NULL;
    sink s = {to_mask, NULL, 0, sk_xcalloc((size_t)dst_w, (size_t)dst_h), dst_w};
    fill(p, m, rule, sk_rect_make(0, 0, dst_w, dst_h), &s);
    return s.mask;
}
