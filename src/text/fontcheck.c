#include "fontcheck.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>

/* Values the check may read before it gives up on a font as too costly
 * to check: the skins' fonts take at most 466,623, SpecialElite's. */
#define BUDGET ((uint64_t)1 << 27)

#define TAG(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))

typedef struct {
    const uint8_t *p; /* NULL when the font has none */
    uint32_t len;
} table;

/* Font units; empty when x0 > x1. */
typedef struct {
    int32_t x0, y0, x1, y1;
} box;

static const box EMPTY = {1, 1, 0, 0};

typedef struct {
    const uint8_t *data;
    size_t n;
    char *err;
    size_t err_len;
    uint64_t work;
    table head, hhea, maxp, hmtx, loca, glyf, cmap, kern, gpos, name, os2, post;
    uint32_t glyphs;
    bool long_loca;
    int upem;
    uint8_t *busy; /* a glyph's compound being walked, to find one that holds itself */
    /* A simple glyph's points: 65536 at most. */
    uint8_t *flags;
    int16_t *xs, *ys;
} checker;

static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)(p[0] << 8 | p[1]);
}

static int16_t s16(const uint8_t *p)
{
    int v = u16(p);
    return (int16_t)(v < 32768 ? v : v - 65536);
}

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static bool fail(checker *c, const char *fmt, ...) SK_PRINTF(2, 3);

static bool fail(checker *c, const char *fmt, ...)
{
    if (c->err_len) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(c->err, c->err_len, fmt, ap);
        va_end(ap);
    }
    return false;
}

/* Counts k reads against the budget. */
static bool spend(checker *c, uint64_t k)
{
    c->work += k;
    return c->work <= BUDGET;
}

static bool too_costly(checker *c)
{
    return fail(c, "takes too long to check: its tables ask for more than %llu reads",
                (unsigned long long)BUDGET);
}

/* Whether len bytes at at lie inside t. */
static bool inside(table t, uint64_t at, uint64_t len)
{
    return at + len <= t.len;
}

static void tag_name(uint32_t tag, char out[5])
{
    for (int i = 0; i < 4; i++) {
        char ch = (char)(tag >> (24 - 8 * i));
        out[i] = ch >= 0x20 && ch < 0x7f ? ch : '?';
    }
    out[4] = 0;
}

/* ---- The directory ---- */

static table *slot(checker *c, uint32_t tag)
{
    switch (tag) {
    case TAG('h', 'e', 'a', 'd'):
        return &c->head;
    case TAG('h', 'h', 'e', 'a'):
        return &c->hhea;
    case TAG('m', 'a', 'x', 'p'):
        return &c->maxp;
    case TAG('h', 'm', 't', 'x'):
        return &c->hmtx;
    case TAG('l', 'o', 'c', 'a'):
        return &c->loca;
    case TAG('g', 'l', 'y', 'f'):
        return &c->glyf;
    case TAG('c', 'm', 'a', 'p'):
        return &c->cmap;
    case TAG('k', 'e', 'r', 'n'):
        return &c->kern;
    case TAG('G', 'P', 'O', 'S'):
        return &c->gpos;
    case TAG('n', 'a', 'm', 'e'):
        return &c->name;
    case TAG('O', 'S', '/', '2'):
        return &c->os2;
    case TAG('p', 'o', 's', 't'):
        return &c->post;
    default:
        return NULL;
    }
}

static bool cff(checker *c)
{
    return fail(c, "has CFF outlines: a skin's fonts must have TrueType outlines (glyf)");
}

/* stb_truetype and font.c each take a tag's first table: two of one
 * tag are refused, so both read the same. */
static bool directory(checker *c)
{
    const uint8_t *d = c->data;
    if (c->n < 12)
        return fail(c, "is too short to be a font");
    if (c->n > SK_FONTCHECK_MAX_BYTES)
        return fail(c, "is larger than a font may be, %zu MiB", SK_FONTCHECK_MAX_BYTES >> 20);
    uint32_t version = u32(d);
    if (version == TAG('O', 'T', 'T', 'O'))
        return cff(c);
    if (version == TAG('t', 't', 'c', 'f'))
        return fail(c, "is a font collection: a skin's font file holds one face");
    if (version != 0x00010000 && version != TAG('t', 'r', 'u', 'e'))
        return fail(c, "is not a TrueType font");
    uint32_t count = u16(d + 4);
    if (!count || 12 + 16 * (uint64_t)count > c->n)
        return fail(c, "has a table directory that runs past the end of the file");
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *r = d + 12 + 16 * i;
        uint32_t tag = u32(r), off = u32(r + 8), len = u32(r + 12);
        char name[5];
        tag_name(tag, name);
        if ((uint64_t)off + len > c->n)
            return fail(c, "has a %s table that runs past the end of the file", name);
        if (tag == TAG('C', 'F', 'F', ' ') || tag == TAG('C', 'F', 'F', '2'))
            return cff(c);
        if (tag == TAG('s', 'b', 'i', 'x') || tag == TAG('C', 'B', 'D', 'T'))
            return fail(c, "has bitmap glyphs (%s): a skin's fonts must have TrueType outlines (glyf)", name);
        table *t = slot(c, tag);
        if (!t)
            continue;
        if (t->p)
            return fail(c, "has two %s tables", name);
        t->p = d + off;
        t->len = len;
    }
    if (!c->glyf.p)
        return fail(c, "has no glyf table: a skin's fonts must have TrueType outlines (glyf)");
    const struct {
        const table *t;
        const char *name;
    } needed[] = {{&c->head, "head"}, {&c->hhea, "hhea"}, {&c->maxp, "maxp"},
                  {&c->hmtx, "hmtx"}, {&c->loca, "loca"}, {&c->cmap, "cmap"}};
    for (size_t i = 0; i < SK_LEN(needed); i++)
        if (!needed[i].t->p)
            return fail(c, "has no %s table", needed[i].name);
    return true;
}

/* ---- head, maxp, hhea, hmtx, loca ---- */

static bool check_metrics(checker *c)
{
    if (c->head.len < 54)
        return fail(c, "has a head table shorter than its 54 bytes");
    if (u32(c->head.p + 12) != 0x5F0F3CF5)
        return fail(c, "has a head table without its magic number");
    c->upem = u16(c->head.p + 18);
    if (c->upem < 16 || c->upem > 16384)
        return fail(c, "has %d units to the em, outside 16 to 16384", c->upem);
    unsigned format = u16(c->head.p + 50);
    if (format > 1)
        return fail(c, "has a loca format of %u, neither 0 nor 1", format);
    c->long_loca = format == 1;
    if (c->maxp.len < 6)
        return fail(c, "has a maxp table too short for its glyph count");
    c->glyphs = u16(c->maxp.p + 4);
    if (!c->glyphs)
        return fail(c, "has no glyphs");
    if (c->hhea.len < 36)
        return fail(c, "has an hhea table shorter than its 36 bytes");
    /* stb_truetype reads advance numLong - 1 for every glyph past it. */
    uint32_t numlong = u16(c->hhea.p + 34);
    if (!numlong)
        return fail(c, "has no horizontal metrics in its hhea table");
    uint64_t need = numlong >= c->glyphs ? 4 * (uint64_t)c->glyphs : 4 * (uint64_t)numlong + 2 * (uint64_t)(c->glyphs - numlong);
    if (!inside(c->hmtx, 0, need))
        return fail(c, "has an hmtx table too short for its %u glyphs", c->glyphs);
    uint32_t per = c->long_loca ? 4 : 2;
    if (!inside(c->loca, 0, (uint64_t)(c->glyphs + 1) * per))
        return fail(c, "has a loca table too short for its %u glyphs", c->glyphs);
    if (!spend(c, c->glyphs))
        return too_costly(c);
    uint64_t last = 0;
    for (uint32_t i = 0; i <= c->glyphs; i++) {
        uint64_t at = c->long_loca ? u32(c->loca.p + 4 * i) : 2 * (uint64_t)u16(c->loca.p + 2 * i);
        if (at < last)
            return fail(c, "has a loca table that goes back at glyph %u", i);
        if (at > c->glyf.len)
            return fail(c, "has a loca table pointing past its glyf table at entry %u", i);
        last = at;
    }
    return true;
}

/* ---- Glyphs ---- */

static void grow(box *b, int32_t x, int32_t y)
{
    if (b->x0 > b->x1) {
        b->x0 = b->x1 = x;
        b->y0 = b->y1 = y;
        return;
    }
    b->x0 = SK_MIN(b->x0, x);
    b->x1 = SK_MAX(b->x1, x);
    b->y0 = SK_MIN(b->y0, y);
    b->y1 = SK_MAX(b->y1, y);
}

static bool within(box in, box out)
{
    return in.x0 > in.x1 || (in.x0 >= out.x0 && in.x1 <= out.x1 && in.y0 >= out.y0 && in.y1 <= out.y1);
}

static box widen(box b, int32_t by)
{
    box w = {b.x0 - by, b.y0 - by, b.x1 + by, b.y1 + by};
    return w;
}

/* (a + b) >> 1 of stb_truetype's midpoints, an arithmetic shift: half,
 * rounded down. */
static int32_t half(int32_t v)
{
    return v >= 0 ? v / 2 : -((1 - v) / 2);
}

/* A component's placement as stb_truetype computes it: x becomes
 * m (a x + c y + e) and y n (b x + d y + f), m and n the lengths of the
 * matrix's columns, in floats, each value then cut toward zero to a
 * short. */
typedef struct {
    float mtx[6], m, n;
} placement;

/* What stb_truetype makes of the glyph being checked: its vertices
 * counted, and the box of the outline they trace, curves' extremes
 * included, which is where the rasterizer's points along them fall. A
 * component's vertices pass through the placements of the compounds
 * that hold it, the innermost first, as stb_truetype places them. */
typedef struct {
    box b;
    int32_t px, py; /* the pen */
    uint64_t count;
    const placement *stack[SK_FONTCHECK_MAX_DEPTH];
    int depth;
    bool overflow; /* a placed value left a short's range */
} trace;

/* The extremes of a quadratic from p0 to p1 about c, on one axis,
 * rounded outward, past a doubt of the arithmetic. */
static void extremes(int32_t *lo, int32_t *hi, double p0, double c, double p1)
{
    double d = p0 - 2 * c + p1;
    if (d == 0)
        return;
    double t = (p0 - c) / d;
    if (!(t > 0 && t < 1))
        return;
    double v = (1 - t) * (1 - t) * p0 + 2 * t * (1 - t) * c + t * t * p1;
    *lo = SK_MIN(*lo, (int32_t)floor(v + 1e-9));
    *hi = SK_MAX(*hi, (int32_t)ceil(v - 1e-9));
}

/* One vertex as stbtt_setvertex stores it, then placed. A value the
 * cast would take past a short's range, where it is undefined, is
 * refused, a unit to spare for floats summed in another order. */
static void emit(trace *t, bool curve, int32_t x, int32_t y, int32_t cx, int32_t cy)
{
    int32_t v[4] = {x, y, cx, cy};
    for (int k = t->depth - 1; k >= 0; k--) {
        const placement *pl = t->stack[k];
        const float *m = pl->mtx;
        float ax = (float)v[0], ay = (float)v[1], bx = (float)v[2], by = (float)v[3];
        float out[4] = {pl->m * (m[0] * ax + m[2] * ay + m[4]), pl->n * (m[1] * ax + m[3] * ay + m[5]),
                        pl->m * (m[0] * bx + m[2] * by + m[4]), pl->n * (m[1] * bx + m[3] * by + m[5])};
        for (int i = 0; i < 4; i++) {
            if (!(out[i] > -32768.0f && out[i] < 32767.0f)) {
                t->overflow = true;
                return;
            }
            v[i] = (int32_t)out[i];
        }
    }
    t->count++;
    grow(&t->b, v[0], v[1]);
    if (curve) {
        extremes(&t->b.x0, &t->b.x1, t->px, v[2], v[0]);
        extremes(&t->b.y0, &t->b.y1, t->py, v[3], v[1]);
    }
    t->px = v[0];
    t->py = v[1];
}

/* stbtt__close_shape: back to the contour's start, through the point
 * it set aside when the contour started off the curve. */
static void close_contour(trace *t, bool was_off, bool start_off, int32_t sx, int32_t sy, int32_t scx, int32_t scy,
                          int32_t cx, int32_t cy)
{
    if (start_off) {
        if (was_off)
            emit(t, true, half(cx + scx), half(cy + scy), cx, cy);
        emit(t, true, sx, sy, scx, scy);
    } else {
        emit(t, was_off, sx, sy, cx, cy);
    }
}

/* A simple glyph read as stbtt__GetGlyphShapeTT reads it: the flags
 * with their repeats, the x deltas, the y deltas, every byte inside the
 * glyph; then its walk of the contours, a move a contour, a line or a
 * curve for a point on the curve or a second point off it, a curve or
 * two to close a contour. A contour whose first point is off the curve
 * starts from its second, or from the midpoint of the two. Its array
 * holds the points and two vertices a contour. */
static bool simple(checker *c, uint32_t gi, const uint8_t *p, uint32_t len, int nc, trace *t)
{
    uint32_t at = 10 + 2 * (uint32_t)nc;
    if (at + 2 > len)
        return fail(c, "has glyph %u's contours running past its end", gi);
    uint32_t n = 1 + (uint32_t)u16(p + at - 2);
    at += 2 + (uint32_t)u16(p + at);
    if (at > len)
        return fail(c, "has glyph %u's instructions running past its end", gi);
    if (!spend(c, (4 + (uint64_t)t->depth) * n))
        return too_costly(c);
    uint8_t flags = 0, repeat = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (repeat) {
            repeat--;
        } else {
            if (at >= len)
                return fail(c, "has glyph %u's points running past its end", gi);
            flags = p[at++];
            if (flags & 8) {
                if (at >= len)
                    return fail(c, "has glyph %u's points running past its end", gi);
                repeat = p[at++];
            }
        }
        c->flags[i] = flags;
    }
    for (int axis = 0; axis < 2; axis++) {
        uint8_t short_bit = axis ? 4 : 2, same_bit = axis ? 32 : 16;
        int16_t *out = axis ? c->ys : c->xs;
        int32_t v = 0;
        for (uint32_t i = 0; i < n; i++) {
            uint8_t f = c->flags[i];
            if (f & short_bit) {
                if (at >= len)
                    return fail(c, "has glyph %u's points running past its end", gi);
                v += f & same_bit ? p[at] : -p[at];
                at++;
            } else if (!(f & same_bit)) {
                if (at + 2 > len)
                    return fail(c, "has glyph %u's points running past its end", gi);
                v += s16(p + at);
                at += 2;
            }
            if (v < INT16_MIN || v > INT16_MAX)
                return fail(c, "has glyph %u with a point past the coordinates' range", gi);
            out[i] = (int16_t)v;
        }
    }
    uint64_t before = t->count;
    uint32_t next = 0, j = 0;
    int32_t sx = 0, sy = 0, scx = 0, scy = 0, cx = 0, cy = 0;
    bool was_off = false, start_off = false;
    for (uint32_t i = 0; i < n; i++) {
        int32_t x = c->xs[i], y = c->ys[i];
        bool on = c->flags[i] & 1;
        if (next == i) {
            if (i)
                close_contour(t, was_off, start_off, sx, sy, scx, scy, cx, cy);
            start_off = !on;
            if (start_off) {
                if (i + 1 >= n)
                    return fail(c, "has glyph %u ending in a contour of one point off the curve", gi);
                scx = x;
                scy = y;
                if (!(c->flags[i + 1] & 1)) {
                    sx = half(x + c->xs[i + 1]);
                    sy = half(y + c->ys[i + 1]);
                } else {
                    sx = c->xs[i + 1];
                    sy = c->ys[i + 1];
                    i++;
                }
            } else {
                sx = x;
                sy = y;
            }
            emit(t, false, sx, sy, 0, 0);
            was_off = false;
            if (j > (uint32_t)nc)
                return fail(c, "has glyph %u's contours out of order", gi);
            next = 1 + (uint32_t)u16(p + 10 + 2 * j);
            j++;
        } else if (!on) {
            if (was_off)
                emit(t, true, half(cx + x), half(cy + y), cx, cy);
            cx = x;
            cy = y;
            was_off = true;
        } else {
            emit(t, was_off, x, y, cx, cy);
            was_off = false;
        }
    }
    close_contour(t, was_off, start_off, sx, sy, scx, scy, cx, cy);
    if (t->count - before > n + 2 * (uint64_t)nc)
        return fail(c, "has glyph %u with more vertices than its points allow", gi);
    return true;
}

static bool walk(checker *c, uint32_t gi, trace *t);

/* A compound read as stbtt__GetGlyphShapeTT reads it: each component's
 * flags, glyph, offsets and scale inside the glyph, placed by offsets
 * (it asserts on matched points), its glyph's vertices placed and
 * added to the compound's. */
static bool compound(checker *c, uint32_t gi, const uint8_t *p, uint32_t len, trace *t)
{
    uint32_t at = 10;
    for (bool more = true; more;) {
        if (!spend(c, 1))
            return too_costly(c);
        if (at + 4 > len)
            return fail(c, "has glyph %u's components running past its end", gi);
        unsigned flags = u16(p + at), part = u16(p + at + 2);
        at += 4;
        if (!(flags & 2))
            return fail(c, "has glyph %u placing a component by matching points, which stb_truetype cannot", gi);
        placement pl = {.mtx = {1, 0, 0, 1, 0, 0}};
        float *mtx = pl.mtx;
        uint32_t args = flags & 1 ? 4 : 2;
        if (at + args > len)
            return fail(c, "has glyph %u's components running past its end", gi);
        mtx[4] = flags & 1 ? s16(p + at) : (int8_t)p[at];
        mtx[5] = flags & 1 ? s16(p + at + 2) : (int8_t)p[at + 1];
        at += args;
        if (!!(flags & 8) + !!(flags & 64) + !!(flags & 128) > 1)
            return fail(c, "has glyph %u giving a component two scales", gi);
        uint32_t scale = flags & 8 ? 2 : flags & 64 ? 4 : flags & 128 ? 8 : 0;
        if (at + scale > len)
            return fail(c, "has glyph %u's components running past its end", gi);
        if (flags & 8) {
            mtx[0] = mtx[3] = s16(p + at) / 16384.0f;
        } else if (flags & 64) {
            mtx[0] = s16(p + at) / 16384.0f;
            mtx[3] = s16(p + at + 2) / 16384.0f;
        } else if (flags & 128) {
            for (int k = 0; k < 4; k++)
                mtx[k] = s16(p + at + 2 * k) / 16384.0f;
        }
        at += scale;
        pl.m = (float)sqrt(mtx[0] * mtx[0] + mtx[1] * mtx[1]);
        pl.n = (float)sqrt(mtx[2] * mtx[2] + mtx[3] * mtx[3]);
        if (part >= c->glyphs)
            return fail(c, "has glyph %u holding glyph %u, which the font does not have", gi, part);
        if (t->depth >= SK_FONTCHECK_MAX_DEPTH)
            return fail(c, "has glyph %u nesting compounds more than %d deep", gi, SK_FONTCHECK_MAX_DEPTH);
        t->stack[t->depth++] = &pl;
        bool ok = walk(c, part, t);
        t->depth--;
        if (!ok)
            return false;
        if (t->overflow)
            return fail(c, "has glyph %u moving a component past the coordinates' range", gi);
        if (t->count > SK_FONTCHECK_MAX_VERTICES)
            return fail(c, "has glyph %u of more than %d vertices", gi, SK_FONTCHECK_MAX_VERTICES);
        more = flags & 32;
    }
    return true;
}

static void glyph_range(const checker *c, uint32_t gi, uint32_t *start, uint32_t *end)
{
    if (c->long_loca) {
        *start = u32(c->loca.p + 4 * gi);
        *end = u32(c->loca.p + 4 * gi + 4);
    } else {
        *start = 2 * (uint32_t)u16(c->loca.p + 2 * gi);
        *end = 2 * (uint32_t)u16(c->loca.p + 2 * gi + 2);
    }
}

/* stbtt_GetGlyphShape of glyph gi, emitted through t. */
static bool walk(checker *c, uint32_t gi, trace *t)
{
    if (c->busy[gi])
        return fail(c, "has glyph %u holding itself", gi);
    uint32_t start, end;
    glyph_range(c, gi, &start, &end);
    if (start == end)
        return true;
    const uint8_t *p = c->glyf.p + start;
    uint32_t len = end - start;
    if (len < 10)
        return fail(c, "has glyph %u shorter than its header", gi);
    int nc = s16(p);
    if (nc > 0)
        return simple(c, gi, p, len, nc, t);
    if (nc == 0)
        return true;
    c->busy[gi] = 1;
    bool ok = compound(c, gi, p, len, t);
    c->busy[gi] = 0;
    return ok;
}

static bool outside(checker *c, uint32_t gi, box drawn, box head)
{
    return fail(c, "has glyph %u drawing from (%d, %d) to (%d, %d), outside its box, (%d, %d) to (%d, %d)", gi,
                (int)drawn.x0, (int)drawn.y0, (int)drawn.x1, (int)drawn.y1, (int)head.x0, (int)head.y0,
                (int)head.x1, (int)head.y1);
}

/* Every glyph as stb_truetype draws it, into the bitmap its box sizes.
 * Font tools round the extremes of curves to whole units, and
 * stb_truetype cuts a placed component's points toward zero, so the
 * outline may pass the box by a unit, two for a compound; but a glyph
 * whose box tops out at or below the baseline, whose bitmap then
 * starts there, draws nothing above it, where stb_truetype's
 * rasterizer asserts. */
static bool check_glyphs(checker *c)
{
    c->busy = sk_xcalloc(c->glyphs, 1);
    c->flags = sk_xmalloc(65536);
    c->xs = sk_xmalloc(65536 * sizeof *c->xs);
    c->ys = sk_xmalloc(65536 * sizeof *c->ys);
    int32_t far = SK_FONTCHECK_MAX_EMS * c->upem;
    for (uint32_t gi = 0; gi < c->glyphs; gi++) {
        uint32_t start, end;
        glyph_range(c, gi, &start, &end);
        if (start == end)
            continue;
        const uint8_t *p = c->glyf.p + start;
        if (end - start < 10)
            return fail(c, "has glyph %u shorter than its header", gi);
        int nc = s16(p);
        if (!nc)
            continue;
        box head = {s16(p + 2), s16(p + 4), s16(p + 6), s16(p + 8)};
        if (head.x0 > head.x1 || head.y0 > head.y1)
            return fail(c, "has glyph %u with its box inside out", gi);
        if (head.x0 < -far || head.y0 < -far || head.x1 > far || head.y1 > far)
            return fail(c, "has glyph %u reaching more than %d ems from its origin", gi, SK_FONTCHECK_MAX_EMS);
        trace t = {.b = EMPTY};
        if (!walk(c, gi, &t))
            return false;
        if (!within(t.b, widen(head, nc > 0 ? 1 : 2)) || (head.y1 <= 0 && t.b.x0 <= t.b.x1 && t.b.y1 > 0))
            return outside(c, gi, t.b, head);
    }
    return true;
}

/* ---- cmap ---- */

/* Format 4's arrays, from the subtable's start. */
typedef struct {
    table s;
    uint32_t segs, range, selector, shift;
    uint64_t ends, starts, deltas, offsets;
} format4;

/* Whether stbtt_FindGlyphIndex's every read for the code cp lies inside
 * the subtable. */
static bool search4(const format4 *f, uint32_t cp)
{
    table s = f->s;
    uint64_t search = f->ends;
    if (!inside(s, search + 2 * (uint64_t)f->shift, 2))
        return false;
    if (cp >= u16(s.p + search + 2 * (uint64_t)f->shift))
        search += 2 * (uint64_t)f->shift;
    search -= 2;
    uint32_t r = f->range;
    for (uint32_t e = f->selector; e; e--) {
        r >>= 1;
        if (!inside(s, search + 2 * (uint64_t)r, 2))
            return false;
        if (cp > u16(s.p + search + 2 * (uint64_t)r))
            search += 2 * (uint64_t)r;
    }
    search += 2;
    uint64_t item = (uint16_t)((search - f->ends) >> 1);
    if (!inside(s, f->starts + 2 * item, 2) || !inside(s, f->ends + 2 * item, 2))
        return false;
    uint32_t start = u16(s.p + f->starts + 2 * item), end = u16(s.p + f->ends + 2 * item);
    if (cp < start || cp > end)
        return true;
    if (!inside(s, f->offsets + 2 * item, 2))
        return false;
    uint32_t ro = u16(s.p + f->offsets + 2 * item);
    if (!ro)
        return inside(s, f->deltas + 2 * item, 2);
    return inside(s, f->offsets + 2 * item + ro + 2 * (uint64_t)(cp - start), 2);
}

/* Format 4 as stbtt_FindGlyphIndex searches it: a binary search over
 * the segments' ends steered by the header's searchRange,
 * entrySelector and rangeShift, then the segment's start, delta and
 * range offset, and maybe a glyph from the array after them. With the
 * header the format defines, every read of the search lands on a
 * segment's end and the segment found is one of them; the glyph array
 * is then read for codes from a segment's start to its end. Any other
 * header is followed for every code. */
static bool cmap4(checker *c, uint32_t off)
{
    format4 f = {.s = {c->cmap.p + off, c->cmap.len - off}};
    if (!inside(f.s, 0, 14))
        return fail(c, "has a cmap format 4 subtable past the end of its table");
    f.segs = u16(f.s.p + 6) >> 1;
    f.range = u16(f.s.p + 8) >> 1;
    f.selector = u16(f.s.p + 10);
    f.shift = u16(f.s.p + 12) >> 1;
    if (!f.segs)
        return fail(c, "has a cmap format 4 subtable with no segments");
    if (!inside(f.s, 0, 16 + 8 * (uint64_t)f.segs))
        return fail(c, "has a cmap format 4 subtable whose segments run past its table");
    f.ends = 14;
    f.starts = 16 + 2 * (uint64_t)f.segs;
    f.deltas = 16 + 4 * (uint64_t)f.segs;
    f.offsets = 16 + 6 * (uint64_t)f.segs;
    uint32_t pow = 1, log = 0;
    while (pow * 2 <= f.segs) {
        pow *= 2;
        log++;
    }
    if (f.range == pow && f.selector == log && f.shift == f.segs - pow) {
        if (!spend(c, f.segs))
            return too_costly(c);
        for (uint32_t k = 0; k < f.segs; k++) {
            uint32_t start = u16(f.s.p + f.starts + 2 * k), end = u16(f.s.p + f.ends + 2 * k);
            uint32_t ro = u16(f.s.p + f.offsets + 2 * k);
            if (ro && start <= end && !inside(f.s, f.offsets + 2 * k + ro + 2 * (uint64_t)(end - start), 2))
                return fail(c, "has a cmap format 4 subtable whose glyph array runs past its table");
        }
        return true;
    }
    if (f.selector > 16)
        return fail(c, "has a cmap format 4 subtable with an entrySelector of %u", f.selector);
    if (!spend(c, 65536 * (uint64_t)(f.selector + 6)))
        return too_costly(c);
    for (uint32_t cp = 0; cp <= 0xFFFF; cp++)
        if (!search4(&f, cp))
            return fail(c, "has a cmap format 4 subtable whose search reads past its table");
    return true;
}

static bool unicode(const uint8_t *record)
{
    unsigned plat = u16(record), enc = u16(record + 2);
    return plat == 0 || (plat == 3 && (enc == 1 || enc == 10));
}

/* The Unicode subtables font.c picks from, and stb_truetype's own
 * choice, which font.c replaces: platform 0, and 3 with encoding 1 or
 * 10. Each is read for every code stbtt_FindGlyphIndex may be asked. */
static bool check_cmap(checker *c)
{
    table t = c->cmap;
    if (t.len < 4)
        return fail(c, "has a cmap table too short for its header");
    uint32_t count = u16(t.p + 2);
    if (!inside(t, 4, 8 * (uint64_t)count))
        return fail(c, "has a cmap table whose encodings run past its end");
    bool usable = false;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *r = t.p + 4 + 8 * i;
        uint32_t off = u32(r + 4);
        if (!unicode(r))
            continue;
        if (!inside(t, off, 2))
            return fail(c, "has a cmap subtable past the end of its table");
        /* A subtable two encodings share is read once. */
        bool seen = false;
        if (!spend(c, i))
            return too_costly(c);
        for (uint32_t k = 0; k < i && !seen; k++)
            seen = unicode(t.p + 4 + 8 * k) && u32(t.p + 4 + 8 * k + 4) == off;
        unsigned format = u16(t.p + off);
        if (format == 0 || format == 4 || format == 6 || format == 12)
            usable |= inside(t, off, 16); /* what font.c asks of the one it picks */
        if (seen)
            continue;
        table s = {t.p + off, t.len - off};
        switch (format) {
        case 0:
            /* Codes below the length less 6, one byte each from 6. */
            if (!inside(s, 0, 6) || !inside(s, 0, u16(s.p + 2)))
                return fail(c, "has a cmap format 0 subtable past the end of its table");
            break;
        case 4:
            if (!cmap4(c, off))
                return false;
            break;
        case 6:
            if (!inside(s, 0, 10) || !inside(s, 10, 2 * (uint64_t)u16(s.p + 8)))
                return fail(c, "has a cmap format 6 subtable past the end of its table");
            break;
        case 12:
        case 13: {
            if (!inside(s, 0, 16) || !inside(s, 16, 12 * (uint64_t)u32(s.p + 12)))
                return fail(c, "has a cmap format %u subtable past the end of its table", format);
            /* stb_truetype reads these as (p[0] << 24) + ..., an int
             * shift that overflows from 2^31: codes are Unicode's and
             * glyphs 16 bits, far below. */
            uint32_t groups = u32(s.p + 12);
            if (!spend(c, groups))
                return too_costly(c);
            for (uint32_t k = 0; k < groups; k++) {
                const uint8_t *g = s.p + 16 + 12 * k;
                if (u32(g) > 0x10FFFF || u32(g + 4) > 0x10FFFF || u32(g + 8) > 0xFFFF)
                    return fail(c, "has a cmap format %u group past Unicode's codes or the glyphs' 16 bits",
                                format);
            }
            break;
        }
        default:
            break;
        }
    }
    if (!usable)
        return fail(c, "has no Unicode cmap subtable of format 0, 4, 6 or 12");
    return true;
}

/* ---- kern and GPOS ---- */

/* stb_truetype reads the first subtable, when it is horizontal and of
 * format 0: a binary search of its pairs, each read as the first
 * glyph shifted 16 places in an int, and the glyph sought the same, so
 * neither may reach 32768. */
static bool check_kern(checker *c)
{
    table t = c->kern;
    if (!t.p)
        return true;
    if (!inside(t, 0, 4))
        return fail(c, "has a kern table too short for its header");
    if (u16(t.p + 2) < 1)
        return true;
    if (!inside(t, 0, 12))
        return fail(c, "has a kern table too short for its first subtable");
    if (u16(t.p + 8) != 1)
        return true;
    uint32_t pairs = u16(t.p + 10);
    if (!inside(t, 0, 18) || !inside(t, 18, 6 * (uint64_t)pairs))
        return fail(c, "has a kern table whose pairs run past its end");
    if (c->glyphs > 32768)
        return fail(c, "has a kern table and more than 32768 glyphs, which stb_truetype cannot search");
    for (uint32_t k = 0; k < pairs; k++)
        if (u16(t.p + 18 + 6 * k) >= 32768)
            return fail(c, "has a kern pair of glyph %u, past the 32768 stb_truetype can search",
                        (unsigned)u16(t.p + 18 + 6 * k));
    return true;
}

/* The most a coverage table's index may be for a glyph of the font, or
 * -1 when it matches none; false when its search reads past the
 * table. */
static bool coverage(checker *c, table t, uint64_t at, int64_t *most)
{
    *most = -1;
    if (!inside(t, at, 2))
        return false;
    unsigned format = u16(t.p + at);
    if (format == 1) {
        if (!inside(t, at, 4))
            return false;
        uint32_t count = u16(t.p + at + 2);
        if (!inside(t, at + 4, 2 * (uint64_t)count))
            return false;
        *most = (int64_t)count - 1;
    } else if (format == 2) {
        if (!inside(t, at, 4))
            return false;
        uint32_t count = u16(t.p + at + 2);
        if (!inside(t, at + 4, 6 * (uint64_t)count))
            return false;
        if (!spend(c, count))
            return false;
        for (uint32_t k = 0; k < count; k++) {
            const uint8_t *r = t.p + at + 4 + 6 * k;
            uint32_t start = u16(r), end = SK_MIN((uint32_t)u16(r + 2), c->glyphs - 1);
            if (start <= end)
                *most = SK_MAX(*most, (int64_t)u16(r + 4) + (end - start));
        }
    }
    return true;
}

static bool class_def(checker *c, table t, uint64_t at)
{
    if (!inside(t, at, 2))
        return false;
    unsigned format = u16(t.p + at);
    if (format == 1) {
        if (!inside(t, at, 6))
            return false;
        uint32_t start = u16(t.p + at + 2), count = u16(t.p + at + 4);
        uint32_t reach = start >= c->glyphs ? 0 : SK_MIN(count, c->glyphs - start);
        return inside(t, at + 6, 2 * (uint64_t)reach);
    }
    if (format == 2)
        return inside(t, at, 4) && inside(t, at + 4, 6 * (uint64_t)u16(t.p + at + 2));
    return true;
}

/* One pair adjustment subtable, for every first glyph the coverage
 * can index: stb_truetype reads a pair set's offset, and that set's
 * count, before it checks the index against the set count. */
static bool pair_pos(checker *c, table t, uint64_t st)
{
    if (!inside(t, st, 4))
        return false;
    int64_t most;
    if (!coverage(c, t, st + u16(t.p + st + 2), &most))
        return false;
    if (most < 0)
        return true;
    unsigned format = u16(t.p + st);
    if (format != 1 && format != 2)
        return true;
    if (!inside(t, st, 8))
        return false;
    if (u16(t.p + st + 4) != 4 || u16(t.p + st + 6) != 0)
        return true;
    if (format == 1) {
        if (!inside(t, st, 10) || !inside(t, st + 10, 2 * ((uint64_t)most + 1)))
            return false;
        uint32_t sets = u16(t.p + st + 8);
        if (!spend(c, (uint64_t)most + 1))
            return false;
        for (int64_t ci = 0; ci <= most; ci++) {
            uint64_t pv = st + u16(t.p + st + 10 + 2 * ci);
            if (!inside(t, pv, 2))
                return false;
            if (ci < sets && !inside(t, pv + 2, 4 * (uint64_t)u16(t.p + pv)))
                return false;
        }
        return true;
    }
    if (!inside(t, st, 16) || !class_def(c, t, st + u16(t.p + st + 8)) || !class_def(c, t, st + u16(t.p + st + 10)))
        return false;
    uint64_t c1 = u16(t.p + st + 12), c2 = u16(t.p + st + 14);
    return inside(t, st + 16, 2 * c1 * c2);
}

/* Marks offset at in a bit set of a table's offsets; false when it was
 * marked already. */
static bool first_time(uint8_t *set, uint64_t at)
{
    uint8_t bit = (uint8_t)(1u << (at % 8));
    if (set[at / 8] & bit)
        return false;
    set[at / 8] |= bit;
    return true;
}

/* GPOS 1.0's lookups of type 2, each lookup and subtable once: the
 * reads of stbtt__GetGlyphGPOSInfoAdvance for any two glyphs of the
 * font. */
static bool check_gpos(checker *c)
{
    table t = c->gpos;
    if (!t.p)
        return true;
    if (!inside(t, 0, 4))
        return fail(c, "has a GPOS table too short for its header");
    if (u16(t.p) != 1 || u16(t.p + 2) != 0)
        return true;
    if (!inside(t, 0, 10))
        return fail(c, "has a GPOS table too short for its header");
    uint64_t list = u16(t.p + 8);
    if (!inside(t, list, 2) || !inside(t, list + 2, 2 * (uint64_t)u16(t.p + list)))
        return fail(c, "has a GPOS lookup list past the end of its table");
    uint32_t lookups = u16(t.p + list);
    uint8_t *seen_lookups = sk_xcalloc((size_t)t.len / 8 + 1, 1), *seen_subs = sk_xcalloc((size_t)t.len / 8 + 1, 1);
    bool ok = true;
    for (uint32_t i = 0; i < lookups && ok; i++) {
        uint64_t lt = list + u16(t.p + list + 2 + 2 * i);
        if (!inside(t, lt, 6)) {
            ok = false;
            break;
        }
        if (u16(t.p + lt) != 2 || !first_time(seen_lookups, lt))
            continue;
        uint32_t subs = u16(t.p + lt + 4);
        ok = inside(t, lt + 6, 2 * (uint64_t)subs) && spend(c, subs);
        for (uint32_t k = 0; k < subs && ok; k++) {
            uint64_t st = lt + u16(t.p + lt + 6 + 2 * k);
            ok = st < t.len && (!first_time(seen_subs, st) || pair_pos(c, t, st));
        }
    }
    free(seen_lookups);
    free(seen_subs);
    if (!ok && c->work > BUDGET)
        return too_costly(c);
    return ok || fail(c, "has a GPOS pair adjustment that reads past the end of its table");
}

/* ---- name, OS/2, post ---- */

static bool check_name(checker *c)
{
    table t = c->name;
    if (!t.p)
        return true;
    if (!inside(t, 0, 6))
        return fail(c, "has a name table too short for its header");
    unsigned format = u16(t.p);
    uint32_t count = u16(t.p + 2), storage = u16(t.p + 4);
    if (format > 1 || !inside(t, 6, 12 * (uint64_t)count) || storage > t.len)
        return fail(c, "has a name table whose records run past its end");
    if (!spend(c, count))
        return too_costly(c);
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *r = t.p + 6 + 12 * i;
        if (!inside(t, (uint64_t)storage + u16(r + 10), u16(r + 8)))
            return fail(c, "has a name whose text runs past the end of its table");
    }
    if (format == 1) {
        uint64_t at = 6 + 12 * (uint64_t)count;
        if (!inside(t, at, 2) || !inside(t, at + 2, 4 * (uint64_t)u16(t.p + at)))
            return fail(c, "has a name table whose language tags run past its end");
        uint32_t tags = u16(t.p + at);
        if (!spend(c, tags))
            return too_costly(c);
        for (uint32_t i = 0; i < tags; i++) {
            const uint8_t *r = t.p + at + 2 + 4 * i;
            if (!inside(t, (uint64_t)storage + u16(r + 2), u16(r)))
                return fail(c, "has a language tag whose text runs past the end of its table");
        }
    }
    return true;
}

static bool check_os2(checker *c)
{
    table t = c->os2;
    if (!t.p)
        return true;
    if (!inside(t, 0, 2))
        return fail(c, "has an OS/2 table too short for its version");
    /* Version 0 as Apple first wrote it, 68 bytes; then 86, 96 and 100. */
    unsigned version = u16(t.p);
    uint32_t need = version == 0 ? 68 : version == 1 ? 86 : version <= 4 ? 96 : 100;
    if (t.len < need)
        return fail(c, "has an OS/2 table of version %u shorter than its %u bytes", version, need);
    return true;
}

static bool check_post(checker *c)
{
    table t = c->post;
    if (!t.p)
        return true;
    if (!inside(t, 0, 32))
        return fail(c, "has a post table shorter than its 32 bytes");
    uint32_t version = u32(t.p);
    if (version == 0x00025000) {
        if (!inside(t, 32, 2) || !inside(t, 34, u16(t.p + 32)))
            return fail(c, "has a post table whose glyph names run past its end");
    } else if (version == 0x00020000) {
        if (!inside(t, 32, 2) || !inside(t, 34, 2 * (uint64_t)u16(t.p + 32)))
            return fail(c, "has a post table whose glyph names run past its end");
        uint32_t count = u16(t.p + 32), names = 0, most = 0;
        for (uint32_t i = 0; i < count; i++)
            most = SK_MAX(most, (uint32_t)u16(t.p + 34 + 2 * i));
        /* Indexes from 258 name the Pascal strings after the array. */
        uint64_t at = 34 + 2 * (uint64_t)count;
        if (!spend(c, count + t.len - at))
            return too_costly(c);
        while (at < t.len) {
            at += 1 + (uint64_t)t.p[at];
            names++;
        }
        if (at > t.len || (most >= 258 && most - 258 >= names))
            return fail(c, "has a post table whose glyph names run past its end");
    }
    return true;
}

bool sk_font_check(const uint8_t *data, size_t n, char *err, size_t err_len)
{
    checker c = {.data = data, .n = n, .err = err, .err_len = err_len};
    if (err_len)
        err[0] = 0;
    bool ok = directory(&c) && check_metrics(&c) && check_glyphs(&c) && check_cmap(&c) && check_kern(&c) &&
              check_gpos(&c) && check_name(&c) && check_os2(&c) && check_post(&c);
    free(c.busy);
    free(c.flags);
    free(c.xs);
    free(c.ys);
    return ok;
}
