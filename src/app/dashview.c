/* Port of app/renderer/dashboard.js and the .dashboard rules of
 * app/renderer/app.css (decisions 0028, 0029 and 0033). The page laid
 * the pane out with CSS and drew its graphs on canvases; here both are
 * done by hand, in device pixels, as Chromium lays a page out zoomed by
 * its device scale: every CSS length times the scale, a font's ascent
 * and descent rounded at its device size, a border's width floored to
 * whole pixels, positions kept in 64ths of a pixel as LayoutUnit keeps
 * them, and each box snapped to whole pixels when it is painted.
 *
 * The pane is a block: the head line, the sums across sessions (a
 * flexbox of three equal charts), then the rows, each a flexbox whose
 * folder alone grows and shrinks. Text wraps at its spaces where CSS
 * wraps it (the head line, the charts' titles, the empty pane's note)
 * and is cut with an ellipsis where text-overflow cuts it. An element
 * with an opacity is drawn into a layer composited at it, unless it
 * holds text alone, whose colour then takes the opacity: the same
 * pixels. A canvas is drawn in place, at its bitmap's size from its
 * box's snapped corner. */
#include "dashview.h"

#include "../base/strv.h"
#include "../base/utf8.h"

#include <math.h>
#include <string.h>

/* app.css, in CSS pixels. */
#define PAD_X 8          /* the pane's padding: 6px 8px */
#define PAD_Y 6
#define FONT_PX 12       /* 12px/1.5 DejaVu Sans Mono */
#define SMALL_PX 11      /* where a rule says 11px */
#define HEAD_GAP 6       /* .d-head's margin-bottom */
#define ACROSS_GAP 10    /* .d-across's gap and margin-bottom */
#define CHART_PAD_X 6    /* .d-chart's padding: 3px 6px */
#define CHART_PAD_Y 3
#define CHART_H 56       /* its canvas's height */
#define ROW_GAP 8        /* .d-row's gap */
#define ROW_PAD 2        /* .d-row's padding: 2px 0 */
#define LABEL_W 110
#define FOLDER_MIN 60    /* .d-folder: flex 1 1 120px, min-width 60px */
#define STATE_W 58
#define SPARK_W 96
#define DIAL_W 118
#define BOARD_W 104
#define CELL_GAP 4       /* .d-spark's and .d-board's gap */
#define SPARK_CANVAS_W 54
#define BOARD_CANVAS_W 46
#define CANVAS_H 20
#define NONE_PAD 6       /* .d-none's padding: 6px 0 */

/* Opacities as alpha, 255 times each rounded. */
#define OP_ENDED 102     /* .d-row.ended, 0.4 */
#define OP_HEADER 153    /* .d-header, 0.6 */
#define OP_DIM 179       /* .d-title and .d-none, 0.7 */
#define OP_FOLDER 191    /* .d-folder, 0.75 */
#define OP_NUM 217       /* .d-num, 0.85 */
#define OP_AREA 46       /* spark()'s globalAlpha, 0.18 */

#define BORDER SK_RGBA(0xff, 0xff, 0xff, 0x22)
#define RULE SK_RGBA(0xff, 0xff, 0xff, 0x14)
#define LIT SK_RGBA(0xff, 0xff, 0xff, 0x10)
#define WORKING SK_RGB(0xe0, 0xb0, 0x50)
#define WAITING SK_RGB(0x8f, 0xd1, 0x8f)
#define BOARD_COLOR SK_RGB(0xc8, 0xc8, 0xc8)
#define DIAL_COLOR SK_RGB(0x9f, 0xd3, 0xc7)

/* A width compared with room to spare, LayoutUnit's 64th. */
#define EPS (1.0 / 64)

#define SPARKS 5
#define SUMS 3

/* SPARKS and COLORS of dashboard.js: the meters' columns. */
static const struct {
    const char *key, *name;
    sk_color color;
} meters[SPARKS] = {
    {"tools", "tool calls", SK_RGB(0xe0, 0xb0, 0x50)},
    {"tokens", "output tokens", SK_RGB(0x6f, 0xa8, 0xdc)},
    {"cost", "cost", SK_RGB(0x8f, 0xd1, 0x8f)},
    {"context", "context", SK_RGB(0xd4, 0x8a, 0xd8)},
    {"compactions", "compactions", SK_RGB(0xe0, 0x7a, 0x6a)},
};

/* ACROSS: each sum's series, its title, and the meter whose colour and
 * format it takes. */
static const struct {
    const char *key, *title, *meter;
    sk_color color;
} sums[SUMS] = {
    {"tokensPerMin", "output tokens / min", "tokens", SK_RGB(0x6f, 0xa8, 0xdc)},
    {"toolsPerMin", "tool calls / min", "tools", SK_RGB(0xe0, 0xb0, 0x50)},
    {"cost", "cost, all sessions", "cost", SK_RGB(0x8f, 0xd1, 0x8f)},
};

static const char ELLIPSIS[] = "\xe2\x80\xa6";
static const char NONE[] = "No agent session is running.";

/* A line of text at a size as Blink sets it: the em in device pixels,
 * the line box line-height: 1.5 high, and the baseline below its top:
 * half the leading, then the ascent, the ascent and descent each rounded
 * at that size. */
typedef struct {
    float px;
    double line, base;
} font_line;

typedef struct {
    size_t at, n;
} span;
typedef SK_VEC(span) spans;
typedef SK_VEC(double) doubles;

/* The pane laid out, in device pixels from the content's top-left
 * unscrolled (the padding's corner, as the pane's own box). */
typedef struct {
    double s, bw;    /* the scale; a 1px border's width */
    double w, h, cw; /* the area, and the content box's width */
    font_line text, bold, small;
    sk_buf head;     /* the head line's text */
    size_t head_lines, title_lines[SUMS], none_lines;
    double across_y, chart_w, chart_cw, chart_h;
    double header_y, header_h, rows_y, row_h, rows_end, height, max_scroll;
    doubles col_x, col_w; /* a row's items */
} layout;

struct sk_dashview {
    sk_arena arena;
    const sk_json *data; /* the last view, NULL before the first */
    sk_strv dials;       /* its dials' names */
    /* What the last draw had, which scroll, hover and hit measure with. */
    bool laid;
    sk_dashview_style style;
    int area_w, area_h;
    layout lay;
    double scroll; /* points */
    bool pointer;
    double px, py; /* the pointer, points from the area's corner */
    long lit;      /* the live row under it, -1 for none */
    sk_image scratch; /* what layers are drawn in */
    sk_path *path, *outline;
};

/* ---- The view's JSON, read as JavaScript reads it ---- */

static size_t json_count(const sk_json *a)
{
    return a && a->type == SK_JSON_ARRAY ? a->array.len : 0;
}

static const sk_json *rows_of(const sk_dashview *v)
{
    return sk_json_get(v->data, "rows");
}

static bool truthy(const sk_json *v)
{
    if (!v)
        return false;
    switch (v->type) {
    case SK_JSON_BOOL:
        return v->boolean;
    case SK_JSON_NUMBER:
        return v->number != 0 && !isnan(v->number);
    case SK_JSON_STRING:
        return v->string.len > 0;
    case SK_JSON_ARRAY:
    case SK_JSON_OBJECT:
        return true;
    default:
        return false;
    }
}

static bool is_string(const sk_json *v, const char *s)
{
    return v && v->type == SK_JSON_STRING && sk_str_eq_c(v->string, s);
}

/* A number as String() writes it. */
static void js_number(sk_buf *out, double d)
{
    if (isnan(d))
        sk_buf_append_c(out, "NaN");
    else if (isinf(d))
        sk_buf_append_c(out, d > 0 ? "Infinity" : "-Infinity");
    else
        sk_json_format_number(out, d);
}

/* Math.round: half up, toward +Infinity. */
static double js_round(double v)
{
    double f = floor(v);
    return v - f >= 0.5 ? f + 1 : f;
}

/* Number.prototype.toFixed(digits), 0 to 2: the exact binary value
 * rounded to the nearest, a tie to the larger magnitude where printf
 * takes the even digit. */
static void to_fixed(sk_buf *out, double v, int digits)
{
    if (!(fabs(v) < 1e21)) {
        js_number(out, v);
        return;
    }
    double p = digits == 0 ? 1 : digits == 1 ? 10 : 100;
    double a = fabs(v), t = a * p, f = floor(t);
    if (t - f == 0.5 && fma(a, p, -t) == 0) {
        sk_buf_printf(out, "%s%.*f", v < 0 ? "-" : "", digits, (f + 1) / p);
        return;
    }
    sk_buf_printf(out, "%.*f", digits, v == 0 ? 0.0 : v);
}

void sk_dashview_format(sk_buf *out, const char *key, double value)
{
    if (isnan(value)) {
        sk_buf_append_c(out, "--");
    } else if (strcmp(key, "cost") == 0) {
        sk_buf_putc(out, '$');
        to_fixed(out, value, 2);
    } else if (strcmp(key, "context") == 0) {
        to_fixed(out, value, 0);
        sk_buf_putc(out, '%');
    } else if (value >= 1e6) {
        to_fixed(out, value / 1e6, 2);
        sk_buf_putc(out, 'M');
    } else if (value >= 1e3) {
        to_fixed(out, value / 1e3, 1);
        sk_buf_putc(out, 'k');
    } else {
        js_number(out, js_round(value));
    }
}

/* What textContent shows of a value el() was given: nothing for null or
 * none. */
static void text_of(sk_buf *out, const sk_json *v)
{
    if (!v)
        return;
    switch (v->type) {
    case SK_JSON_STRING:
        sk_buf_append_str(out, v->string);
        break;
    case SK_JSON_NUMBER:
        js_number(out, v->number);
        break;
    case SK_JSON_BOOL:
        sk_buf_append_c(out, v->boolean ? "true" : "false");
        break;
    case SK_JSON_OBJECT:
        sk_buf_append_c(out, "[object Object]");
        break;
    default:
        break;
    }
}

/* A value in a template literal: undefined and null spelled out. */
static void template_of(sk_buf *out, const sk_json *v)
{
    if (!v)
        sk_buf_append_c(out, "undefined");
    else if (v->type == SK_JSON_NULL)
        sk_buf_append_c(out, "null");
    else
        text_of(out, v);
}

/* text_of with white-space collapsed: each run of spaces, tabs and line
 * breaks one space, none at either end. */
static void plain(sk_buf *out, const sk_json *v)
{
    sk_buf_clear(out);
    text_of(out, v);
    size_t o = 0;
    bool space = false;
    for (size_t i = 0; i < out->len; i++) {
        char c = out->ptr[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') {
            space = o > 0;
            continue;
        }
        if (space)
            out->ptr[o++] = ' ';
        space = false;
        out->ptr[o++] = c;
    }
    out->len = o;
    if (out->ptr)
        out->ptr[o] = '\0';
}

/* last() of dashboard.js: the newest number, NAN for none. */
static double last_of(const sk_json *s)
{
    for (size_t i = json_count(s); i-- > 0;)
        if (s->array.items[i].type == SK_JSON_NUMBER)
            return s->array.items[i].number;
    return NAN;
}

/* What a row's board cell reads: nothing for a session with no project,
 * "..." while it loads, "no access", or the issues open now and closed
 * in 14 days. True for the last, which has a sparkline. */
static bool board_text(const sk_json *b, sk_buf *out)
{
    sk_buf_clear(out);
    if (!truthy(b))
        return false;
    const sk_json *state = sk_json_get(b, "state");
    if (!is_string(state, "ok")) {
        sk_buf_append_c(out, is_string(state, "loading") ? "..." : "no access");
        return false;
    }
    template_of(out, sk_json_get(b, "openNow"));
    sk_buf_append_c(out, " / ");
    template_of(out, sk_json_get(b, "closed14"));
    return true;
}

/* The head line render() writes. */
static void head_text(const sk_json *data, sk_buf *out)
{
    sk_buf_clear(out);
    sk_buf_append_c(out, "Sessions dashboard");
    if (!data)
        return;
    const sk_json *rows = sk_json_get(data, "rows");
    size_t n = json_count(rows), live = 0;
    for (size_t i = 0; i < n; i++)
        live += !truthy(sk_json_get(&rows->array.items[i], "ended"));
    const sk_json *iv = sk_json_get(data, "intervalMs");
    double interval = iv && iv->type == SK_JSON_NUMBER ? iv->number : NAN;
    double mins = js_round((double)json_count(sk_json_get(data, "ticks")) * interval / 60000);
    sk_buf_printf(out, ": %zu live, %zu ended, the last ", live, n - live);
    if (mins < 1) {
        sk_buf_append_c(out, "minute");
    } else {
        js_number(out, mins);
        sk_buf_append_c(out, " min");
    }
    sk_buf_append_c(out, ", sampled every ");
    js_number(out, interval / 1000);
    sk_buf_append_c(out, " s");
}

/* ---- Layout ---- */

/* LayoutUnit's 64ths, truncated. */
static double unit(double v)
{
    return trunc(v * 64) / 64;
}

static font_line line_of(sk_fontset *set, int style, double px)
{
    sk_font_vmetrics m;
    sk_font_vmetrics_at(sk_fontset_primary(set, style), (float)px, &m);
    double ascent = floor(m.ascent + 0.5), descent = floor(m.descent + 0.5), line = unit(px * 1.5);
    return (font_line){(float)px, line, unit((line - ascent - descent) / 2) + ascent};
}

/* s broken into lines at most maxw wide at its spaces, as white-space:
 * normal breaks it; a word wider than maxw keeps a line of its own. The
 * lines into out when it is not NULL; returns how many. */
static size_t wrap(sk_fontset *set, int style, float px, const char *s, double maxw, spans *out)
{
    size_t n = strlen(s), lines = 0, i = 0;
    for (;;) {
        while (i < n && s[i] == ' ')
            i++;
        if (i >= n)
            return lines;
        size_t end = i;
        for (size_t j = i; j < n;) {
            size_t e = j;
            while (e < n && s[e] == ' ')
                e++;
            if (e == n)
                break;
            while (e < n && s[e] != ' ')
                e++;
            if (end > i && sk_measure_text(set, style, px, s + i, e - i) > maxw + EPS)
                break;
            end = j = e;
        }
        if (out) {
            span sp = {i, end - i};
            sk_vec_push(out, sp);
        }
        lines++;
        i = end;
    }
}

/* A row's items as its flexbox lays them out, 8px apart from the
 * content's left: label, folder, state, the meters, the dials and the
 * board, all fixed but the folder, which takes what is left, 60px at
 * least. What passes the right edge overflows it. */
static void columns(layout *l, size_t dials)
{
    double s = l->s, items = SPARKS + (double)dials + 4;
    double fixed = LABEL_W + STATE_W + SPARKS * SPARK_W + (double)dials * DIAL_W + BOARD_W + ROW_GAP * (items - 1);
    double folder = SK_MAX(FOLDER_MIN * s, l->cw - fixed * s);
    l->col_x.len = l->col_w.len = 0;
    double x = PAD_X * s;
    for (size_t i = 0; i < (size_t)items; i++) {
        double w = i == 0 ? LABEL_W * s
                 : i == 1 ? folder
                 : i == 2 ? STATE_W * s
                 : i < 3 + SPARKS ? SPARK_W * s
                 : i + 1 < (size_t)items ? DIAL_W * s
                 : BOARD_W * s;
        sk_vec_push(&l->col_x, x);
        sk_vec_push(&l->col_w, w);
        x += w + ROW_GAP * s;
    }
}

static void lay_out(sk_dashview *v)
{
    layout *l = &v->lay;
    const sk_dashview_style *st = &v->style;
    double s = st->scale > 0 && isfinite(st->scale) ? st->scale : 1;
    l->s = s;
    l->bw = s < 1 ? 1 : floor(s + 1e-9);
    l->w = v->area_w;
    l->h = v->area_h;
    l->cw = SK_MAX(0, l->w - 2 * PAD_X * s);
    l->text = line_of(st->fonts, SK_STYLE_REGULAR, FONT_PX * s);
    l->bold = line_of(st->fonts, SK_STYLE_BOLD, FONT_PX * s);
    l->small = line_of(st->fonts, SK_STYLE_REGULAR, SMALL_PX * s);
    head_text(v->data, &l->head);
    l->head_lines = wrap(st->fonts, SK_STYLE_BOLD, l->bold.px, l->head.ptr, l->cw, NULL);
    l->across_y = PAD_Y * s + (double)l->head_lines * l->bold.line + HEAD_GAP * s;
    /* Three boxes of flex: 1 1 0, 10px apart: what is left once their
     * borders and padding are in, in thirds. */
    double edge = 2 * l->bw + 2 * CHART_PAD_X * s;
    l->chart_cw = SK_MAX(0, (l->cw - 2 * ACROSS_GAP * s - SUMS * edge) / SUMS);
    l->chart_w = l->chart_cw + edge;
    size_t most = 0;
    for (size_t i = 0; i < SUMS; i++) {
        l->title_lines[i] = wrap(st->fonts, SK_STYLE_REGULAR, l->small.px, sums[i].title, l->chart_cw, NULL);
        most = SK_MAX(most, l->title_lines[i]);
    }
    bool data = v->data != NULL;
    l->chart_h = data ? 2 * l->bw + 2 * CHART_PAD_Y * s + (double)most * l->small.line + CHART_H * s : 0;
    l->header_y = l->across_y + l->chart_h + ACROSS_GAP * s;
    l->header_h = data ? 2 * ROW_PAD * s + l->small.line + l->bw : 0;
    l->rows_y = l->header_y + l->header_h;
    l->row_h = 2 * ROW_PAD * s + SK_MAX(l->text.line, CANVAS_H * s) + l->bw;
    size_t rows = json_count(rows_of(v));
    l->none_lines = 0;
    if (rows) {
        l->rows_end = l->rows_y + (double)rows * l->row_h;
    } else if (data) {
        l->none_lines = wrap(st->fonts, SK_STYLE_REGULAR, l->text.px, NONE, l->cw, NULL);
        l->rows_end = l->rows_y + 2 * NONE_PAD * s + (double)l->none_lines * l->text.line;
    } else {
        l->rows_end = l->rows_y;
    }
    l->height = l->rows_end + PAD_Y * s;
    l->max_scroll = SK_MAX(0, floor(l->height + 0.5) - l->h);
    columns(l, v->dials.len);
}

/* The scroll in device pixels, whole ones, within the rows. */
static double scroll_px(const sk_dashview *v)
{
    return SK_MIN(floor(v->scroll * v->lay.s + 0.5), v->lay.max_scroll);
}

static void clamp_scroll(sk_dashview *v)
{
    double most = v->laid ? v->lay.max_scroll / v->lay.s : 0;
    v->scroll = SK_MAX(0, SK_MIN(v->scroll, most));
}

/* The row whose box holds (x, y) points from the area's corner, or -1:
 * across from the content's left to its right or past it where the
 * row's items overflow it, and never outside the pane. */
static long row_at(const sk_dashview *v, double x, double y)
{
    if (!v->laid || !v->data)
        return -1;
    const layout *l = &v->lay;
    double s = l->s, dx = x * s, dy = y * s;
    if (!(dx >= 0 && dy >= 0 && dx < l->w && dy < l->h))
        return -1;
    dy += scroll_px(v);
    size_t n = json_count(rows_of(v)), last = l->col_x.len - 1;
    double right = SK_MAX(PAD_X * s + l->cw, l->col_x.ptr[last] + l->col_w.ptr[last]);
    if (!n || dy < l->rows_y || dx < PAD_X * s || dx >= right)
        return -1;
    double k = floor((dy - l->rows_y) / l->row_h);
    return k < (double)n ? (long)k : -1;
}

static const sk_json *row(const sk_dashview *v, long i)
{
    return sk_json_at(rows_of(v), (size_t)i);
}

/* The live row under the pointer, -1 for none. */
static long lit_row(const sk_dashview *v)
{
    if (!v->pointer)
        return -1;
    long i = row_at(v, v->px, v->py);
    return i >= 0 && !truthy(sk_json_get(row(v, i), "ended")) ? i : -1;
}

/* ---- Drawing ---- */

/* Where drawing goes: an image, the content's corner in it, scrolled,
 * and the pane's part of it. */
typedef struct {
    sk_image *im;
    int ox, oy;
    sk_rect clip;
} surf;

typedef struct {
    sk_dashview *v;
    const sk_dashview_style *st;
    const layout *l;
    surf pane;
    sk_image layer; /* a view of the scratch image, while a layer is open */
    sk_rect layer_at;
    sk_buf buf;
} ctx;

static int round_px(double v)
{
    return (int)floor(v + 0.5);
}

/* The content's box from (x0, y0) to (x1, y1) snapped to whole pixels,
 * edge by edge, as Blink paints it: in the surface's image. */
static sk_rect snap(const surf *f, double x0, double y0, double x1, double y1)
{
    int a = round_px(x0), b = round_px(y0);
    return sk_rect_make(f->ox + a, f->oy + b, round_px(x1) - a, round_px(y1) - b);
}

static void fill(const surf *f, sk_rect r, sk_color c)
{
    sk_fill_rect(f->im, r, c, SK_BLEND_SRC_OVER, &f->clip);
}

/* c with an opacity of op / 255 on it. */
static sk_color fade(sk_color c, int op)
{
    uint32_t a = ((c >> 24) * (uint32_t)op + 127) / 255;
    return (c & 0xffffffu) | (a << 24);
}

/* A border bw wide inside b, each pixel once, as a ring. */
static void frame(const surf *f, sk_rect b, int bw, sk_color c)
{
    fill(f, sk_rect_make(b.x, b.y, b.w, bw), c);
    fill(f, sk_rect_make(b.x, b.y + b.h - bw, b.w, bw), c);
    fill(f, sk_rect_make(b.x, b.y + bw, bw, b.h - 2 * bw), c);
    fill(f, sk_rect_make(b.x + b.w - bw, b.y + bw, bw, b.h - 2 * bw), c);
}

/* n bytes of s from x at the baseline y of the content, clipped to box
 * (the image's) as overflow: hidden clips it, or to the pane alone. */
static void draw_text(ctx *c, const surf *f, int style, float px, double x, double y, const char *s, size_t n,
                      sk_color col, const sk_rect *box)
{
    sk_rect clip = box ? sk_rect_intersect(*box, f->clip) : f->clip;
    if (sk_rect_empty(clip) || !n)
        return;
    sk_draw_text(f->im, c->st->cache, c->st->fonts, style, px, (float)(f->ox + x), (float)(f->oy + y), s, n, col,
                 &clip);
}

/* One line from x, cut with an ellipsis where it passes maxw, as
 * text-overflow: ellipsis cuts it. */
static void draw_fit(ctx *c, const surf *f, float px, double x, double y, double maxw, const char *s, size_t n,
                     sk_color col, const sk_rect *box)
{
    sk_fontset *set = c->st->fonts;
    if (sk_measure_text(set, SK_STYLE_REGULAR, px, s, n) <= maxw + EPS) {
        draw_text(c, f, SK_STYLE_REGULAR, px, x, y, s, n, col, box);
        return;
    }
    double ew = sk_measure_text(set, SK_STYLE_REGULAR, px, ELLIPSIS, 3), kw = 0;
    size_t keep = 0;
    for (size_t i = 0; i < n;) {
        uint32_t cp;
        size_t k = sk_utf8_decode(s + i, n - i, &cp, NULL);
        double w = sk_measure_text(set, SK_STYLE_REGULAR, px, s, i + k);
        if (w + ew > maxw + EPS)
            break;
        i += k;
        keep = i;
        kw = w;
    }
    draw_text(c, f, SK_STYLE_REGULAR, px, x, y, s, keep, col, box);
    draw_text(c, f, SK_STYLE_REGULAR, px, x + kw, y, ELLIPSIS, 3, col, box);
}

/* clientWidth of a box dev pixels wide, in CSS pixels, as Blink's
 * AdjustForAbsoluteZoom::AdjustInt gives it. */
static int client(int dev, double s)
{
    if (s == 1)
        return dev;
    return (int)((s > 1 ? dev + 1 : dev) / s);
}

/* spark() of dashboard.js, on a canvas whose box is w by h device
 * pixels at (x, y) of the content: its bitmap clientWidth by
 * clientHeight times the scale, at the box's snapped corner. A line of
 * s across it 1.5px wide, gaps where s is null, scaled to its own range
 * from 0 at least, the newest point at the right; filled, the area
 * under a series with no gaps at 0.18 over it. */
static void spark(ctx *c, const surf *f, double x, double y, double w, double h, const sk_json *s, sk_color col,
                  bool filled)
{
    double sc = c->l->s;
    sk_rect box = snap(f, x, y, x + w, y + h);
    int cw = client(box.w, sc), ch = client(box.h, sc);
    size_t n = json_count(s), have = 0;
    if (cw <= 0 || ch <= 0 || !n)
        return;
    sk_rect clip = sk_rect_intersect(sk_rect_make(box.x, box.y, round_px(cw * sc), round_px(ch * sc)), f->clip);
    if (sk_rect_empty(clip))
        return;
    double lo = 0, hi = -INFINITY;
    for (size_t i = 0; i < n; i++)
        if (s->array.items[i].type == SK_JSON_NUMBER) {
            lo = SK_MIN(lo, s->array.items[i].number);
            hi = SK_MAX(hi, s->array.items[i].number);
            have++;
        }
    if (!have)
        return;
    double range = hi - lo != 0 ? hi - lo : 1;
#define SPARK_X(i) (box.x + (n < 2 ? cw - 1 : (double)(i) / (double)(n - 1) * (cw - 1)) * sc)
#define SPARK_Y(v) (box.y + (ch - 2 - ((v) - lo) / range * (ch - 4)) * sc)
    sk_path *p = c->v->path, *o = c->v->outline;
    sk_path_clear(p);
    bool on = false;
    for (size_t i = 0; i < n; i++) {
        const sk_json *e = &s->array.items[i];
        if (e->type != SK_JSON_NUMBER) {
            on = false;
            continue;
        }
        if (on)
            sk_path_line(p, (float)SPARK_X(i), (float)SPARK_Y(e->number));
        else
            sk_path_move(p, (float)SPARK_X(i), (float)SPARK_Y(e->number));
        on = true;
    }
    sk_path_clear(o);
    sk_path_stroke(o, p, (float)(1.5 * sc), SK_CAP_BUTT, SK_JOIN_MITER, 10);
    sk_fill_path(f->im, o, NULL, SK_FILL_NONZERO, col, &clip);
    if (filled && n > 1 && have == n) {
        sk_path_clear(p);
        sk_path_move(p, (float)SPARK_X(0), (float)(box.y + ch * sc));
        for (size_t i = 0; i < n; i++)
            sk_path_line(p, (float)SPARK_X(i), (float)SPARK_Y(s->array.items[i].number));
        sk_path_line(p, (float)SPARK_X(n - 1), (float)(box.y + ch * sc));
        sk_path_close(p);
        sk_fill_path(f->im, p, NULL, SK_FILL_NONZERO, fade(col, OP_AREA), &clip);
    }
#undef SPARK_X
#undef SPARK_Y
}

/* An element with an opacity over the content's rows y0 to y1: what it
 * draws goes to a layer the pane wide, which layer_end composites. False
 * when none of it shows. */
static bool layer_begin(ctx *c, double y0, double y1, surf *out)
{
    const surf *p = &c->pane;
    int top = p->oy + (int)floor(y0), bottom = p->oy + (int)ceil(y1);
    sk_rect r = sk_rect_intersect(p->clip, sk_rect_make(p->clip.x, top, p->clip.w, bottom - top));
    if (sk_rect_empty(r))
        return false;
    sk_image *s = &c->v->scratch;
    if (s->w < r.w || s->h < r.h) {
        int w = SK_MAX(s->w, r.w), h = SK_MAX(s->h, r.h);
        sk_image_free(s);
        sk_image_init(s, w, h);
    }
    c->layer = sk_image_view(s, sk_rect_make(0, 0, r.w, r.h));
    c->layer_at = r;
    sk_fill_rect(&c->layer, sk_image_bounds(&c->layer), 0, SK_BLEND_SRC, NULL);
    *out = (surf){&c->layer, p->ox - r.x, p->oy - r.y, sk_image_bounds(&c->layer)};
    return true;
}

static void layer_end(ctx *c, int op)
{
    sk_blit(c->pane.im, c->layer_at.x, c->layer_at.y, &c->layer, sk_image_bounds(&c->layer), SK_BLEND_SRC_OVER,
            (uint8_t)op, &c->pane.clip);
}

/* Whether any of the content's rows y0 to y1 shows. */
static bool shows(const ctx *c, double y0, double y1)
{
    const surf *p = &c->pane;
    int top = p->oy + (int)floor(y0), bottom = p->oy + (int)ceil(y1);
    return top < p->clip.y + p->clip.h && bottom > p->clip.y;
}

static void draw_lines(ctx *c, int style, const font_line *fl, double x, double y, const char *s, double maxw,
                       sk_color col)
{
    spans sp = {0};
    wrap(c->st->fonts, style, fl->px, s, maxw, &sp);
    for (size_t k = 0; k < sp.len; k++)
        draw_text(c, &c->pane, style, fl->px, x, y + (double)k * fl->line + fl->base, s + sp.ptr[k].at, sp.ptr[k].n,
                  col, NULL);
    sk_vec_free(&sp);
}

/* .d-across: each sum a box with its title, its value at the top right
 * and its filled sparkline. */
static void draw_sums(ctx *c)
{
    const layout *l = c->l;
    double s = l->s;
    const sk_json *across = sk_json_get(c->v->data, "across");
    for (size_t i = 0; i < SUMS; i++) {
        double x0 = PAD_X * s + (double)i * (l->chart_w + ACROSS_GAP * s), y0 = l->across_y;
        if (!shows(c, y0, y0 + l->chart_h))
            continue;
        frame(&c->pane, snap(&c->pane, x0, y0, x0 + l->chart_w, y0 + l->chart_h), (int)l->bw, BORDER);
        double cx = x0 + l->bw + CHART_PAD_X * s, cy = y0 + l->bw + CHART_PAD_Y * s;
        draw_lines(c, SK_STYLE_REGULAR, &l->small, cx, cy, sums[i].title, l->chart_cw, fade(c->st->fg, OP_DIM));
        const sk_json *series = sk_json_get(across, sums[i].key);
        spark(c, &c->pane, cx, cy + (double)l->title_lines[i] * l->small.line, l->chart_cw, CHART_H * s, series,
              sums[i].color, true);
        sk_buf_clear(&c->buf);
        sk_dashview_format(&c->buf, sums[i].meter, last_of(series));
        double vw = sk_measure_text(c->st->fonts, SK_STYLE_BOLD, l->bold.px, c->buf.ptr, c->buf.len);
        double right = x0 + l->chart_w - l->bw - CHART_PAD_X * s;
        draw_text(c, &c->pane, SK_STYLE_BOLD, l->bold.px, right - vw, cy + l->bold.base, c->buf.ptr, c->buf.len,
                  c->st->fg, NULL);
    }
}

/* The box of column i over the line box top to top + h, overflow:
 * hidden's clip; from x when it is not the column's own left. */
static sk_rect cell(const ctx *c, const surf *f, size_t i, double x, double top, double h)
{
    const layout *l = c->l;
    return snap(f, x, top, l->col_x.ptr[i] + l->col_w.ptr[i], top + h);
}

/* A row's rule along its bottom, and its background when lit. */
static void rule(const ctx *c, const surf *f, double top, double h, bool lit)
{
    const layout *l = c->l;
    sk_rect box = snap(f, PAD_X * l->s, top, PAD_X * l->s + l->cw, top + h);
    if (lit)
        fill(f, box, LIT);
    fill(f, sk_rect_make(box.x, box.y + box.h - (int)l->bw, box.w, (int)l->bw), RULE);
}

/* The header row: the columns' names at 11px, at 0.6. */
static void draw_header(ctx *c)
{
    const layout *l = c->l;
    const font_line *sm = &l->small;
    double top = l->header_y, tt = top + ROW_PAD * l->s, base = tt + sm->base;
    surf f;
    if (!layer_begin(c, top, top + l->header_h, &f))
        return;
    rule(c, &f, top, l->header_h, false);
    const double *x = l->col_x.ptr;
    sk_color fg = c->st->fg;
    sk_rect box = cell(c, &f, 0, x[0], tt, sm->line);
    draw_fit(c, &f, sm->px, x[0], base, l->col_w.ptr[0], "session", 7, fg, &box);
    box = cell(c, &f, 1, x[1], tt, sm->line);
    draw_fit(c, &f, sm->px, x[1], base, l->col_w.ptr[1], "folder", 6, fade(fg, OP_FOLDER), &box);
    draw_text(c, &f, SK_STYLE_REGULAR, sm->px, x[2], base, "state", 5, fg, NULL);
    size_t n = l->col_x.len;
    for (size_t i = 3; i < n; i++) {
        sk_buf_clear(&c->buf);
        if (i < 3 + SPARKS)
            sk_buf_append_c(&c->buf, meters[i - 3].name);
        else if (i + 1 < n)
            sk_buf_append_c(&c->buf, c->v->dials.ptr[i - 3 - SPARKS]);
        else
            sk_buf_append_c(&c->buf, "open / closed");
        box = cell(c, &f, i, x[i], tt, sm->line);
        draw_text(c, &f, SK_STYLE_REGULAR, sm->px, x[i], base, c->buf.ptr, c->buf.len, fg, &box);
    }
    layer_end(c, OP_HEADER);
}

/* A session's row: its label, folder and state, a sparkline and the
 * last value of each meter and dial, and its board; at 0.4 once ended,
 * lit under the pointer while live. */
static void draw_row(ctx *c, const sk_json *r, size_t i, bool lit)
{
    const layout *l = c->l;
    double s = l->s, top = l->rows_y + (double)i * l->row_h;
    bool ended = truthy(sk_json_get(r, "ended"));
    surf layer;
    const surf *f = &c->pane;
    if (ended) {
        if (!layer_begin(c, top, top + l->row_h, &layer))
            return;
        f = &layer;
    } else if (!shows(c, top, top + l->row_h)) {
        return;
    }
    rule(c, f, top, l->row_h, lit);
    /* align-items: center on the line, as high as a canvas. */
    double ct = top + ROW_PAD * s, cross = l->row_h - 2 * ROW_PAD * s - l->bw;
    double tt = ct + unit((cross - l->text.line) / 2), tb = tt + l->text.base;
    double nt = ct + unit((cross - l->small.line) / 2), nb = nt + l->small.base;
    double kt = ct + unit((cross - CANVAS_H * s) / 2);
    const double *x = l->col_x.ptr, *w = l->col_w.ptr;
    sk_color fg = c->st->fg, num = fade(fg, OP_NUM);
    sk_buf *b = &c->buf;
    plain(b, sk_json_get(r, "label"));
    sk_rect box = cell(c, f, 0, x[0], tt, l->text.line);
    draw_fit(c, f, l->text.px, x[0], tb, w[0], b->ptr, b->len, fg, &box);
    plain(b, sk_json_get(r, "folder"));
    box = cell(c, f, 1, x[1], tt, l->text.line);
    draw_fit(c, f, l->text.px, x[1], tb, w[1], b->ptr, b->len, fade(fg, OP_FOLDER), &box);
    plain(b, ended ? NULL : sk_json_get(r, "state"));
    if (ended)
        sk_buf_append_c(b, "ended");
    sk_color sc = ended ? fg : strcmp(b->ptr ? b->ptr : "", "working") == 0 ? WORKING
                         : strcmp(b->ptr ? b->ptr : "", "waiting") == 0 ? WAITING
                         : fg;
    draw_text(c, f, SK_STYLE_REGULAR, l->text.px, x[2], tb, b->ptr, b->len, sc, NULL);
    const sk_json *series = sk_json_get(r, "series");
    size_t n = l->col_x.len;
    for (size_t k = 3; k + 1 < n; k++) {
        bool dial = k >= 3 + SPARKS;
        const char *key = dial ? c->v->dials.ptr[k - 3 - SPARKS] : meters[k - 3].key;
        const sk_json *sr = sk_json_get(series, key);
        spark(c, f, x[k], kt, SPARK_CANVAS_W * s, CANVAS_H * s, sr, dial ? DIAL_COLOR : meters[k - 3].color, false);
        sk_buf_clear(b);
        sk_dashview_format(b, key, last_of(sr));
        double nx = x[k] + (SPARK_CANVAS_W + CELL_GAP) * s;
        box = cell(c, f, k, nx, nt, l->small.line);
        draw_text(c, f, SK_STYLE_REGULAR, l->small.px, nx, nb, b->ptr, b->len, num, &box);
    }
    const sk_json *board = sk_json_get(r, "board");
    double bx = x[n - 1];
    if (board_text(board, b)) {
        spark(c, f, bx, kt, BOARD_CANVAS_W * s, CANVAS_H * s, sk_json_get(board, "open"), BOARD_COLOR, false);
        bx += (BOARD_CANVAS_W + CELL_GAP) * s;
    }
    box = cell(c, f, n - 1, bx, nt, l->small.line);
    draw_text(c, f, SK_STYLE_REGULAR, l->small.px, bx, nb, b->ptr, b->len, num, &box);
    if (ended)
        layer_end(c, OP_ENDED);
}

void sk_dashview_draw(sk_dashview *v, sk_image *dst, sk_rect area, const sk_dashview_style *st)
{
    v->style = *st;
    v->area_w = SK_MAX(0, area.w);
    v->area_h = SK_MAX(0, area.h);
    v->laid = true;
    lay_out(v);
    clamp_scroll(v);
    v->lit = lit_row(v);
    const layout *l = &v->lay;
    ctx c = {v, st, l, {dst, area.x, area.y - (int)scroll_px(v), sk_rect_intersect(area, sk_image_bounds(dst))},
             {0}, {0, 0, 0, 0}, {0}};
    if (sk_rect_empty(c.pane.clip))
        return;
    sk_fill_rect(dst, c.pane.clip, st->bg, SK_BLEND_SRC_OVER, NULL);
    double s = l->s;
    draw_lines(&c, SK_STYLE_BOLD, &l->bold, PAD_X * s, PAD_Y * s, l->head.ptr, l->cw, st->fg);
    if (!v->data) {
        sk_buf_free(&c.buf);
        return;
    }
    draw_sums(&c);
    draw_header(&c);
    const sk_json *rows = rows_of(v);
    size_t n = json_count(rows);
    for (size_t i = 0; i < n; i++)
        draw_row(&c, &rows->array.items[i], i, (long)i == v->lit);
    if (!n)
        draw_lines(&c, SK_STYLE_REGULAR, &l->text, PAD_X * s, l->rows_y + NONE_PAD * s, NONE, l->cw,
                   fade(st->fg, OP_DIM));
    sk_buf_free(&c.buf);
}

/* ---- The view ---- */

sk_dashview *sk_dashview_new(void)
{
    sk_dashview *v = sk_xcalloc(1, sizeof *v);
    sk_arena_init(&v->arena, 0);
    v->lit = -1;
    v->path = sk_path_new();
    v->outline = sk_path_new();
    return v;
}

void sk_dashview_free(sk_dashview *v)
{
    if (!v)
        return;
    sk_arena_free(&v->arena);
    sk_strv_free(&v->dials);
    sk_buf_free(&v->lay.head);
    sk_vec_free(&v->lay.col_x);
    sk_vec_free(&v->lay.col_w);
    sk_image_free(&v->scratch);
    sk_path_free(v->path);
    sk_path_free(v->outline);
    free(v);
}

bool sk_dashview_update(sk_dashview *v, const char *json, size_t len)
{
    if (!json)
        return false;
    sk_arena a;
    sk_arena_init(&a, 0);
    const sk_json *d = sk_json_parse(&a, json, len, NULL);
    if (!d || d->type != SK_JSON_OBJECT) {
        sk_arena_free(&a);
        return false;
    }
    sk_arena_free(&v->arena);
    v->arena = a;
    v->data = d;
    /* (data.dials || []): a name each, as String() makes it. */
    sk_strv_free(&v->dials);
    const sk_json *dials = sk_json_get(d, "dials");
    sk_buf name = {0};
    for (size_t i = 0; i < json_count(dials); i++) {
        sk_buf_clear(&name);
        template_of(&name, &dials->array.items[i]);
        sk_strv_push(&v->dials, sk_buf_cstr(&name));
    }
    sk_buf_free(&name);
    if (v->laid) {
        lay_out(v);
        clamp_scroll(v);
        v->lit = lit_row(v);
    }
    return true;
}

bool sk_dashview_scroll(sk_dashview *v, double dy)
{
    if (!v->laid || !isfinite(dy))
        return false;
    double was = scroll_px(v);
    v->scroll += dy;
    clamp_scroll(v);
    if (scroll_px(v) == was)
        return false;
    v->lit = lit_row(v);
    return true;
}

bool sk_dashview_hover(sk_dashview *v, double x, double y)
{
    v->pointer = true;
    v->px = x;
    v->py = y;
    long lit = lit_row(v);
    bool changed = lit != v->lit;
    v->lit = lit;
    return changed;
}

const char *sk_dashview_hit(sk_dashview *v, double x, double y)
{
    long i = row_at(v, x, y);
    const sk_json *r = i >= 0 ? row(v, i) : NULL;
    if (!r || truthy(sk_json_get(r, "ended")))
        return NULL;
    const sk_json *key = sk_json_get(r, "key");
    return key && key->type == SK_JSON_STRING ? key->string.ptr : NULL;
}

/* A member written when the row has it, as JSON.stringify leaves out
 * undefined. */
static void member(sk_jw *w, const sk_json *r, const char *key)
{
    const sk_json *m = sk_json_get(r, key);
    if (!m)
        return;
    sk_jw_key(w, sk_str_c(key));
    sk_jw_value(w, m);
}

void sk_dashview_state(const sk_dashview *v, sk_jw *w)
{
    const sk_json *d = v->data;
    if (!d) {
        sk_jw_null(w);
        return;
    }
    sk_jw_object(w);
    sk_jw_key(w, SK_STR("ticks"));
    sk_jw_int(w, (int64_t)json_count(sk_json_get(d, "ticks")));
    sk_jw_key(w, SK_STR("dials"));
    const sk_json *dials = sk_json_get(d, "dials");
    if (truthy(dials)) {
        sk_jw_value(w, dials);
    } else {
        sk_jw_array(w);
        sk_jw_array_end(w);
    }
    const sk_json *rows = rows_of(v);
    size_t n = json_count(rows);
    sk_buf b = {0};
    sk_jw_key(w, SK_STR("rows"));
    sk_jw_array(w);
    for (size_t i = 0; i < n; i++) {
        const sk_json *r = &rows->array.items[i], *series = sk_json_get(r, "series");
        sk_jw_object(w);
        member(w, r, "key");
        member(w, r, "label");
        member(w, r, "state");
        member(w, r, "ended");
        const sk_json *tokens = sk_json_get(series, "tokens");
        size_t points = 0;
        for (size_t k = 0; k < json_count(tokens); k++)
            points += tokens->array.items[k].type != SK_JSON_NULL;
        sk_jw_key(w, SK_STR("points"));
        sk_jw_int(w, (int64_t)points);
        sk_jw_key(w, SK_STR("last"));
        sk_jw_object(w);
        for (size_t k = 0; k < SPARKS; k++) {
            sk_jw_key(w, sk_str_c(meters[k].key));
            double last = last_of(sk_json_get(series, meters[k].key));
            if (isnan(last))
                sk_jw_null(w);
            else
                sk_jw_number(w, last);
        }
        sk_jw_object_end(w);
        const sk_json *board = sk_json_get(r, "board");
        sk_jw_key(w, SK_STR("board"));
        if (truthy(board))
            sk_jw_value(w, board);
        else
            sk_jw_null(w);
        board_text(board, &b);
        sk_jw_key(w, SK_STR("boardText"));
        sk_jw_string(w, sk_buf_str(&b));
        sk_jw_object_end(w);
    }
    sk_jw_array_end(w);
    sk_jw_key(w, SK_STR("shownRows"));
    sk_jw_array(w);
    for (size_t i = 0; i < n; i++) {
        const sk_json *r = &rows->array.items[i];
        bool ended = truthy(sk_json_get(r, "ended"));
        sk_buf_clear(&b);
        template_of(&b, sk_json_get(r, "key"));
        sk_jw_object(w);
        sk_jw_key(w, SK_STR("key"));
        sk_jw_string(w, sk_buf_str(&b));
        sk_jw_key(w, SK_STR("ended"));
        sk_jw_bool(w, ended);
        sk_jw_key(w, SK_STR("opacity"));
        sk_jw_string(w, ended ? SK_STR("0.4") : SK_STR("1"));
        sk_jw_object_end(w);
    }
    sk_jw_array_end(w);
    sk_jw_object_end(w);
    sk_buf_free(&b);
}

bool sk_dashview_layout_of(const sk_dashview *v, sk_dashview_layout *out)
{
    if (!v->laid)
        return false;
    const layout *l = &v->lay;
    double s = l->s;
    *out = (sk_dashview_layout){
        .height = l->height / s,
        .scroll = scroll_px(v) / s,
        .max_scroll = l->max_scroll / s,
        .head_lines = l->head_lines,
        .header_y = l->header_y / s,
        .rows_y = l->rows_y / s,
        .row_h = l->row_h / s,
        .columns = l->col_x.len,
    };
    return true;
}

bool sk_dashview_column(const sk_dashview *v, size_t i, double *x, double *w)
{
    if (!v->laid || i >= v->lay.col_x.len)
        return false;
    *x = v->lay.col_x.ptr[i] / v->lay.s;
    *w = v->lay.col_w.ptr[i] / v->lay.s;
    return true;
}
