#include "menu.h"

#include "../base/utf8.h"
#include "../gfx/draw.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

const char *const sk_menu_titles[SK_MENU_COUNT] = {"File", "Agents", "Skins", "Visualization"};

/* app.css's #menu, in points. */
#define FONT_PX 13.0
#define LINE (FONT_PX * 1.6)     /* an item's line box */
#define HEAD_PX 11.0
#define HEAD_LINE (HEAD_PX * 1.6)
#define MONO_PX 11.0
#define MONO_LINE (MONO_PX * 1.4)
#define PAD_X 14.0               /* .item's and .title's sides */
#define FLOAT_GAP 28.0           /* .keys's and ::after's margin-left */
#define MIN_W 180.0
#define MAX_W 520.0
#define SUB_MIN_W 160.0
#define BAR_H (LINE + 2)         /* .title's padding 1px */
#define RULE_H 9.0               /* hr: 4 + 1 + 4 */
#define MSG_MARGIN_X 10.0
#define MSG_MARGIN_Y 4.0
#define MSG_PAD 6.0
#define MSG_MAX 240.0            /* .error's max-height */
#define SHADOW_Y 4.0
#define SHADOW_BLUR 16.0
#define EDGE 4.0                 /* what menu.js keeps from the window's edges */
#define S_GAP 10.0               /* .session's columns */
#define S_LABEL 120.0
#define S_STATE 58.0
#define S_AGE 38.0
#define S_WHERE 52.0
#define S_FOLDER_MIN 60.0
#define LOC_GAP 14.0             /* .located */
#define LOC_MIN 60.0
#define ENDED_ALPHA 140          /* .session.ended's opacity 0.55 */

#define C_BG SK_RGB(0x1b, 0x1d, 0x21)
#define C_TEXT SK_RGB(0xd8, 0xdd, 0xe3)
#define C_BORDER SK_RGB(0x4a, 0x50, 0x5a)
#define C_BAR SK_RGB(0x24, 0x27, 0x2c)
#define C_LINE SK_RGB(0x3a, 0x3f, 0x47)
#define C_TITLE_HOVER SK_RGB(0x2c, 0x31, 0x38)
#define C_LIT SK_RGB(0x2f, 0x5d, 0x8a)
#define C_WHITE SK_RGB(0xff, 0xff, 0xff)
#define C_DIM SK_RGB(0x8a, 0x92, 0x9c)
#define C_DISABLED SK_RGB(0x5c, 0x63, 0x6c)
#define C_PROBLEM SK_RGB(0xff, 0xb4, 0xb4)
#define C_PATH SK_RGB(0xa7, 0xb0, 0xba)
#define C_WORKING SK_RGB(0xe0, 0xb0, 0x50)
#define C_WAITING SK_RGB(0x8f, 0xd1, 0x8f)
#define C_WHERE SK_RGB(0x6f, 0xa8, 0xdc)
#define C_ERROR_BG SK_RGB(0x3a, 0x16, 0x16)
#define C_NOTE_BG SK_RGB(0x16, 0x28, 0x3a)
#define C_NOTE SK_RGB(0xc8, 0xdc, 0xf0)
#define C_SHADOW SK_RGBA(0, 0, 0, 0xaa)

#define ARROW "\xe2\x96\xb8" /* U+25B8, .submenu::after */
#define ELLIPSIS "\xe2\x80\xa6"

typedef struct {
    double y, h; /* from the drop-down's top */
} span;

struct sk_menu {
    bool open;
    int shown;
    unsigned asked;
    unsigned presses, waiting; /* the newest press, and the one an answer is awaited for */
    sk_menu_row *rows;
    size_t n;
    span *geo;
    /* The sessions' box: its rows, which have ended, its height fixed
     * at the show, and how far it scrolled. */
    sk_menu_session *sess;
    bool *ended;
    size_t nsess;
    bool has_box;
    int box_row;
    double box_h, box_scroll;
    /* The message at the foot, wrapped. */
    char *message;
    bool error;
    char **lines;
    size_t nlines;
    double msg_y, msg_h, msg_scroll;
    /* The drop-down's border box, and the bar's titles from its left. */
    double x, y, w, h;
    double tx[SK_MENU_COUNT], tw[SK_MENU_COUNT];
    /* What it was placed by. */
    double bx, by, bw, bh, win_w, win_h, stage_top;
    sk_menu_style st;
    /* The pointer's. */
    int hover_title, hover_row, hover_sess, hover_sub;
    int flyout;
    double fx, fy, fw, fh;
    char *kept; /* the argument of the last row that closed the menu */
};

/* ---- Text ---- */

float sk_baseline_in(sk_fontset *set, int style, float px, float top, float lh)
{
    sk_font_vmetrics vm;
    sk_font_vmetrics_at(sk_fontset_primary(set, style), px, &vm);
    return top + (lh - (vm.ascent + vm.descent)) / 2 + vm.ascent;
}

float sk_draw_fit(sk_image *dst, sk_glyph_cache *cache, sk_fontset *set, int style, float px, float x, float base,
                  float maxw, const char *s, sk_color c, bool at_start, const sk_rect *clip)
{
    size_t n = strlen(s);
    float w = sk_measure_text(set, style, px, s, n);
    /* Half a pixel of slack: a box sized to its text is rounded. */
    if (w <= maxw + 0.5f) {
        sk_draw_text(dst, cache, set, style, px, x, base, s, n, c, clip);
        return w;
    }
    float ew = sk_measure_text(set, style, px, ELLIPSIS, 3);
    if (ew > maxw)
        return 0;
    if (at_start) {
        /* The longest end of s that fits after the ellipsis. */
        for (size_t i = 0; i < n;) {
            uint32_t cp;
            size_t k = sk_utf8_decode(s + i, n - i, &cp, NULL);
            i += k ? k : 1;
            float tw = sk_measure_text(set, style, px, s + i, n - i);
            if (tw + ew <= maxw) {
                sk_draw_text(dst, cache, set, style, px, x, base, ELLIPSIS, 3, c, clip);
                sk_draw_text(dst, cache, set, style, px, x + ew, base, s + i, n - i, c, clip);
                return ew + tw;
            }
        }
        sk_draw_text(dst, cache, set, style, px, x, base, ELLIPSIS, 3, c, clip);
        return ew;
    }
    size_t keep = 0;
    float kw = 0;
    for (size_t i = 0; i < n;) {
        uint32_t cp;
        size_t k = sk_utf8_decode(s + i, n - i, &cp, NULL);
        k = k ? k : 1;
        float cw = sk_measure_text(set, style, px, s, i + k);
        if (cw + ew > maxw)
            break;
        keep = i + k;
        kw = cw;
        i += k;
    }
    sk_draw_text(dst, cache, set, style, px, x, base, s, keep, c, clip);
    sk_draw_text(dst, cache, set, style, px, x + kw, base, ELLIPSIS, 3, c, clip);
    return kw + ew;
}

/* ---- Shadows ---- */

/* The share of a Gaussian of deviation sigma, centred at c, that falls
 * between a and b. */
static double share(double a, double b, double c, double sigma)
{
    double k = 1 / (sqrt(2.0) * sigma);
    return 0.5 * (erf((b - c) * k) - erf((a - c) * k));
}

void sk_shadow_rect(sk_image *dst, double x, double y, double w, double h, double blur, sk_color c,
                    const sk_rect *clip)
{
    double sigma = blur / 2;
    if (sigma < 0.01) {
        sk_rect r = sk_rect_make((int)lround(x), (int)lround(y), (int)lround(x + w) - (int)lround(x),
                                 (int)lround(y + h) - (int)lround(y));
        sk_fill_rect(dst, r, c, SK_BLEND_SRC_OVER, clip);
        return;
    }
    double ext = 3 * sigma;
    sk_rect area = sk_rect_make((int)floor(x - ext), (int)floor(y - ext), 0, 0);
    area.w = (int)ceil(x + w + ext) - area.x;
    area.h = (int)ceil(y + h + ext) - area.y;
    area = sk_rect_intersect(area, sk_image_bounds(dst));
    if (clip)
        area = sk_rect_intersect(area, *clip);
    if (sk_rect_empty(area))
        return;
    /* Each pixel's coverage is the blurred rect's: the product of the
     * Gaussian's shares across and down. */
    double *gx = sk_xcalloc((size_t)area.w, sizeof *gx), *gy = sk_xcalloc((size_t)area.h, sizeof *gy);
    for (int i = 0; i < area.w; i++)
        gx[i] = share(x, x + w, area.x + i + 0.5, sigma);
    for (int j = 0; j < area.h; j++)
        gy[j] = share(y, y + h, area.y + j + 0.5, sigma);
    uint8_t *a = sk_xmalloc((size_t)area.w * (size_t)area.h);
    for (int j = 0; j < area.h; j++)
        for (int i = 0; i < area.w; i++)
            a[(size_t)j * (size_t)area.w + (size_t)i] = (uint8_t)lround(SK_MIN(1.0, gx[i] * gy[j]) * 255);
    sk_mask m = {a, area.w, area.h, area.w};
    sk_fill_mask(dst, area.x, area.y, &m, c, clip);
    free(a);
    free(gx);
    free(gy);
}

/* ---- Rows ---- */

static void row_free(sk_menu_row *r)
{
    free(r->text);
    free(r->keys);
    free(r->location);
    free(r->arg);
    sk_menu_rows_free(r->sub, r->nsub);
    sk_menu_sessions_free(r->sessions, r->nsessions);
}

void sk_menu_rows_free(sk_menu_row *rows, size_t n)
{
    for (size_t i = 0; i < n; i++)
        row_free(&rows[i]);
    free(rows);
}

static void session_free(sk_menu_session *s)
{
    free(s->key);
    free(s->label);
    free(s->folder);
    free(s->state);
    free(s->age);
    free(s->where);
}

void sk_menu_sessions_free(sk_menu_session *rows, size_t n)
{
    for (size_t i = 0; i < n; i++)
        session_free(&rows[i]);
    free(rows);
}

/* ---- Measuring, in points ---- */

static double text_w(const sk_menu *m, sk_fontset *set, int style, double size, const char *s)
{
    if (!s)
        return 0;
    double k = m->st.scale;
    return sk_measure_text(set, style, (float)(size * k), s, strlen(s)) / k;
}

/* A heading's text as .heading sets it: upper case, 1px apart. */
static void upper(const char *s, char *out, size_t cap)
{
    size_t i = 0;
    for (; s[i] && i + 1 < cap; i++)
        out[i] = s[i] >= 'a' && s[i] <= 'z' ? (char)(s[i] - 32) : s[i];
    out[i] = 0;
}

static double heading_w(const sk_menu *m, const char *s)
{
    char up[256];
    upper(s, up, sizeof up);
    double w = 0;
    for (size_t i = 0; up[i];) {
        uint32_t cp;
        size_t k = sk_utf8_decode(up + i, strlen(up + i), &cp, NULL);
        k = k ? k : 1;
        char ch[8] = {0};
        memcpy(ch, up + i, k < sizeof ch ? k : sizeof ch - 1);
        w += text_w(m, m->st.sans, 0, HEAD_PX, ch) + 1;
        i += k;
    }
    return w;
}

static int row_style(const sk_menu_row *r)
{
    return r->current ? SK_STYLE_BOLD : 0;
}

static double session_w(const sk_menu *m, const sk_menu_session *s)
{
    return 2 * PAD_X + S_LABEL + S_STATE + S_AGE + S_WHERE + 4 * S_GAP +
           SK_MAX(S_FOLDER_MIN, text_w(m, m->st.sans, 0, FONT_PX, s->folder));
}

/* What a row would take across, its max-content width. */
static double row_w(const sk_menu *m, const sk_menu_row *r)
{
    switch (r->kind) {
    case SK_ROW_ITEM: {
        double w = text_w(m, m->st.sans, row_style(r), FONT_PX, r->text);
        if (r->keys)
            w += FLOAT_GAP + text_w(m, m->st.sans, 0, FONT_PX, r->keys);
        if (r->location)
            w += LOC_GAP + SK_MAX(LOC_MIN, text_w(m, m->st.sans, 0, FONT_PX, r->location));
        return w + 2 * PAD_X;
    }
    case SK_ROW_FLYOUT:
        return 2 * PAD_X + text_w(m, m->st.sans, row_style(r), FONT_PX, r->text) + FLOAT_GAP +
               text_w(m, m->st.sans, 0, FONT_PX, ARROW);
    case SK_ROW_HEADING:
        return 2 * PAD_X + heading_w(m, r->text);
    case SK_ROW_SESSIONS: {
        double w = 2 * PAD_X + text_w(m, m->st.sans, 0, FONT_PX, "none running");
        if (m->nsess)
            w = 0;
        for (size_t i = 0; i < m->nsess; i++)
            w = SK_MAX(w, session_w(m, &m->sess[i]));
        return w;
    }
    default:
        return 0;
    }
}

static double sub_w(const sk_menu *m, const sk_menu_row *r)
{
    double w = SUB_MIN_W;
    for (size_t i = 0; i < r->nsub; i++)
        w = SK_MAX(w, 2 * PAD_X + text_w(m, m->st.sans, row_style(&r->sub[i]), FONT_PX, r->sub[i].text));
    return w + 2;
}

static void free_lines(sk_menu *m)
{
    for (size_t i = 0; i < m->nlines; i++)
        free(m->lines[i]);
    free(m->lines);
    m->lines = NULL;
    m->nlines = 0;
}

static void push_line(sk_menu *m, const char *s, size_t n)
{
    m->lines = sk_xreallocarray(m->lines, m->nlines + 1, sizeof *m->lines);
    char *l = sk_xmalloc(n + 1);
    memcpy(l, s, n);
    l[n] = 0;
    m->lines[m->nlines++] = l;
}

/* The message's lines as white-space: pre-wrap breaks them in width:
 * at its newlines, then after the last space that fits; a word wider
 * than the line is broken where it reaches the edge. */
static void wrap(sk_menu *m, double width)
{
    free_lines(m);
    const char *s = m->message;
    while (s) {
        const char *nl = strchr(s, '\n');
        size_t n = nl ? (size_t)(nl - s) : strlen(s);
        size_t at = 0;
        do {
            size_t fit = 0, space = 0;
            for (size_t i = at; i < n;) {
                uint32_t cp;
                size_t k = sk_utf8_decode(s + i, n - i, &cp, NULL);
                k = k ? k : 1;
                char *part = sk_xmalloc(i + k - at + 1);
                memcpy(part, s + at, i + k - at);
                part[i + k - at] = 0;
                double w = text_w(m, m->st.mono, 0, MONO_PX, part);
                free(part);
                if (w > width && fit > at)
                    break;
                fit = i + k;
                if (cp == ' ')
                    space = fit;
                i += k;
            }
            size_t end = fit < n && space > at ? space : fit;
            push_line(m, s + at, end - at);
            at = end;
        } while (at < n);
        s = nl ? nl + 1 : NULL;
    }
}

static double longest_line(const sk_menu *m)
{
    double w = 0;
    const char *s = m->message;
    while (s) {
        const char *nl = strchr(s, '\n');
        size_t n = nl ? (size_t)(nl - s) : strlen(s);
        char *line = sk_xmalloc(n + 1);
        memcpy(line, s, n);
        line[n] = 0;
        w = SK_MAX(w, text_w(m, m->st.mono, 0, MONO_PX, line));
        free(line);
        s = nl ? nl + 1 : NULL;
    }
    return w;
}

static double row_h(const sk_menu *m, const sk_menu_row *r)
{
    switch (r->kind) {
    case SK_ROW_HEADING:
        return HEAD_LINE;
    case SK_ROW_RULE:
        return RULE_H;
    case SK_ROW_SESSIONS:
        return m->box_h;
    default:
        return LINE;
    }
}

static double box_content_h(const sk_menu *m)
{
    return (m->nsess ? (double)m->nsess : 1) * LINE;
}

/* Sizes every part from the rows, and with place puts the drop-down
 * below the button, or above it. */
static void layout(sk_menu *m, bool place)
{
    double cw = 0;
    for (int t = 0; t < SK_MENU_COUNT; t++) {
        m->tx[t] = cw;
        m->tw[t] = 2 * PAD_X + text_w(m, m->st.sans, 0, FONT_PX, sk_menu_titles[t]);
        cw += m->tw[t];
    }
    for (size_t i = 0; i < m->n; i++)
        cw = SK_MAX(cw, row_w(m, &m->rows[i]));
    if (m->message)
        cw = SK_MAX(cw, longest_line(m) + 2 * (MSG_MARGIN_X + MSG_PAD));
    double inner = SK_MIN(SK_MAX(cw, MIN_W), MAX_W);
    /* The border, the bar over the top padding, its border and margin. */
    double y = 1 + BAR_H + 1 + 4;
    m->geo = sk_xreallocarray(m->geo, m->n ? m->n : 1, sizeof *m->geo);
    for (size_t i = 0; i < m->n; i++) {
        m->geo[i].y = y;
        m->geo[i].h = row_h(m, &m->rows[i]);
        y += m->geo[i].h;
    }
    if (m->message) {
        wrap(m, inner - 2 * (MSG_MARGIN_X + MSG_PAD));
        /* A rule's bottom margin and the message's top one collapse. */
        bool after_rule = m->n && m->rows[m->n - 1].kind == SK_ROW_RULE;
        double content = (double)m->nlines * MONO_LINE;
        if (m->error)
            content = SK_MIN(content, MSG_MAX);
        m->msg_y = y + (after_rule ? 0 : MSG_MARGIN_Y);
        m->msg_h = 2 * MSG_PAD + content;
        y = m->msg_y + m->msg_h + MSG_MARGIN_Y;
    }
    m->w = inner + 2;
    m->h = y + 4 + 1;
    if (!place)
        return;
    m->x = SK_MAX(EDGE, SK_MIN(m->bx, m->win_w - m->w - EDGE));
    double below = m->by + m->bh + 2;
    m->y = below + m->h <= m->win_h - EDGE ? below : SK_MAX(m->stage_top, m->by - m->h - 2);
}

/* fitSessions: the box keeps the height it opened with, no more than
 * the window leaves the drop-down below the button, and not below two
 * rows. */
static void fit_box(sk_menu *m)
{
    double natural = box_content_h(m);
    m->box_h = natural;
    layout(m, false);
    double room = m->win_h - EDGE - (m->by + m->bh + 2);
    double over = m->h - room;
    m->box_h = SK_MAX(SK_MIN(natural, 2 * LINE), over > 0 ? natural - over : natural);
}

static double box_max_scroll(const sk_menu *m)
{
    return SK_MAX(0.0, box_content_h(m) - m->box_h);
}

static double msg_max_scroll(const sk_menu *m)
{
    return SK_MAX(0.0, (double)m->nlines * MONO_LINE - (m->msg_h - 2 * MSG_PAD));
}

/* ---- The menu ---- */

sk_menu *sk_menu_new(void)
{
    sk_menu *m = sk_xcalloc(1, sizeof *m);
    m->hover_title = m->hover_row = m->hover_sess = m->hover_sub = m->flyout = m->box_row = -1;
    return m;
}

static void clear(sk_menu *m)
{
    sk_menu_rows_free(m->rows, m->n);
    m->rows = NULL;
    m->n = 0;
    sk_menu_sessions_free(m->sess, m->nsess);
    free(m->ended);
    m->sess = NULL;
    m->ended = NULL;
    m->nsess = 0;
    m->has_box = false;
    m->box_row = -1;
    m->box_scroll = 0;
    free(m->message);
    m->message = NULL;
    m->msg_scroll = 0;
    free_lines(m);
    m->hover_title = m->hover_row = m->hover_sess = m->hover_sub = m->flyout = -1;
    m->waiting = 0;
}

void sk_menu_free(sk_menu *m)
{
    if (!m)
        return;
    clear(m);
    free(m->geo);
    free(m->kept);
    free(m);
}

bool sk_menu_open(const sk_menu *m)
{
    return m->open;
}

int sk_menu_shown(const sk_menu *m)
{
    return m->shown;
}

unsigned sk_menu_ask(sk_menu *m, int menu)
{
    (void)menu;
    return ++m->asked;
}

bool sk_menu_show(sk_menu *m, unsigned ask, int menu, sk_menu_row *rows, size_t n, const sk_menu_style *st,
                  double bx, double by, double bw, double bh, double win_w, double win_h, double stage_top)
{
    if (ask != m->asked || menu < 0 || menu >= SK_MENU_COUNT) {
        sk_menu_rows_free(rows, n);
        return false;
    }
    clear(m);
    m->open = true;
    m->shown = menu;
    m->rows = rows;
    m->n = n;
    m->st = *st;
    m->bx = bx;
    m->by = by;
    m->bw = bw;
    m->bh = bh;
    m->win_w = win_w;
    m->win_h = win_h;
    m->stage_top = stage_top;
    for (size_t i = 0; i < n; i++) {
        if (rows[i].kind != SK_ROW_SESSIONS || m->has_box)
            continue;
        m->has_box = true;
        m->box_row = (int)i;
        m->sess = rows[i].sessions;
        m->nsess = rows[i].nsessions;
        m->ended = sk_xcalloc(m->nsess ? m->nsess : 1, sizeof *m->ended);
        rows[i].sessions = NULL;
        rows[i].nsessions = 0;
    }
    if (m->has_box)
        fit_box(m);
    layout(m, true);
    return true;
}

void sk_menu_sessions(sk_menu *m, sk_menu_session *rows, size_t n)
{
    if (!m->open || !m->has_box) {
        sk_menu_sessions_free(rows, n);
        return;
    }
    /* Every row shown stays in its place: redrawn when still listed,
     * else marked ended; the new ones go last. */
    bool *taken = sk_xcalloc(n ? n : 1, sizeof *taken);
    for (size_t i = 0; i < m->nsess; i++) {
        size_t k = 0;
        while (k < n && (taken[k] || strcmp(rows[k].key, m->sess[i].key)))
            k++;
        if (k == n) {
            m->ended[i] = true;
            continue;
        }
        session_free(&m->sess[i]);
        m->sess[i] = rows[k];
        m->ended[i] = false;
        taken[k] = true;
    }
    for (size_t k = 0; k < n; k++) {
        if (taken[k])
            continue;
        m->sess = sk_xreallocarray(m->sess, m->nsess + 1, sizeof *m->sess);
        m->ended = sk_xreallocarray(m->ended, m->nsess + 1, sizeof *m->ended);
        m->sess[m->nsess] = rows[k];
        m->ended[m->nsess++] = false;
    }
    free(taken);
    free(rows);
    m->box_scroll = SK_MIN(m->box_scroll, box_max_scroll(m));
    layout(m, false);
}

bool sk_menu_has_sessions(const sk_menu *m)
{
    return m->open && m->has_box;
}

void sk_menu_close(sk_menu *m)
{
    m->asked++;
    if (!m->open)
        return;
    m->open = false;
    clear(m);
}

static void say(sk_menu *m, const char *text, bool error)
{
    free(m->message);
    m->message = sk_xstrdup(text);
    m->error = error;
    m->msg_scroll = 0;
    layout(m, false);
}

void sk_menu_answer(sk_menu *m, unsigned press, const char *error, const char *note)
{
    if (!m->open || !press || press != m->waiting)
        return;
    m->waiting = 0;
    if (error)
        say(m, error, true);
    else if (note)
        say(m, note, false);
    else
        sk_menu_close(m);
}

/* ---- The pointer ---- */

typedef struct {
    bool inside;     /* in the drop-down or its flyout */
    int title, row, sess, sub;
    bool in_box, in_msg;
} hit_t;

static bool in(double x, double y, double rx, double ry, double rw, double rh)
{
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static bool flyout_box(const sk_menu *m, double *x, double *y, double *w, double *h)
{
    if (m->flyout < 0)
        return false;
    *x = m->fx;
    *y = m->fy;
    *w = m->fw;
    *h = m->fh;
    return true;
}

static hit_t hit(const sk_menu *m, double x, double y)
{
    hit_t h = {false, -1, -1, -1, -1, false, false};
    if (!m->open)
        return h;
    double fx, fy, fw, fh;
    if (flyout_box(m, &fx, &fy, &fw, &fh) && in(x, y, fx, fy, fw, fh)) {
        h.inside = true;
        h.row = m->flyout;
        double top = fy + 1 + 4;
        if (y >= top && y < top + (double)m->rows[m->flyout].nsub * LINE && x >= fx + 1 && x < fx + fw - 1)
            h.sub = (int)((y - top) / LINE);
        return h;
    }
    if (!in(x, y, m->x, m->y, m->w, m->h))
        return h;
    h.inside = true;
    double lx = x - m->x - 1, ly = y - m->y;
    if (lx < 0 || lx >= m->w - 2)
        return h;
    if (ly >= 1 && ly < 1 + BAR_H) {
        for (int t = 0; t < SK_MENU_COUNT; t++)
            if (lx >= m->tx[t] && lx < m->tx[t] + m->tw[t])
                h.title = t;
        return h;
    }
    for (size_t i = 0; i < m->n; i++) {
        if (ly < m->geo[i].y || ly >= m->geo[i].y + m->geo[i].h)
            continue;
        h.row = (int)i;
        if (m->rows[i].kind == SK_ROW_SESSIONS) {
            h.in_box = true;
            double at = (ly - m->geo[i].y + m->box_scroll) / LINE;
            if (at >= 0 && at < (double)m->nsess)
                h.sess = (int)at;
        }
        return h;
    }
    if (m->message && ly >= m->msg_y && ly < m->msg_y + m->msg_h && lx >= MSG_MARGIN_X &&
        lx < m->w - 2 - MSG_MARGIN_X)
        h.in_msg = true;
    return h;
}

/* Whether a row takes a press and lights under the pointer. */
static bool acts(const sk_menu_row *r)
{
    return (r->kind == SK_ROW_ITEM && !r->disabled && (r->action || r->then == SK_ROW_SAYS));
}

static bool lights(const sk_menu_row *r)
{
    return (r->kind == SK_ROW_ITEM && !r->disabled) || r->kind == SK_ROW_FLYOUT;
}

static void place_flyout(sk_menu *m, int i)
{
    const sk_menu_row *r = &m->rows[i];
    m->flyout = i;
    m->fw = sub_w(m, r);
    m->fh = 2 + 2 * 4 + (double)r->nsub * LINE;
    double rx = m->x + 1, rw = m->w - 2, ry = m->y + m->geo[i].y;
    /* To the right of the drop-down, to its left when the window has no
     * room, and moved up as far as it would run off the bottom. */
    m->fx = rx + rw;
    if (m->fx + m->fw > m->win_w - EDGE)
        m->fx = rx - m->fw;
    m->fy = ry - 5;
    if (m->fy + m->fh > m->win_h - EDGE)
        m->fy -= m->fy + m->fh - (m->win_h - EDGE);
}

bool sk_menu_mouse_move(sk_menu *m, double x, double y)
{
    if (!m->open)
        return false;
    int title = -1, row = -1, sess = -1, sub = -1, flyout = m->flyout;
    hit_t h = x < 0 && y < 0 ? (hit_t){false, -1, -1, -1, -1, false, false} : hit(m, x, y);
    /* The flyout stays while the pointer is on its row or in it. */
    if (flyout >= 0 && !(h.row == flyout && (h.sub >= 0 || h.inside)))
        flyout = -1;
    if (h.inside) {
        title = h.title;
        row = h.row >= 0 && lights(&m->rows[h.row]) ? h.row : -1;
        sess = h.sess;
        sub = h.sub;
        if (h.row >= 0 && m->rows[h.row].kind == SK_ROW_FLYOUT)
            flyout = h.row;
    }
    bool changed = title != m->hover_title || row != m->hover_row || sess != m->hover_sess ||
                   sub != m->hover_sub || flyout != m->flyout;
    m->hover_title = title;
    m->hover_row = row;
    m->hover_sess = sess;
    m->hover_sub = sub;
    if (flyout != m->flyout) {
        if (flyout >= 0)
            place_flyout(m, flyout);
        else
            m->flyout = -1;
    }
    return changed;
}

sk_menu_press sk_menu_mouse_down(sk_menu *m, double x, double y)
{
    sk_menu_press p = {SK_MENU_PRESS_NOTHING, 0, 0, NULL, false, false, 0};
    hit_t h = hit(m, x, y);
    if (!h.inside) {
        sk_menu_close(m);
        p.kind = SK_MENU_PRESS_OUTSIDE;
        return p;
    }
    if (h.title >= 0) {
        if (h.title != m->shown) {
            p.kind = SK_MENU_PRESS_SHOW;
            p.menu = h.title;
        }
        return p;
    }
    if (h.sess >= 0) {
        p.kind = SK_MENU_PRESS_SESSION;
        p.arg = m->sess[h.sess].key;
        p.waits = true;
        p.press = m->waiting = ++m->presses;
        return p;
    }
    if (h.row < 0)
        return p;
    const sk_menu_row *r = &m->rows[h.row];
    if (h.sub >= 0 && (size_t)h.sub < r->nsub)
        r = &r->sub[h.sub];
    else if (r->kind == SK_ROW_FLYOUT)
        return p;
    if (!acts(r))
        return p;
    if (r->then == SK_ROW_SAYS) {
        say(m, r->arg ? r->arg : "", true);
        return p;
    }
    p.kind = SK_MENU_PRESS_ACTION;
    p.action = r->action;
    p.arg = r->arg;
    if (r->then == SK_ROW_WAITS) {
        p.waits = true;
        p.press = m->waiting = ++m->presses;
        return p;
    }
    /* The row closes the menu: its argument outlives it. */
    free(m->kept);
    m->kept = r->arg ? sk_xstrdup(r->arg) : NULL;
    p.arg = m->kept;
    p.closes = true;
    sk_menu_close(m);
    return p;
}

bool sk_menu_scroll(sk_menu *m, double x, double y, double dy)
{
    hit_t h = hit(m, x, y);
    if (h.in_box) {
        double max = box_max_scroll(m);
        m->box_scroll = SK_MIN(SK_MAX(m->box_scroll + dy * LINE, 0.0), max);
        return true;
    }
    if (h.in_msg) {
        double max = msg_max_scroll(m);
        m->msg_scroll = SK_MIN(SK_MAX(m->msg_scroll + dy * MONO_LINE, 0.0), max);
        return true;
    }
    return h.inside;
}

bool sk_menu_key(sk_menu *m, const sk_key_event *k, int *show)
{
    *show = -1;
    if (!m->open)
        return false;
    if (k->key == SK_KEY_ESCAPE)
        sk_menu_close(m);
    else if (k->key == SK_KEY_LEFT || k->key == SK_KEY_RIGHT)
        *show = (m->shown + (k->key == SK_KEY_LEFT ? SK_MENU_COUNT - 1 : 1)) % SK_MENU_COUNT;
    return true;
}

/* ---- Drawing ---- */

/* A rect in points as the pixels it covers at scale k. */
static sk_rect prect(double k, double x, double y, double w, double h)
{
    int x0 = (int)lround(x * k), y0 = (int)lround(y * k);
    int x1 = (int)lround((x + w) * k), y1 = (int)lround((y + h) * k);
    return sk_rect_make(x0, y0, x1 - x0, y1 - y0);
}

static sk_color faded(sk_color c, bool ended)
{
    return ended ? (c & 0x00ffffffu) | ((uint32_t)ENDED_ALPHA << 24) : c;
}

typedef struct {
    sk_menu *m;
    sk_image *dst;
    double k;
    sk_rect clip;
} pen;

/* A line of text in a line box lh points high at top. */
static float text_at(pen *p, sk_fontset *set, int style, double size, double x, double top, double lh,
                     const char *s, sk_color c, const sk_rect *clip)
{
    float px = (float)(size * p->k);
    float base = sk_baseline_in(set, style, px, (float)(top * p->k), (float)(lh * p->k));
    return sk_draw_text(p->dst, p->m->st.cache, set, style, px, (float)(x * p->k), base, s, strlen(s), c, clip);
}

static void fit_at(pen *p, sk_fontset *set, int style, double size, double x, double top, double lh, double maxw,
                   const char *s, sk_color c, bool at_start, const sk_rect *clip)
{
    float px = (float)(size * p->k);
    float base = sk_baseline_in(set, style, px, (float)(top * p->k), (float)(lh * p->k));
    sk_draw_fit(p->dst, p->m->st.cache, set, style, px, (float)(x * p->k), base, (float)(maxw * p->k), s, c,
                at_start, clip);
}

static void frame(pen *p, double x, double y, double w, double h)
{
    sk_shadow_rect(p->dst, x * p->k, (y + SHADOW_Y) * p->k, w * p->k, h * p->k, SHADOW_BLUR * p->k, C_SHADOW,
                   &p->clip);
    sk_fill_rect(p->dst, prect(p->k, x, y, w, h), C_BORDER, SK_BLEND_SRC_OVER, &p->clip);
    sk_fill_rect(p->dst, prect(p->k, x + 1, y + 1, w - 2, h - 2), C_BG, SK_BLEND_SRC_OVER, &p->clip);
}

static sk_color item_color(const sk_menu_row *r)
{
    return r->disabled ? C_DISABLED : r->current ? C_WHITE : r->problem ? C_PROBLEM : C_TEXT;
}

static void draw_heading(pen *p, const sk_menu_row *r, double x, double y)
{
    char up[256];
    upper(r->text, up, sizeof up);
    double at = x;
    for (size_t i = 0; up[i];) {
        uint32_t cp;
        size_t k = sk_utf8_decode(up + i, strlen(up + i), &cp, NULL);
        k = k ? k : 1;
        char ch[8] = {0};
        memcpy(ch, up + i, k < sizeof ch ? k : sizeof ch - 1);
        at += text_at(p, p->m->st.sans, 0, HEAD_PX, at, y, HEAD_LINE, ch, C_DIM, &p->clip) / p->k + 1;
        i += k;
    }
}

static sk_color state_color(const char *state)
{
    if (!strcmp(state, "working"))
        return C_WORKING;
    if (!strcmp(state, "waiting"))
        return C_WAITING;
    return C_DIM;
}

static void draw_session(pen *p, const sk_menu_session *s, bool ended, bool hover, double x, double y, double w,
                         const sk_rect *clip)
{
    sk_menu *m = p->m;
    if (hover)
        sk_fill_rect(p->dst, prect(p->k, x, y, w, LINE), faded(C_LIT, ended), SK_BLEND_SRC_OVER, clip);
    double at = x + PAD_X;
    double folder = SK_MAX(S_FOLDER_MIN, w - 2 * PAD_X - (S_LABEL + S_STATE + S_AGE + S_WHERE + 4 * S_GAP));
    fit_at(p, m->st.sans, 0, FONT_PX, at, y, LINE, S_LABEL, s->label, faded(C_TEXT, ended), false, clip);
    at += S_LABEL + S_GAP;
    fit_at(p, m->st.sans, 0, FONT_PX, at, y, LINE, folder, s->folder, faded(C_PATH, ended), true, clip);
    at += folder + S_GAP;
    const char *state = ended ? "ended" : s->state;
    text_at(p, m->st.sans, 0, FONT_PX, at, y, LINE, state, faded(state_color(state), ended), clip);
    at += S_STATE + S_GAP;
    double aw = text_w(m, m->st.sans, 0, FONT_PX, s->age);
    text_at(p, m->st.sans, 0, FONT_PX, at + S_AGE - aw, y, LINE, s->age, faded(C_DIM, ended), clip);
    at += S_AGE + S_GAP;
    text_at(p, m->st.sans, 0, FONT_PX, at, y, LINE, s->where, faded(C_WHERE, ended), clip);
}

static void draw_item(pen *p, const sk_menu_row *r, bool lit, double x, double y, double w, const sk_rect *clip)
{
    sk_menu *m = p->m;
    if (lit)
        sk_fill_rect(p->dst, prect(p->k, x, y, w, LINE), C_LIT, SK_BLEND_SRC_OVER, clip);
    sk_color c = item_color(r);
    int style = row_style(r);
    if (r->location) {
        /* .located: the name, then the folder showing its end in what
         * is left, 14 apart. */
        double tw = text_w(m, m->st.sans, style, FONT_PX, r->text);
        text_at(p, m->st.sans, style, FONT_PX, x + PAD_X, y, LINE, r->text, c, clip);
        double lx = x + PAD_X + tw + LOC_GAP;
        fit_at(p, m->st.sans, 0, FONT_PX, lx, y, LINE, SK_MAX(LOC_MIN, x + w - PAD_X - lx), r->location, C_PATH,
               true, clip);
        return;
    }
    text_at(p, m->st.sans, style, FONT_PX, x + PAD_X, y, LINE, r->text, c, clip);
    const char *right = r->kind == SK_ROW_FLYOUT ? ARROW : r->keys;
    if (right) {
        double rw = text_w(m, m->st.sans, 0, FONT_PX, right);
        text_at(p, m->st.sans, 0, FONT_PX, x + w - PAD_X - rw, y, LINE, right, C_DIM, clip);
    }
}

static void draw_flyout(pen *p)
{
    sk_menu *m = p->m;
    const sk_menu_row *r = &m->rows[m->flyout];
    frame(p, m->fx, m->fy, m->fw, m->fh);
    for (size_t i = 0; i < r->nsub; i++) {
        double y = m->fy + 1 + 4 + (double)i * LINE;
        draw_item(p, &r->sub[i], (int)i == m->hover_sub, m->fx + 1, y, m->fw - 2, &p->clip);
    }
}

sk_rect sk_menu_bounds(const sk_menu *m, double scale)
{
    if (!m->open)
        return sk_rect_make(0, 0, 0, 0);
    double ext = 1.5 * SHADOW_BLUR; /* three deviations */
    sk_rect r = prect(scale, m->x - ext, m->y - ext + SHADOW_Y, m->w + 2 * ext, m->h + 2 * ext);
    r = sk_rect_union(r, prect(scale, m->x, m->y, m->w, m->h));
    if (m->flyout >= 0) {
        r = sk_rect_union(r, prect(scale, m->fx - ext, m->fy - ext + SHADOW_Y, m->fw + 2 * ext, m->fh + 2 * ext));
        r = sk_rect_union(r, prect(scale, m->fx, m->fy, m->fw, m->fh));
    }
    r.x -= 1;
    r.y -= 1;
    r.w += 2;
    r.h += 2;
    return r;
}

sk_rect sk_menu_draw(sk_menu *m, sk_image *dst, const sk_menu_style *st)
{
    if (!m->open)
        return sk_rect_make(0, 0, 0, 0);
    if (st->scale != m->st.scale || st->sans != m->st.sans) {
        m->st = *st;
        layout(m, false);
        if (m->flyout >= 0)
            place_flyout(m, m->flyout);
    }
    pen p = {m, dst, st->scale, sk_image_bounds(dst)};
    double x = m->x, y = m->y, inner = m->w - 2;
    frame(&p, x, y, m->w, m->h);
    sk_rect box = prect(p.k, x + 1, y + 1, inner, m->h - 2);
    box = sk_rect_intersect(box, p.clip);
    /* The bar. */
    sk_fill_rect(dst, prect(p.k, x + 1, y + 1, inner, BAR_H), C_BAR, SK_BLEND_SRC_OVER, &box);
    sk_fill_rect(dst, prect(p.k, x + 1, y + 1 + BAR_H, inner, 1), C_LINE, SK_BLEND_SRC_OVER, &box);
    for (int t = 0; t < SK_MENU_COUNT; t++) {
        double tx = x + 1 + m->tx[t];
        bool cur = t == m->shown;
        if (cur || t == m->hover_title)
            sk_fill_rect(dst, prect(p.k, tx, y + 1, m->tw[t], BAR_H), cur ? C_LIT : C_TITLE_HOVER,
                         SK_BLEND_SRC_OVER, &box);
        text_at(&p, st->sans, 0, FONT_PX, tx + PAD_X, y + 2, LINE, sk_menu_titles[t], cur ? C_WHITE : C_TEXT, &box);
    }
    /* The rows. */
    for (size_t i = 0; i < m->n; i++) {
        const sk_menu_row *r = &m->rows[i];
        double ry = y + m->geo[i].y;
        switch (r->kind) {
        case SK_ROW_HEADING:
            draw_heading(&p, r, x + 1 + PAD_X, ry);
            break;
        case SK_ROW_RULE:
            sk_fill_rect(dst, prect(p.k, x + 1, ry + 4, inner, 1), C_LINE, SK_BLEND_SRC_OVER, &box);
            break;
        case SK_ROW_SESSIONS: {
            sk_rect clip = sk_rect_intersect(box, prect(p.k, x + 1, ry, inner, m->box_h));
            if (!m->nsess)
                text_at(&p, st->sans, 0, FONT_PX, x + 1 + PAD_X, ry, LINE, "none running", C_DIM, &clip);
            for (size_t k = 0; k < m->nsess; k++) {
                double sy = ry + (double)k * LINE - m->box_scroll;
                if (sy + LINE <= ry || sy >= ry + m->box_h)
                    continue;
                draw_session(&p, &m->sess[k], m->ended[k], (int)k == m->hover_sess, x + 1, sy, inner, &clip);
            }
            /* A thumb for a box that scrolls. */
            double max = box_max_scroll(m);
            if (max > 0) {
                double th = SK_MAX(16.0, m->box_h * m->box_h / box_content_h(m));
                double ty = ry + (m->box_h - th) * m->box_scroll / max;
                sk_fill_rect(dst, prect(p.k, x + 1 + inner - 6, ty + 1, 4, th - 2), SK_RGBA(0xff, 0xff, 0xff, 0x50),
                             SK_BLEND_SRC_OVER, &clip);
            }
            break;
        }
        default: {
            bool lit = (int)i == m->hover_row || (int)i == m->flyout;
            draw_item(&p, r, lit && lights(r), x + 1, ry, inner, &box);
            break;
        }
        }
    }
    /* The message. */
    if (m->message) {
        double mx = x + 1 + MSG_MARGIN_X, my = y + m->msg_y, mw = inner - 2 * MSG_MARGIN_X;
        sk_rect r = prect(p.k, mx, my, mw, m->msg_h);
        sk_fill_rect(dst, r, m->error ? C_ERROR_BG : C_NOTE_BG, SK_BLEND_SRC_OVER, &box);
        sk_rect clip = sk_rect_intersect(box, prect(p.k, mx, my + MSG_PAD, mw, m->msg_h - 2 * MSG_PAD));
        for (size_t i = 0; i < m->nlines; i++) {
            double ly = my + MSG_PAD + (double)i * MONO_LINE - m->msg_scroll;
            if (ly + MONO_LINE <= my || ly >= my + m->msg_h)
                continue;
            text_at(&p, st->mono, 0, MONO_PX, mx + MSG_PAD, ly, MONO_LINE, m->lines[i],
                    m->error ? C_PROBLEM : C_NOTE, &clip);
        }
    }
    if (m->flyout >= 0)
        draw_flyout(&p);
    return sk_rect_intersect(sk_menu_bounds(m, st->scale), sk_image_bounds(dst));
}

/* ---- For the live checks ---- */

static void row_json(sk_jw *w, const sk_menu *m, const sk_menu_row *r, const span *g)
{
    static const char *const KINDS[] = {"item", "heading", "rule", "flyout", "sessions"};
    sk_jw_object(w);
    sk_jw_key(w, SK_STR("kind"));
    sk_jw_string(w, sk_str_c(KINDS[r->kind]));
    sk_jw_key(w, SK_STR("text"));
    if (r->text)
        sk_jw_string(w, sk_str_c(r->text));
    else
        sk_jw_null(w);
    sk_jw_key(w, SK_STR("keys"));
    if (r->keys)
        sk_jw_string(w, sk_str_c(r->keys));
    else
        sk_jw_null(w);
    sk_jw_key(w, SK_STR("disabled"));
    sk_jw_bool(w, r->disabled);
    sk_jw_key(w, SK_STR("current"));
    sk_jw_bool(w, r->current);
    if (g) {
        sk_jw_key(w, SK_STR("y"));
        sk_jw_number(w, m->y + g->y);
        sk_jw_key(w, SK_STR("h"));
        sk_jw_number(w, g->h);
    }
    sk_jw_object_end(w);
}

void sk_menu_state(const sk_menu *m, sk_jw *w)
{
    sk_jw_object(w);
    sk_jw_key(w, SK_STR("open"));
    sk_jw_bool(w, m->open);
    sk_jw_key(w, SK_STR("shown"));
    sk_jw_string(w, sk_str_c(sk_menu_titles[m->shown]));
    if (m->open) {
        sk_jw_key(w, SK_STR("x"));
        sk_jw_number(w, m->x);
        sk_jw_key(w, SK_STR("y"));
        sk_jw_number(w, m->y);
        sk_jw_key(w, SK_STR("w"));
        sk_jw_number(w, m->w);
        sk_jw_key(w, SK_STR("h"));
        sk_jw_number(w, m->h);
    }
    sk_jw_key(w, SK_STR("rows"));
    sk_jw_array(w);
    for (size_t i = 0; i < m->n; i++)
        row_json(w, m, &m->rows[i], &m->geo[i]);
    sk_jw_array_end(w);
    sk_jw_key(w, SK_STR("sessions"));
    sk_jw_array(w);
    for (size_t i = 0; i < m->nsess; i++) {
        sk_jw_object(w);
        sk_jw_key(w, SK_STR("key"));
        sk_jw_string(w, sk_str_c(m->sess[i].key));
        sk_jw_key(w, SK_STR("label"));
        sk_jw_string(w, sk_str_c(m->sess[i].label));
        sk_jw_key(w, SK_STR("ended"));
        sk_jw_bool(w, m->ended[i]);
        sk_jw_object_end(w);
    }
    sk_jw_array_end(w);
    sk_jw_key(w, SK_STR("message"));
    if (m->message)
        sk_jw_string(w, sk_str_c(m->message));
    else
        sk_jw_null(w);
    sk_jw_key(w, SK_STR("error"));
    sk_jw_bool(w, m->message && m->error);
    sk_jw_key(w, SK_STR("flyout"));
    if (m->flyout >= 0) {
        sk_jw_array(w);
        const sk_menu_row *r = &m->rows[m->flyout];
        for (size_t i = 0; i < r->nsub; i++)
            row_json(w, m, &r->sub[i], NULL);
        sk_jw_array_end(w);
    } else {
        sk_jw_null(w);
    }
    sk_jw_object_end(w);
}
