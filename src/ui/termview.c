/* The terminal drawn as xterm.js 6.0.0's WebGL renderer drew it for the
 * Electron app. Ported from @xterm/addon-webgl 0.19.0's WebglRenderer,
 * CellColorResolver, RectangleRenderer and LinkRenderLayer, from
 * @xterm/xterm 6.0.0's CharSizeService, ThemeService,
 * SelectionRenderModel and Mouse, and from @xterm/addon-fit 0.11.0, (c)
 * The xterm.js authors, MIT licence (third_party/xterm.js/LICENSE).
 *
 * A frame is xterm.js's in software: every cell's background, then every
 * cell's sprite from the atlas (atlas.c) over them, in cell order, then
 * the cursor's rectangles, then the hovered link's underline. A sprite
 * may reach 4 pixels past its row, so a row redrawn takes its
 * neighbours with it, and draws their sprites where they reach into it.
 *
 * The character's size is what CharSizeService measured in Chromium:
 * measureText('W').width, and the font's ascent and descent each
 * rounded, as Blink's FontMetrics rounds them. The device character is
 * the width times the scale rounded down and the height rounded up,
 * and with lineHeight 1 and letterSpacing 0 it is the cell. */
#include "internal.h"

#include <math.h>
#include <string.h>

/* FitAddon leaves room for the scrollbar, in CSS pixels
 * (ViewportConstants.DEFAULT_SCROLL_BAR_WIDTH). */
#define SCROLLBAR_CSS 14
/* How far a sprite reaches above and below its cell: the atlas canvas's
 * padding. */
#define SPRITE_REACH 4
#define MAX_CELL 1024
/* xterm.js's cursorWidth option: a bar's width in CSS pixels. */
#define CURSOR_WIDTH 1

typedef struct {
    const sk_ui_sprite *spr;
    sk_px bg;
    bool clip_left; /* the sprite cut at the cell's left edge */
} cell_paint;

typedef enum { SHAPE_BLOCK, SHAPE_OUTLINE, SHAPE_BAR, SHAPE_UNDERLINE } cursor_shape;

/* The cursor as the model drew it. */
typedef struct {
    bool visible;
    int x, y, width;
    cursor_shape shape;
} cursor_paint;

/* SelectionRenderModel, its rows in the viewport. */
typedef struct {
    bool has, column;
    int start_row, end_row, capped_start, capped_end, start_col, end_col;
} sel_model;

/* The theme as ThemeService resolves it, 0xRRGGBB. */
typedef struct {
    const sk_term_palette *pal;
    uint32_t fg, bg, cursor, accent, sel;
    bool sel_fg_set;
    uint32_t sel_fg;
} theme;

struct sk_termview {
    sk_termview_style style;
    sk_ui_atlas *atlas;
    int cell_w, cell_h;
    bool restyled;
    /* What the last draw drew. */
    bool drawn;
    int cols, rows, dst_w, dst_h;
    sk_term_palette pal;
    sk_termview_state st;
    cursor_paint cursor;
    sel_model sel;
    /* Each row's hovered-link underline, as a signature of its spans. */
    uint64_t *link_sig;
    /* Per frame. */
    sk_term_cell *cells;
    cell_paint *paint;
    bool *dirty, *redraw, *resolved, *reach_up, *reach_down;
    size_t cap_cells;
    int cap_rows;
};

/* ---- Sizes ---- */

static void measure(sk_termview *v)
{
    const sk_termview_style *s = &v->style;
    sk_glyph_ref w = sk_fontset_lookup(s->fonts, 'W', SK_STYLE_REGULAR);
    double width = sk_font_advance(w.font, w.glyph, s->font_px);
    sk_font_vmetrics vm;
    sk_font_vmetrics_at(sk_fontset_primary(s->fonts, SK_STYLE_REGULAR), s->font_px, &vm);
    double height = (double)floorf(vm.ascent + 0.5f) + (double)floorf(vm.descent + 0.5f);
    double cw = floor(width * s->scale), ch = ceil(height * s->scale);
    /* A cell is at least a pixel, and at most what a sane font makes. */
    v->cell_w = (int)fmin(fmax(cw, 1), MAX_CELL);
    v->cell_h = (int)fmin(fmax(ch, 1), MAX_CELL);
}

void sk_termview_default_style(sk_termview_style *out)
{
    memset(out, 0, sizeof *out);
    out->font_px = 14;
    out->scale = 1;
    /* rgba(255, 255, 255, 0.3), for the inactive selection too. */
    out->selection_bg = SK_RGBA(255, 255, 255, 0x4D);
    out->selection_inactive_bg = out->selection_bg;
    out->cursor_accent = SK_RGB(0, 0, 0);
}

sk_termview *sk_termview_new(const sk_termview_style *s)
{
    sk_termview *v = sk_xcalloc(1, sizeof *v);
    v->atlas = sk_ui_atlas_new();
    sk_termview_set_style(v, s);
    return v;
}

void sk_termview_free(sk_termview *v)
{
    if (!v)
        return;
    sk_ui_atlas_free(v->atlas);
    free(v->link_sig);
    free(v->cells);
    free(v->paint);
    free(v->dirty);
    free(v->redraw);
    free(v->resolved);
    free(v->reach_up);
    free(v->reach_down);
    free(v);
}

void sk_termview_set_style(sk_termview *v, const sk_termview_style *s)
{
    v->style = *s;
    if (!(v->style.scale > 0) || !isfinite(v->style.scale))
        v->style.scale = 1;
    if (!(v->style.font_px > 0) || !isfinite(v->style.font_px))
        v->style.font_px = 14;
    measure(v);
    v->restyled = true;
}

void sk_termview_cell(const sk_termview *v, int *w, int *h)
{
    *w = v->cell_w;
    *h = v->cell_h;
}

static int to_int(double d, int lo)
{
    if (!(d >= lo))
        return lo;
    return d > INT32_MAX ? INT32_MAX : (int)d;
}

void sk_termview_fit(const sk_termview *v, int w, int h, int *cols, int *rows)
{
    double scale = v->style.scale;
    /* The parent's computed size, parseInt'd, in CSS pixels. */
    double width = fmax(0, trunc(w / scale)), height = trunc(h / scale);
    double cell_w = v->cell_w / scale, cell_h = v->cell_h / scale;
    *cols = to_int(floor((width - SCROLLBAR_CSS) / cell_w), 2);
    *rows = to_int(floor(height / cell_h), 1);
}

void sk_termview_hit(const sk_termview *v, const sk_term *t, int x, int y, int *col, int *row, int *edge)
{
    int cols, rows;
    /* The lock is the only thing sk_term_size changes. */
    sk_term_size((sk_term *)t, &cols, &rows);
    int cw = v->cell_w, ch = v->cell_h;
    /* getMouseReportCoords: the point kept on the canvas. */
    long long cx = SK_MIN(SK_MAX((long long)x, 0LL), (long long)cols * cw - 1);
    long long cy = SK_MIN(SK_MAX((long long)y, 0LL), (long long)rows * ch - 1);
    *col = (int)(cx / cw);
    *row = (int)(cy / ch);
    /* getCoords for a selection: half a cell on, so a point selects up
     * to the nearer edge of its cell. */
    double e = ceil(((double)x + cw / 2.0) / cw);
    *edge = (int)fmin(fmax(e, 1), (double)cols + 1) - 1;
}

/* ---- Colours ---- */

/* rgba.blend: fg's alpha (0xAA of 0xAARRGGBB) over bg, each channel
 * rounded as Math.round. */
static uint32_t blend(uint32_t bg, uint32_t fg_rgb, uint32_t alpha)
{
    if (alpha == 255)
        return fg_rgb & 0xFFFFFF;
    double a = alpha / 255.0;
    uint32_t out = 0;
    for (int s = 16; s >= 0; s -= 8) {
        int b = (int)(bg >> s & 255), f = (int)(fg_rgb >> s & 255);
        double d = (double)(f - b) * a;
        out |= (uint32_t)(b + (int)sk_ui_js_round(d)) << s;
    }
    return out;
}

static uint32_t argb_over(uint32_t bg, sk_color c)
{
    return blend(bg, c & 0xFFFFFF, c >> 24);
}

static void theme_of(theme *th, const sk_term_palette *pal, const sk_termview_style *s, bool focused)
{
    th->pal = pal;
    th->fg = pal->fg & 0xFFFFFF;
    th->bg = pal->bg & 0xFFFFFF;
    th->cursor = pal->cursor & 0xFFFFFF;
    th->accent = argb_over(th->bg, s->cursor_accent);
    th->sel = argb_over(th->bg, focused ? s->selection_bg : s->selection_inactive_bg);
    th->sel_fg_set = s->selection_fg != 0;
    th->sel_fg = s->selection_fg & 0xFFFFFF;
}

static bool palette_mode(uint32_t word)
{
    uint32_t m = word & SK_ATTR_CM_MASK;
    return m == SK_ATTR_CM_P16 || m == SK_ATTR_CM_P256;
}

static bool rgb_mode(uint32_t word)
{
    return (word & SK_ATTR_CM_MASK) == SK_ATTR_CM_RGB;
}

/* A word's colour as RectangleRenderer finds it, dflt for the default. */
static uint32_t word_color(const theme *th, uint32_t word, uint32_t dflt)
{
    if (palette_mode(word))
        return th->pal->ansi[word & SK_ATTR_PCOLOR_MASK] & 0xFFFFFF;
    if (rgb_mode(word))
        return word & SK_ATTR_RGB_MASK;
    return dflt;
}

/* treatGlyphAsBackgroundColor: Powerline, box drawing and blocks. */
static bool glyph_as_background(uint32_t code)
{
    return (code >= 0xE0A4 && code <= 0xE0D6) || (code >= 0x2500 && code <= 0x259F);
}

typedef struct {
    uint32_t fg, bg, ext;
} words;

/* CellColorResolver.resolve for a selected cell: no decorations are
 * ever added, so the selection is the one override. */
static words selected_words(const theme *th, uint32_t fg, uint32_t bg, uint32_t ext, uint32_t code)
{
    const uint32_t RGB = SK_ATTR_RGB_MASK, CM = SK_ATTR_CM_MASK;
    bool inverse = fg & SK_FG_INVERSE, bg_default = (bg & CM) == SK_ATTR_CM_DEFAULT;
    uint32_t obg, ofg = 0;
    bool has_fg = false;
    if (inverse || !bg_default) {
        uint32_t base = inverse ? word_color(th, fg, th->fg) : word_color(th, bg, 0);
        obg = blend(base, th->sel, 0x80);
    } else {
        obg = th->sel;
    }
    if (th->sel_fg_set) {
        ofg = th->sel_fg;
        has_fg = true;
    }
    if (glyph_as_background(code)) {
        if (inverse && bg_default) {
            ofg = th->sel;
        } else {
            uint32_t base = inverse ? word_color(th, bg, 0) : word_color(th, fg, th->fg);
            ofg = blend(base, th->sel, 0x80);
        }
        has_fg = true;
    }
    words w;
    w.bg = (bg & ~RGB & ~SK_BG_DIM) | obg | SK_ATTR_CM_RGB;
    w.fg = has_fg ? (fg & ~RGB & ~SK_FG_INVERSE) | ofg | SK_ATTR_CM_RGB : fg;
    w.ext = ext;
    if (inverse && !has_fg) {
        uint32_t keep = fg & ~(RGB | SK_FG_INVERSE | CM);
        w.fg = bg_default ? keep | th->bg | SK_ATTR_CM_RGB : keep | (bg & (RGB | CM));
    }
    return w;
}

/* The background RectangleRenderer fills a cell with. */
static uint32_t cell_background(const theme *th, words w)
{
    if (w.fg & SK_FG_INVERSE)
        return word_color(th, w.fg, th->fg);
    return word_color(th, w.bg, th->bg);
}

/* ---- The selection ---- */

static void sel_of(sk_term *t, int rows, sel_model *out)
{
    memset(out, 0, sizeof *out);
    int x0, l0, x1, l1, top, base, lines;
    bool column = false;
    if (!sk_term_selection_range(t, &x0, &l0, &x1, &l1, &column) || (x0 == x1 && l0 == l1))
        return;
    sk_term_viewport(t, &top, &base, &lines);
    int vs = l0 - top, ve = l1 - top;
    int cs = SK_MAX(vs, 0), ce = SK_MIN(ve, rows - 1);
    if (cs >= rows || ce < 0)
        return;
    out->has = true;
    out->column = column;
    out->start_row = vs;
    out->end_row = ve;
    out->capped_start = cs;
    out->capped_end = ce;
    out->start_col = x0;
    out->end_col = x1;
}

static bool selected(const sel_model *s, int x, int y)
{
    if (!s->has)
        return false;
    if (s->column) {
        if (s->start_col <= s->end_col)
            return x >= s->start_col && y >= s->capped_start && x < s->end_col && y <= s->capped_end;
        return x < s->start_col && y >= s->capped_start && x >= s->end_col && y <= s->capped_end;
    }
    return (y > s->start_row && y < s->end_row) ||
           (s->start_row == s->end_row && y == s->start_row && x >= s->start_col && x < s->end_col) ||
           (s->start_row < s->end_row && y == s->end_row && x < s->end_col) ||
           (s->start_row < s->end_row && y == s->start_row && x >= s->start_col);
}

static void mark_sel(bool *rows, int n, const sel_model *s)
{
    if (!s->has)
        return;
    for (int y = SK_MAX(s->capped_start, 0); y <= SK_MIN(s->capped_end, n - 1); y++)
        rows[y] = true;
}

/* ---- Buffers ---- */

static void reserve(sk_termview *v, int cols, int rows)
{
    size_t cells = (size_t)cols * (size_t)rows;
    if (cells > v->cap_cells) {
        free(v->cells);
        free(v->paint);
        v->cells = sk_xreallocarray(NULL, cells, sizeof *v->cells);
        v->paint = sk_xreallocarray(NULL, cells, sizeof *v->paint);
        v->cap_cells = cells;
    }
    if (rows > v->cap_rows) {
        bool **flags[] = {&v->dirty, &v->redraw, &v->resolved, &v->reach_up, &v->reach_down};
        for (size_t i = 0; i < SK_LEN(flags); i++) {
            free(*flags[i]);
            *flags[i] = sk_xcalloc((size_t)rows, sizeof **flags[i]);
        }
        uint64_t *sig = sk_xcalloc((size_t)rows, sizeof *sig);
        if (v->link_sig)
            memcpy(sig, v->link_sig, (size_t)v->cap_rows * sizeof *sig);
        free(v->link_sig);
        v->link_sig = sig;
        v->cap_rows = rows;
    }
}

/* ---- The hovered link ---- */

static uint64_t sig_add(uint64_t h, int a, int b)
{
    h ^= (uint64_t)(uint32_t)a << 32 | (uint32_t)b;
    h *= 0x100000001b3ULL;
    return h ^ h >> 29;
}

/* Calls span for each run of row y the link underline covers; returns
 * their signature, 0 for none. */
static uint64_t link_spans(const sk_termview *v, const sk_termview_state *st, int y, int cols,
                           void (*span)(void *, int, int, int), void *ud)
{
    uint64_t h = 0;
    if (st->link_row0 >= 0 && y >= st->link_row0 && y <= st->link_row1) {
        int a = y == st->link_row0 ? st->link_col0 : 0;
        int b = y == st->link_row1 ? st->link_col1 : cols;
        a = SK_MAX(a, 0);
        b = SK_MIN(b, cols);
        if (b > a) {
            h = sig_add(h ^ 1, a, b);
            if (span)
                span(ud, y, a, b);
        }
    }
    if (st->link) {
        const sk_term_cell *row = &v->cells[(size_t)y * (size_t)cols];
        for (int x = 0; x < cols;) {
            if (row[x].link != st->link) {
                x++;
                continue;
            }
            int a = x;
            while (x < cols && row[x].link == st->link)
                x++;
            h = sig_add(h ^ 2, a, x);
            if (span)
                span(ud, y, a, x);
        }
    }
    return h;
}

/* ---- Painting ---- */

static void fill_px(sk_image *dst, sk_rect r, sk_px p)
{
    for (int y = r.y; y < r.y + r.h; y++) {
        sk_px *d = sk_image_row(dst, y) + r.x;
        for (int x = 0; x < r.w; x++)
            d[x] = p;
    }
}

/* A sprite's opaque pixels over dst within clip. */
static void blit(sk_image *dst, const sk_ui_sprite *s, int x, int y, sk_rect clip)
{
    sk_rect r = sk_rect_intersect(sk_rect_make(x, y, s->w, s->h), clip);
    if (sk_rect_empty(r))
        return;
    for (int j = r.y; j < r.y + r.h; j++) {
        const sk_px *src = s->px + (size_t)(j - y) * (size_t)s->w + (size_t)(r.x - x);
        sk_px *d = sk_image_row(dst, j) + r.x;
        for (int i = 0; i < r.w; i++) {
            uint32_t m = 0u - (src[i] >> 31);
            d[i] = (src[i] & m) | (d[i] & ~m);
        }
    }
}

/* A rectangle as WebGL fills it, without anti-aliasing: the pixels whose
 * centres it holds. */
static void fill_gl(sk_image *dst, double x, double y, double w, double h, sk_px p, sk_rect clip)
{
    int a = (int)ceil(x - 0.5), b = (int)ceil(x + w - 0.5);
    int c = (int)ceil(y - 0.5), d = (int)ceil(y + h - 0.5);
    sk_rect r = sk_rect_intersect(sk_rect_make(a, c, b - a, d - c), clip);
    if (!sk_rect_empty(r))
        fill_px(dst, r, p);
}

/* A rectangle as a 2D canvas fills it, anti-aliased. */
static void fill_aa(sk_image *dst, double x, double y, double w, double h, sk_color c, sk_rect clip)
{
    sk_path *p = sk_path_new();
    sk_path_rect(p, (float)x, (float)y, (float)w, (float)h);
    sk_fill_path(dst, p, NULL, SK_FILL_NONZERO, c, &clip);
    sk_path_free(p);
}

typedef struct {
    sk_image *dst;
    const sk_termview *v;
    sk_color c;
    sk_rect clip;
} link_ctx;

/* _fillBottomLineAtCells: a line one device pixel a scale high, a pixel
 * above the row's bottom. */
static void link_line(void *ud, int y, int a, int b)
{
    link_ctx *k = ud;
    double dpr = k->v->style.scale;
    int cw = k->v->cell_w, ch = k->v->cell_h;
    fill_aa(k->dst, (double)a * cw, (double)(y + 1) * ch - dpr - 1, (double)(b - a) * cw, dpr, k->c, k->clip);
}

/* ---- Drawing ---- */

static bool same_cursor(const cursor_paint *a, const cursor_paint *b)
{
    return a->visible == b->visible && a->x == b->x && a->y == b->y && a->width == b->width && a->shape == b->shape;
}

static bool same_sel(const sel_model *a, const sel_model *b)
{
    if (a->has != b->has)
        return false;
    return !a->has || (a->column == b->column && a->start_row == b->start_row && a->end_row == b->end_row &&
                       a->capped_start == b->capped_start && a->capped_end == b->capped_end &&
                       a->start_col == b->start_col && a->end_col == b->end_col);
}

/* The cursor as the model draws it this frame. */
static cursor_paint cursor_of(sk_term *t, const sk_termview_state *st, int cols, int rows,
                              const sk_term_cell *cells)
{
    cursor_paint p = {0};
    sk_term_cursor c;
    sk_term_cursor_of(t, &c);
    /* A blinking cursor rests shown while the terminal is not focused. */
    bool on = !c.blink || !st->focused || st->cursor_on;
    if (!c.initialized || c.hidden || !on || c.y < 0 || c.y >= rows)
        return p;
    p.visible = true;
    p.x = SK_MIN(c.x, cols - 1);
    p.y = c.y;
    p.width = cells[(size_t)p.y * (size_t)cols + (size_t)p.x].width;
    if (!st->focused)
        p.shape = SHAPE_OUTLINE;
    else
        p.shape = c.style == SK_CURSOR_BAR ? SHAPE_BAR : c.style == SK_CURSOR_UNDERLINE ? SHAPE_UNDERLINE : SHAPE_BLOCK;
    return p;
}

static void resolve_row(sk_termview *v, const theme *th, const sel_model *sel, const cursor_paint *cur, int y,
                        int cols)
{
    const sk_term_cell *row = &v->cells[(size_t)y * (size_t)cols];
    cell_paint *out = &v->paint[(size_t)y * (size_t)cols];
    double lw = fmax(1, floor(v->style.font_px * v->style.scale / 15));
    int dotted_period = (int)sk_ui_js_round(lw) * 2;
    uint32_t last_bg = 0;
    bool up = false, down = false;
    for (int x = 0; x < cols; x++) {
        const sk_term_cell *c = &row[x];
        uint32_t code = c->comb ? c->comb[c->ncomb - 1] : c->cp;
        uint32_t ext = 0;
        if (c->bg & SK_BG_HAS_EXTENDED) {
            ext = c->ext;
            if (c->link)
                ext = (ext & ~SK_EXT_UNDERLINE_STYLE) | (uint32_t)SK_UNDERLINE_DASHED << SK_EXT_UNDERLINE_SHIFT;
        }
        uint32_t ustyle = (ext & SK_EXT_UNDERLINE_STYLE) >> SK_EXT_UNDERLINE_SHIFT;
        uint32_t variant = code && ustyle == SK_UNDERLINE_DOTTED ? (uint32_t)(x * v->cell_w % dotted_period) : 0;
        words w = {c->fg, c->bg, ext};
        if (selected(sel, x, y))
            w = selected_words(th, c->fg, c->bg, ext, code);
        w.ext = (w.ext & 0x1FFFFFFFu) | (variant << 29 & 0xE0000000u);
        if (cur->visible && cur->shape == SHAPE_BLOCK && y == cur->y && x >= cur->x && x <= cur->x + cur->width - 1) {
            w.fg = SK_ATTR_CM_RGB | th->accent;
            w.bg = SK_ATTR_CM_RGB | th->cursor;
        }
        cell_paint *p = &out[x];
        p->bg = 0xFF000000u | cell_background(th, w);
        p->spr = NULL;
        bool decorated = (w.fg & (SK_FG_UNDERLINE | SK_FG_STRIKETHROUGH)) || (w.bg & SK_BG_OVERLINE) ||
                         ((w.bg & SK_BG_HAS_EXTENDED) && (w.ext & SK_EXT_UNDERLINE_STYLE));
        /* A blank with nothing on it draws nothing: skip the atlas. */
        if (code && (c->comb || code != ' ' || decorated)) {
            const uint32_t *cps = c->comb ? c->comb : &c->cp;
            int n = c->comb ? (int)c->ncomb : 1;
            p->spr = sk_ui_atlas_get(v->atlas, cps, n, SK_MAX(c->width, 1), w.fg, w.bg, w.ext);
        }
        p->clip_left = p->spr && w.bg != last_bg && p->spr->x < 0;
        if (p->spr) {
            up |= p->spr->y < 0;
            down |= p->spr->y + p->spr->h > v->cell_h;
        }
        last_bg = w.bg;
    }
    v->reach_up[y] = up;
    v->reach_down[y] = down;
}

static void paint_row(sk_termview *v, sk_image *dst, const theme *th, const sk_termview_state *st,
                      const cursor_paint *cur, int y, int cols, int rows, int reach)
{
    int cw = v->cell_w, ch = v->cell_h;
    sk_rect band = sk_rect_intersect(sk_rect_make(0, y * ch, cols * cw, ch), sk_image_bounds(dst));
    if (sk_rect_empty(band))
        return;
    const cell_paint *row = &v->paint[(size_t)y * (size_t)cols];
    for (int x = 0; x < cols;) {
        int a = x;
        sk_px p = row[x].bg;
        while (x < cols && row[x].bg == p)
            x++;
        sk_rect r = sk_rect_intersect(sk_rect_make(a * cw, y * ch, (x - a) * cw, ch), band);
        if (!sk_rect_empty(r))
            fill_px(dst, r, p);
    }
    for (int ny = SK_MAX(y - reach, 0); ny <= SK_MIN(y + reach, rows - 1); ny++) {
        if ((ny < y && !v->reach_down[ny]) || (ny > y && !v->reach_up[ny]))
            continue;
        const cell_paint *nrow = &v->paint[(size_t)ny * (size_t)cols];
        for (int x = 0; x < cols; x++) {
            const sk_ui_sprite *s = nrow[x].spr;
            if (!s)
                continue;
            sk_rect clip = band;
            if (nrow[x].clip_left)
                clip = sk_rect_intersect(clip, sk_rect_make(x * cw, band.y, band.x + band.w - x * cw, band.h));
            blit(dst, s, x * cw + s->x, ny * ch + s->y, clip);
        }
    }
    if (cur->visible && cur->y == y && cur->shape != SHAPE_BLOCK) {
        sk_px p = sk_premul(0xFF000000u | th->cursor);
        double dpr = v->style.scale, left = (double)cur->x * cw, top = (double)y * ch, w = (double)cur->width * cw;
        if (cur->shape == SHAPE_BAR || cur->shape == SHAPE_OUTLINE)
            fill_gl(dst, left, top, cur->shape == SHAPE_BAR ? dpr * CURSOR_WIDTH : dpr, ch, p, band);
        if (cur->shape == SHAPE_UNDERLINE || cur->shape == SHAPE_OUTLINE)
            fill_gl(dst, left, top + ch - dpr, w, dpr, p, band);
        if (cur->shape == SHAPE_OUTLINE) {
            fill_gl(dst, left, top, w, dpr, p, band);
            fill_gl(dst, (double)(cur->x + cur->width) * cw - dpr, top, dpr, ch, p, band);
        }
    }
    link_ctx k = {dst, v, 0xFF000000u | th->fg, band};
    link_spans(v, st, y, cols, link_line, &k);
}

sk_rect sk_termview_draw(sk_termview *v, sk_term *t, sk_image *dst, const sk_termview_state *st, bool all)
{
    sk_termview_state state = {false, true, 0, -1, 0, -1, 0};
    if (st)
        state = *st;
    sk_term_lock(t);
    int cols, rows;
    sk_term_size(t, &cols, &rows);
    reserve(v, cols, rows);
    sk_term_palette pal;
    sk_term_palette_of(t, &pal);
    bool recolor = !v->drawn || v->restyled || memcmp(&pal, &v->pal, sizeof pal) != 0;
    bool full = all || recolor || cols != v->cols || rows != v->rows || dst->w != v->dst_w || dst->h != v->dst_h;
    if (recolor) {
        sk_ui_atlas_config cfg = {v->style.fonts, v->style.cache, v->style.font_px, v->style.scale,
                                  v->cell_w,      v->cell_h,      v->cell_w,        v->cell_h,
                                  pal};
        sk_ui_atlas_configure(v->atlas, &cfg);
    } else {
        sk_ui_atlas_trim(v->atlas);
    }

    sk_term_take_dirty(t, v->dirty);
    for (int y = 0; y < rows; y++)
        sk_term_row(t, y, &v->cells[(size_t)y * (size_t)cols]);
    theme th;
    theme_of(&th, &pal, &v->style, state.focused);
    cursor_paint cur = cursor_of(t, &state, cols, rows, v->cells);
    sel_model sel;
    sel_of(t, rows, &sel);

    /* What changed besides the cells. */
    bool *dirty = v->dirty;
    if (full) {
        for (int y = 0; y < rows; y++)
            dirty[y] = true;
    } else {
        if (!same_cursor(&cur, &v->cursor)) {
            if (v->cursor.visible && v->cursor.y < rows)
                dirty[v->cursor.y] = true;
            if (cur.visible)
                dirty[cur.y] = true;
        }
        if (!same_sel(&sel, &v->sel) || (sel.has && state.focused != v->st.focused)) {
            mark_sel(dirty, rows, &v->sel);
            mark_sel(dirty, rows, &sel);
        }
    }
    for (int y = 0; y < rows; y++) {
        uint64_t sig = link_spans(v, &state, y, cols, NULL, NULL);
        if (sig != v->link_sig[y])
            dirty[y] = true;
        v->link_sig[y] = sig;
    }

    /* A row redrawn takes its neighbours, whose sprites may reach it; a
     * row painted needs its neighbours' sprites. */
    int reach = (SPRITE_REACH + v->cell_h - 1) / v->cell_h;
    for (int y = 0; y < rows; y++) {
        v->redraw[y] = false;
        for (int k = SK_MAX(y - reach, 0); k <= SK_MIN(y + reach, rows - 1) && !v->redraw[y]; k++)
            v->redraw[y] = dirty[k];
    }
    for (int y = 0; y < rows; y++) {
        v->resolved[y] = false;
        for (int k = SK_MAX(y - reach, 0); k <= SK_MIN(y + reach, rows - 1) && !v->resolved[y]; k++)
            v->resolved[y] = v->redraw[k];
        if (v->resolved[y])
            resolve_row(v, &th, &sel, &cur, y, cols);
    }
    int first = -1, last = -1;
    for (int y = 0; y < rows; y++) {
        if (!v->redraw[y])
            continue;
        paint_row(v, dst, &th, &state, &cur, y, cols, rows, reach);
        if (first < 0)
            first = y;
        last = y;
    }

    v->drawn = true;
    v->restyled = false;
    v->cols = cols;
    v->rows = rows;
    v->dst_w = dst->w;
    v->dst_h = dst->h;
    v->pal = pal;
    v->st = state;
    v->cursor = cur;
    v->sel = sel;
    sk_term_unlock(t);
    if (first < 0)
        return sk_rect_make(0, 0, 0, 0);
    sk_rect r = sk_rect_make(0, first * v->cell_h, cols * v->cell_w, (last - first + 1) * v->cell_h);
    r = sk_rect_intersect(r, sk_image_bounds(dst));
    return sk_rect_empty(r) ? sk_rect_make(0, 0, 0, 0) : r;
}
