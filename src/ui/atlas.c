/* xterm.js's texture atlas: each distinct cell, its characters and its
 * resolved attributes, drawn once on a canvas filled with its
 * background, as TextureAtlas._drawToCache draws it (the glyph, its
 * underline in five styles, overline, strikethrough, dim, bold in the
 * bright colours), the background then taken out and the rest kept as
 * a sprite of opaque pixels. Ported from @xterm/addon-webgl 0.19.0's
 * TextureAtlas.ts, (c) 2017 The xterm.js authors, MIT licence
 * (third_party/xterm.js/LICENSE).
 *
 * Its quirks are kept, since they are what the Electron app showed: a
 * sprite's edge pixels are blended over the cell's own background; the
 * lines after a shade are drawn in its pattern; strikethrough's half
 * pixel offset follows the line width set before it; underline colours
 * come from the foreground's colour mode when SGR 58 has not set one.
 * Text is drawn with stb_truetype, not Skia, and combining characters
 * at their base's pen, as there is no shaping. */
#include "internal.h"

#include <math.h>
#include <string.h>

/* TMP_CANVAS_GLYPH_PADDING * 2, the canvas's margin round a cell. */
#define PAD 4
/* DIM_OPACITY 0.5 times 255, rounded as color.opacity rounds it. */
#define DIM_ALPHA 128
/* The most a sprite may hold before the atlas starts over. */
#define BUDGET ((size_t)48 << 20)

typedef struct entry {
    uint64_t hash;
    uint32_t fg, bg, ext;
    int n, width;
    uint32_t cp;   /* the one character when n is 1 */
    uint32_t *cps; /* else n of them */
    bool empty;
    sk_ui_sprite spr;
    struct entry *next;
} entry;

struct sk_ui_atlas {
    sk_ui_atlas_config cfg;
    int descent[4]; /* each style's in device pixels, rounded as Chromium rounds it */
    entry **buckets;
    size_t nbuckets, count, bytes;
};

sk_ui_atlas *sk_ui_atlas_new(void)
{
    sk_ui_atlas *a = sk_xcalloc(1, sizeof *a);
    a->nbuckets = 1024;
    a->buckets = sk_xcalloc(a->nbuckets, sizeof *a->buckets);
    return a;
}

static void clear(sk_ui_atlas *a)
{
    for (size_t i = 0; i < a->nbuckets; i++) {
        for (entry *e = a->buckets[i], *next; e; e = next) {
            next = e->next;
            free(e->cps);
            free(e->spr.px);
            free(e);
        }
        a->buckets[i] = NULL;
    }
    a->count = a->bytes = 0;
}

void sk_ui_atlas_free(sk_ui_atlas *a)
{
    if (!a)
        return;
    clear(a);
    free(a->buckets);
    free(a);
}

void sk_ui_atlas_configure(sk_ui_atlas *a, const sk_ui_atlas_config *cfg)
{
    clear(a);
    a->cfg = *cfg;
    float px = (float)(cfg->font_px * cfg->scale);
    for (int s = 0; s < 4; s++) {
        sk_font_vmetrics vm;
        sk_font_vmetrics_at(sk_fontset_primary(cfg->fonts, s), px, &vm);
        a->descent[s] = (int)floorf(vm.descent + 0.5f);
    }
}

void sk_ui_atlas_trim(sk_ui_atlas *a)
{
    if (a->bytes > BUDGET)
        clear(a);
}

size_t sk_ui_atlas_count(const sk_ui_atlas *a)
{
    return a->count;
}

size_t sk_ui_atlas_bytes(const sk_ui_atlas *a)
{
    return a->bytes;
}

/* ---- Colours ---- */

/* An attribute word's colour: a palette index, or 0xRRGGBB. */
static uint32_t color_of(uint32_t word)
{
    return (word & SK_ATTR_CM_MASK) == SK_ATTR_CM_RGB ? word & SK_ATTR_RGB_MASK : word & SK_ATTR_PCOLOR_MASK;
}

static bool palette_mode(uint32_t mode)
{
    return mode == SK_ATTR_CM_P16 || mode == SK_ATTR_CM_P256;
}

/* _getBackgroundColor, opaque. */
static uint32_t background(const sk_ui_atlas_config *c, uint32_t mode, uint32_t color, bool inverse)
{
    if (palette_mode(mode))
        return c->pal.ansi[color];
    if (mode == SK_ATTR_CM_RGB)
        return color;
    return inverse ? c->pal.fg : c->pal.bg;
}

/* _getForegroundColor, with no minimum contrast (xterm.js's default). */
static sk_color foreground(const sk_ui_atlas_config *c, uint32_t mode, uint32_t color, bool inverse, bool bold,
                           bool dim)
{
    uint32_t rgb;
    if (palette_mode(mode))
        rgb = c->pal.ansi[bold && color < 8 ? color + 8 : color];
    else if (mode == SK_ATTR_CM_RGB)
        rgb = color;
    else
        rgb = inverse ? c->pal.bg : c->pal.fg;
    return (sk_color)(dim ? DIM_ALPHA : 255) << 24 | (rgb & 0xFFFFFF);
}

static sk_ui_paint solid(uint32_t rgb)
{
    sk_ui_paint p = {0xFF000000u | (rgb & 0xFFFFFF), NULL};
    return p;
}

/* ---- Lines ---- */

/* A horizontal line from x0 to x1 stroked lw wide about y, butt
 * capped: a rectangle. */
static void hline(sk_path *p, double x0, double x1, double y, double lw)
{
    if (x1 > x0)
        sk_path_rect(p, (float)x0, (float)(y - lw / 2), (float)(x1 - x0), (float)lw);
}

/* The same, dashed by setLineDash(dash): an odd list repeats twice, and
 * the pattern starts over at each moveTo. */
static void dashed(sk_path *p, double x0, double x1, double y, double lw, const double *dash, int n)
{
    double d[6], sum = 0;
    for (int i = 0; i < n; i++)
        d[i] = d[i + n] = dash[i];
    if (n % 2)
        n *= 2;
    for (int i = 0; i < n; i++)
        sum += d[i];
    if (!(sum > 0)) {
        hline(p, x0, x1, y, lw);
        return;
    }
    double at = x0;
    for (int k = 0; at < x1; k++) {
        double len = d[k % n];
        if (k % 2 == 0)
            hline(p, at, fmin(at + len, x1), y, lw);
        at += len;
    }
}

/* computeNextVariantOffset. */
static int next_variant(int cell_w, double lw, int current)
{
    int twice = (int)sk_ui_js_round(lw) * 2;
    return (cell_w - (twice - current)) % twice;
}

/* ---- Text ---- */

/* A character's glyph in style at the canvas's size, the pen on a whole
 * pixel as fillText's is. Valid until the next. */
static const sk_glyph *glyph(sk_ui_atlas *a, uint32_t cp, int style)
{
    const sk_ui_atlas_config *c = &a->cfg;
    return sk_glyph_get(c->glyphs, sk_fontset_lookup(c->fonts, cp, style), (float)(c->font_px * c->scale), 0);
}

/* Whether any glyph reaches below the baseline, as measureText's
 * actualBoundingBoxDescent > 0. */
static bool descends(sk_ui_atlas *a, const uint32_t *cps, int n, int style)
{
    for (int i = 0; i < n; i++) {
        const sk_glyph *g = glyph(a, cps[i], style);
        if (g->mask.w > 0 && g->top + g->mask.h > 0)
            return true;
    }
    return false;
}

/* fillText: the characters at the pen, combining ones over their base,
 * each glyph source-over in turn. */
static void text(sk_ui_atlas *a, sk_ui_canvas *cv, const uint32_t *cps, int n, int style, int x, int baseline,
                 sk_color c)
{
    for (int i = 0; i < n; i++) {
        const sk_glyph *g = glyph(a, cps[i], style);
        if (g->mask.w > 0 && g->mask.h > 0)
            sk_fill_mask(&cv->im, x + g->left, baseline + g->top, &g->mask, c, NULL);
    }
}

/* The glyphs' union, one byte a pixel of the canvas. */
static uint8_t *text_cover(sk_ui_atlas *a, const sk_ui_canvas *cv, const uint32_t *cps, int n, int style, int x,
                           int baseline)
{
    int w = cv->im.w, h = cv->im.h;
    uint8_t *cover = sk_xcalloc((size_t)w * (size_t)h, 1);
    for (int i = 0; i < n; i++) {
        const sk_glyph *g = glyph(a, cps[i], style);
        int gx = x + g->left, gy = baseline + g->top;
        for (int y = SK_MAX(gy, 0); y < SK_MIN(gy + g->mask.h, h); y++)
            for (int xx = SK_MAX(gx, 0); xx < SK_MIN(gx + g->mask.w, w); xx++) {
                uint8_t k = g->mask.a[(size_t)(y - gy) * (size_t)g->mask.stride + (size_t)(xx - gx)];
                uint8_t *o = &cover[(size_t)y * (size_t)w + (size_t)xx];
                *o = SK_MAX(*o, k);
            }
    }
    return cover;
}

/* The union spread by r pixels, as a stroke lineWidth 2r wide about the
 * outline reaches, within rows from..to. */
static uint8_t *dilate(const uint8_t *src, int w, int h, double r, int from, int to)
{
    uint8_t *out = sk_xcalloc((size_t)w * (size_t)h, 1);
    int k = (int)ceil(r);
    for (int y = SK_MAX(from, 0); y < SK_MIN(to, h); y++)
        for (int x = 0; x < w; x++) {
            int m = 0;
            for (int dy = -k; dy <= k; dy++)
                for (int dx = -k; dx <= k; dx++) {
                    int sx = x + dx, sy = y + dy;
                    if (sx < 0 || sy < 0 || sx >= w || sy >= h || dx * dx + dy * dy > r * r)
                        continue;
                    m = SK_MAX(m, src[(size_t)sy * (size_t)w + (size_t)sx]);
                }
            out[(size_t)y * (size_t)w + (size_t)x] = (uint8_t)m;
        }
    return out;
}

/* ---- clearColor ---- */

static void rgb_of(sk_px p, int *r, int *g, int *b)
{
    *r = (int)(p >> 16 & 255);
    *g = (int)(p >> 8 & 255);
    *b = (int)(p & 255);
}

/* The colour difference below which a pixel counts as background. */
static int threshold_of(uint32_t back, sk_color fore)
{
    int d = abs((int)(back >> 16 & 255) - (int)(fore >> 16 & 255)) +
            abs((int)(back >> 8 & 255) - (int)(fore >> 8 & 255)) + abs((int)(back & 255) - (int)(fore & 255));
    return d / 12;
}

/* Whether pixel p is the background, or near enough to it. */
static bool is_back(sk_px p, uint32_t back, int threshold, bool check)
{
    int r, g, b;
    rgb_of(p, &r, &g, &b);
    int br = (int)(back >> 16 & 255), bgr = (int)(back >> 8 & 255), bb = (int)(back & 255);
    if (r == br && g == bgr && b == bb)
        return true;
    return check && abs(r - br) + abs(g - bgr) + abs(b - bb) < threshold;
}

/* Whether the cell's part of the canvas is background throughout. */
static bool cell_empty(const sk_ui_canvas *cv, int x, int y, int w, int h, uint32_t back, int threshold, bool check)
{
    for (int j = SK_MAX(y, 0); j < SK_MIN(y + h, cv->im.h); j++)
        for (int i = SK_MAX(x, 0); i < SK_MIN(x + w, cv->im.w); i++)
            if (!is_back(sk_image_row(&cv->im, j)[i], back, threshold, check))
                return false;
    return true;
}

/* ---- Drawing a cell ---- */

static void draw(sk_ui_atlas *a, entry *e, const uint32_t *cps)
{
    const sk_ui_atlas_config *c = &a->cfg;
    uint32_t fg = e->fg, bg = e->bg, ext = e->ext;
    e->empty = true;
    if (fg & SK_FG_INVISIBLE)
        return;
    bool bold = fg & SK_FG_BOLD, inverse = fg & SK_FG_INVERSE, dim = bg & SK_BG_DIM, italic = bg & SK_BG_ITALIC;
    bool has_ext = bg & SK_BG_HAS_EXTENDED;
    int ustyle = (int)((ext & SK_EXT_UNDERLINE_STYLE) >> SK_EXT_UNDERLINE_SHIFT);
    bool underline = (has_ext && ustyle != SK_UNDERLINE_NONE) || (fg & SK_FG_UNDERLINE);
    bool strike = fg & SK_FG_STRIKETHROUGH, overline = bg & SK_BG_OVERLINE;
    uint32_t fgm = fg & SK_ATTR_CM_MASK, bgm = bg & SK_ATTR_CM_MASK, fgc = color_of(fg), bgc = color_of(bg);
    if (inverse) {
        uint32_t t = fgc;
        fgc = bgc;
        bgc = t;
        t = fgm;
        fgm = bgm;
        bgm = t;
    }
    uint32_t back = background(c, bgm, bgc, inverse);
    sk_color fore = foreground(c, fgm, fgc, inverse, bold, dim);

    int n = e->n, width = e->width, cw = c->cell_w, ch = c->cell_h, len16 = 0;
    uint32_t cp0 = cps[0];
    for (int i = 0; i < n; i++)
        len16 += cps[i] > 0xFFFF ? 2 : 1;
    bool single = len16 == 1;
    bool powerline = single && cp0 >= 0xE0A4 && cp0 <= 0xE0D6;
    bool restricted = single && cp0 >= 0xE0B0 && cp0 <= 0xE0B7;
    int pad = restricted ? 0 : PAD;
    int allowed = SK_MIN(cw * SK_MAX(len16, 2) + 4, 4096);
    int cvw = SK_MAX(cw * 4 + 4, allowed), cvh = ch + 8;
    sk_ui_canvas cv;
    sk_ui_canvas_init(&cv, cvw, cvh, 0xFF000000u | back);
    cv.fill.c = fore;
    int style = (bold ? SK_STYLE_BOLD : 0) | (italic ? SK_STYLE_ITALIC : 0);
    /* textBaseline ideographic: the font's rounded descent above the
     * character's bottom. */
    int baseline = pad + c->char_h - a->descent[style];
    double dpr = c->scale, font = c->font_px;

    bool custom = n == 1 && sk_ui_custom_draw(&cv, cp0, pad, pad, cw, ch, font, dpr);
    bool check = !powerline;

    if (underline) {
        double lw = fmax(1, floor(font * dpr / 15));
        double yoff = fmod(lw, 2) == 1 ? 0.5 : 0;
        sk_ui_paint paint = cv.fill;
        uint32_t umode = has_ext ? ext & SK_ATTR_CM_MASK : e->fg & SK_ATTR_CM_MASK;
        uint32_t ucolor = has_ext ? color_of(ext) : color_of(e->fg);
        if (umode == SK_ATTR_CM_RGB) {
            check = false;
            paint = solid(ucolor);
        } else if (umode != SK_ATTR_CM_DEFAULT) {
            check = false;
            paint = solid(c->pal.ansi[bold && ucolor < 8 ? ucolor + 8 : ucolor]);
        }
        double xl = pad, ytop = ceil(pad + c->char_h) - yoff;
        double ymid = ytop + lw, ybot = ytop + lw * 2;
        int next = (int)(ext >> 29 & 7);
        /* The path gathers every cell's lines and each cell strokes all
         * of them again, as the canvas's path is begun once. */
        sk_path *acc = sk_path_new();
        for (int i = 0; i < width; i++) {
            double xcl = xl + i * cw, xcr = xl + (i + 1) * cw, xcm = xcl + cw / 2.0;
            double clip[4];
            bool clipped = false;
            switch (ustyle) {
            case SK_UNDERLINE_DOUBLE:
                hline(acc, xcl, xcr, ytop, lw);
                hline(acc, xcl, xcr, ybot, lw);
                break;
            case SK_UNDERLINE_CURLY: {
                double ycb = lw <= 1 ? ybot : ceil(pad + c->char_h - lw / 2) - yoff;
                double yct = lw <= 1 ? ytop : ceil(pad + c->char_h + lw / 2) - yoff;
                clip[0] = xcl;
                clip[1] = ytop;
                clip[2] = xcl + cw;
                clip[3] = ybot;
                clipped = true;
                double half = cw / 2.0;
                sk_path *wave = sk_path_new();
                sk_path_move(wave, (float)(xcl - half), (float)ymid);
                sk_path_cubic(wave, (float)(xcl - half), (float)yct, (float)xcl, (float)yct, (float)xcl, (float)ymid);
                sk_path_cubic(wave, (float)xcl, (float)ycb, (float)xcm, (float)ycb, (float)xcm, (float)ymid);
                sk_path_cubic(wave, (float)xcm, (float)yct, (float)xcr, (float)yct, (float)xcr, (float)ymid);
                sk_path_cubic(wave, (float)xcr, (float)ycb, (float)(xcr + half), (float)ycb, (float)(xcr + half),
                              (float)ymid);
                sk_path_stroke(acc, wave, (float)lw, SK_CAP_BUTT, SK_JOIN_MITER, 10);
                sk_path_free(wave);
                break;
            }
            case SK_UNDERLINE_DOTTED: {
                double off = next == 0 ? 0 : next >= lw ? lw * 2 - next : lw - next;
                double dash[2] = {sk_ui_js_round(lw), sk_ui_js_round(lw)};
                if (next >= lw || off == 0) {
                    dashed(acc, xcl + off, xcr, ytop, lw, dash, 2);
                } else {
                    dashed(acc, xcl, xcl + off, ytop, lw, dash, 2);
                    dashed(acc, xcl + off + lw, xcr, ytop, lw, dash, 2);
                }
                next = next_variant(cw, lw, next);
                break;
            }
            case SK_UNDERLINE_DASHED: {
                double w = xcr - xcl;
                double line = floor(0.6 * w), gap = floor(0.3 * w);
                double dash[3] = {line, gap, w - line - gap};
                dashed(acc, xcl, xcr, ytop, lw, dash, 3);
                break;
            }
            default:
                hline(acc, xcl, xcr, ytop, lw);
                break;
            }
            sk_ui_canvas_fill(&cv, acc, paint, clipped ? clip : NULL);
        }
        sk_path_free(acc);
        /* The text stroked 3 device pixels wide in the background near
         * the line, so a descender stands clear of it. */
        if (!custom && font >= 12 && !(single && cp0 == ' ') && descends(a, cps, n, style)) {
            double up = ceil(lw / 2);
            double band[4] = {xl, ytop - up, xl + (double)cw * width, ytop - up + (ybot - ytop + up)};
            uint8_t *cover = text_cover(a, &cv, cps, n, style, pad, baseline);
            uint8_t *spread = dilate(cover, cvw, cvh, dpr * 3 / 2, (int)floor(band[1]), (int)ceil(band[3]));
            sk_ui_canvas_cover(&cv, spread, solid(back), band);
            free(spread);
            free(cover);
        }
    }

    if (overline) {
        double lw = fmax(1, floor(font * dpr / 15));
        double yoff = fmod(lw, 2) == 1 ? 0.5 : 0;
        cv.line_width = lw;
        sk_ui_canvas_rect(&cv, pad, pad + yoff - lw / 2, (double)c->char_w * width, lw, cv.fill);
    }

    if (!custom)
        text(a, &cv, cps, n, style, pad, baseline, fore);

    /* An underscore below the cell moves up until it shows, by up to 5
     * pixels. */
    int threshold = threshold_of(back, fore);
    if (single && cp0 == '_' && cell_empty(&cv, pad, pad, cw, ch, back, threshold, check)) {
        for (int off = 1; off <= 5; off++) {
            sk_fill_rect(&cv.im, sk_image_bounds(&cv.im), 0xFF000000u | back, SK_BLEND_SRC, NULL);
            text(a, &cv, cps, n, style, pad, baseline - off, fore);
            if (!cell_empty(&cv, pad, pad, cw, ch, back, threshold, check))
                break;
        }
    }

    if (strike) {
        double lw = fmax(1, floor(font * dpr / 10));
        double yoff = fmod(cv.line_width, 2) == 1 ? 0.5 : 0;
        cv.line_width = lw;
        double y = pad + floor(c->char_h / 2.0) - yoff;
        sk_ui_canvas_rect(&cv, pad, y - lw / 2, (double)c->char_w * width, lw, cv.fill);
    }

    /* clearColor: the background, and what is near it, made clear. */
    uint8_t *keep = sk_xcalloc((size_t)cvw * (size_t)cvh, 1);
    bool any = false;
    for (int y = 0; y < cvh; y++) {
        const sk_px *row = sk_image_row(&cv.im, y);
        for (int x = 0; x < cvw; x++) {
            if (!is_back(row[x], back, threshold, check)) {
                keep[(size_t)y * (size_t)cvw + (size_t)x] = 1;
                any = true;
            }
        }
    }
    if (any) {
        /* _findGlyphBoundingBox, its scans kept as they are. */
        int sw = restricted ? cw : allowed, sh = restricted ? ch : cvh;
        int top = 0, left = 0, right = sw, bottom = sh;
#define KEPT(x, y) ((x) < cvw && (y) < cvh && keep[(size_t)(y) * (size_t)cvw + (size_t)(x)])
        bool found = false;
        for (int y = 0; y < sh && !found; y++)
            for (int x = 0; x < sw && !found; x++)
                if (KEPT(x, y))
                    top = y, found = true;
        found = false;
        for (int x = 0; x < pad + sw && !found; x++)
            for (int y = 0; y < sh && !found; y++)
                if (KEPT(x, y))
                    left = x, found = true;
        found = false;
        for (int x = pad + sw - 1; x >= pad && !found; x--)
            for (int y = 0; y < sh && !found; y++)
                if (KEPT(x, y))
                    right = x, found = true;
        found = false;
        for (int y = sh - 1; y >= 0 && !found; y--)
            for (int x = 0; x < sw && !found; x++)
                if (KEPT(x, y))
                    bottom = y, found = true;
        int w = right - left + 1, h = bottom - top + 1;
        if (w > 0 && h > 0) {
            sk_px *px = sk_xcalloc((size_t)w * (size_t)h, sizeof *px);
            bool drawn = false;
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++)
                    if (KEPT(left + x, top + y)) {
                        px[(size_t)y * (size_t)w + (size_t)x] = sk_image_row(&cv.im, top + y)[left + x];
                        drawn = true;
                    }
#undef KEPT
            if (drawn) {
                int adjust = restricted || custom ? (cw - c->char_w) / 2 : 0;
                e->spr.x = left - pad - adjust;
                e->spr.y = top - pad;
                e->spr.w = w;
                e->spr.h = h;
                e->spr.px = px;
                e->empty = false;
                a->bytes += (size_t)w * (size_t)h * sizeof *px;
            } else {
                free(px);
            }
        }
    }
    free(keep);
    sk_ui_canvas_free(&cv);
}

/* ---- The cache ---- */

static uint64_t mix(uint64_t h, uint64_t v)
{
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

static uint64_t hash_of(const uint32_t *cps, int n, int width, uint32_t fg, uint32_t bg, uint32_t ext)
{
    uint64_t h = (uint64_t)n << 8 | (uint64_t)width;
    for (int i = 0; i < n; i++)
        h = mix(h, cps[i]);
    h = mix(h, (uint64_t)fg << 32 | bg);
    h = mix(h, ext);
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    return h;
}

static bool same(const entry *e, uint64_t hash, const uint32_t *cps, int n, int width, uint32_t fg, uint32_t bg,
                 uint32_t ext)
{
    if (e->hash != hash || e->n != n || e->width != width || e->fg != fg || e->bg != bg || e->ext != ext)
        return false;
    if (n == 1)
        return e->cp == cps[0];
    return memcmp(e->cps, cps, (size_t)n * sizeof *cps) == 0;
}

static void grow(sk_ui_atlas *a)
{
    size_t nb = a->nbuckets * 2;
    entry **b = sk_xcalloc(nb, sizeof *b);
    for (size_t i = 0; i < a->nbuckets; i++)
        for (entry *e = a->buckets[i], *next; e; e = next) {
            next = e->next;
            size_t k = (size_t)(e->hash & (nb - 1));
            e->next = b[k];
            b[k] = e;
        }
    free(a->buckets);
    a->buckets = b;
    a->nbuckets = nb;
}

const sk_ui_sprite *sk_ui_atlas_get(sk_ui_atlas *a, const uint32_t *cps, int n, int width, uint32_t fg, uint32_t bg,
                                    uint32_t ext)
{
    if (n <= 0 || !cps)
        return NULL;
    uint64_t hash = hash_of(cps, n, width, fg, bg, ext);
    for (entry *e = a->buckets[hash & (a->nbuckets - 1)]; e; e = e->next)
        if (same(e, hash, cps, n, width, fg, bg, ext))
            return e->empty ? NULL : &e->spr;
    entry *e = sk_xcalloc(1, sizeof *e);
    e->hash = hash;
    e->fg = fg;
    e->bg = bg;
    e->ext = ext;
    e->n = n;
    e->width = width;
    e->cp = cps[0];
    if (n > 1) {
        e->cps = sk_xreallocarray(NULL, (size_t)n, sizeof *cps);
        memcpy(e->cps, cps, (size_t)n * sizeof *cps);
    }
    draw(a, e, cps);
    if (a->count >= a->nbuckets)
        grow(a);
    size_t k = (size_t)(hash & (a->nbuckets - 1));
    e->next = a->buckets[k];
    a->buckets[k] = e;
    a->count++;
    a->bytes += sizeof *e + (n > 1 ? (size_t)n * sizeof *cps : 0);
    return e->empty ? NULL : &e->spr;
}
