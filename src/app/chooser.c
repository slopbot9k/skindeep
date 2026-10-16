/* Port of app/renderer/chooser.js and the #chooser rules of
 * app/renderer/app.css (decision 0025): the dialog laid out as the
 * page's flexbox lays it out, in CSS pixels, which are the window's
 * points, and drawn at the window's scale, with the controls Chromium
 * would have given the page. */
#include "chooser.h"

#include "../base/utf8.h"
#include "identity.h"
#include "js.h"

#include <math.h>
#include <string.h>

/* app.css's #chooser, in CSS pixels. */
#define TOP_PAD 48      /* #chooser's padding-top, above the dialog */
#define DIALOG_W 640    /* .dialog: min(640px, 100% - 32px) */
#define SIDE 32
#define BORDER 1
#define PAD_T 10        /* padding: 10px 14px 12px */
#define PAD_X 14
#define PAD_B 12
#define FONT 13.0       /* font: 13px/1.6 "DejaVu Sans" */
#define LH 1.6
#define TITLE_MB 6
#define ROW_M 6         /* .row: margin 6px 0, gap 8px */
#define GAP 8
#define NAME_W 72       /* .name */
#define AGENTS_GAP 18   /* .agents */
#define CTL_FONT 12.0   /* inputs, the select and buttons: 12px/1.6 */
#define CTL_PAD_Y 1
#define FIELD_PAD 4     /* padding: 1px 4px */
#define BUTTON_PAD 10   /* padding: 1px 10px */
#define HINT_X 80       /* .hint's margin-left */
#define HINT_FONT 11.0
#define ERROR_LH 1.4    /* .error: 11px/1.4 */
#define ERROR_PAD 6
#define ERROR_MT 6
#define BUTTONS_MT 10
#define SHADOW_Y 4      /* box-shadow: 0 4px 16px #000a */
#define SHADOW_BLUR 16
/* Chromium's: a radio 13 wide, margin 3px 3px 0 5px, its bottom on the
 * baseline; a focus ring 2 wide; a select's arrow in its right 16. */
#define RADIO 13
#define RADIO_ML 5
#define RADIO_MR 3
#define RADIO_MT 3
#define RING 2
#define ARROW_W 16
/* The select's list: rows, and how many show before it scrolls. */
#define LIST_ROW 20.0
#define LIST_MAX 12

#define COL_OVERLAY SK_RGBA(0, 0, 0, 0xaa)
#define COL_SHADOW SK_RGBA(0, 0, 0, 0xaa)
#define COL_DIALOG SK_RGB(0x1b, 0x1d, 0x21)
#define COL_TEXT SK_RGB(0xd8, 0xdd, 0xe3)
#define COL_BORDER SK_RGB(0x4a, 0x50, 0x5a)
#define COL_TITLE SK_RGB(0xff, 0xff, 0xff)
#define COL_NAME SK_RGB(0x8a, 0x92, 0x9c)
#define COL_FIELD SK_RGB(0x0f, 0x11, 0x14)
#define COL_FIELD_TEXT SK_RGB(0xe6, 0xe9, 0xed)
#define COL_BUTTON SK_RGB(0x2a, 0x2e, 0x35)
#define COL_HOVER SK_RGB(0x2f, 0x5d, 0x8a)
#define COL_DISABLED SK_RGB(0x66, 0x66, 0x66)
#define COL_DROPPED SK_RGB(0xe0, 0xb0, 0x50)
#define COL_ERROR_BG SK_RGB(0x3a, 0x16, 0x16)
#define COL_ERROR SK_RGB(0xff, 0xb4, 0xb4)
/* What the page leaves to Chromium: the focus ring, the selection, and
 * the radios of a page with no color-scheme, light. */
#define COL_RING SK_RGB(0x6f, 0xa8, 0xdc)
#define COL_SELECTION SK_RGB(0x2f, 0x5d, 0x8a)
#define COL_LIST_TEXT SK_RGB(0xff, 0xff, 0xff)
#define COL_RADIO_FACE SK_RGB(0xff, 0xff, 0xff)
#define COL_RADIO_EDGE SK_RGB(0x76, 0x76, 0x76)
#define COL_RADIO_EDGE_HOVER SK_RGB(0x4f, 0x4f, 0x4f)
#define COL_RADIO_ON SK_RGB(0x00, 0x75, 0xff)
#define COL_RADIO_ON_HOVER SK_RGB(0x00, 0x5c, 0xc8)

static const char HINT[] = "No shell runs the line: quotes and backslashes work, ~ and $VAR do not.";
static const char DROPPED[] = "Not carried from this window: ";

/* The controls, in the order Tab takes them. */
enum {
    K_AGENT, /* one radio per agent */
    K_PROFILE = K_AGENT + SK_PROFILE_AGENTS,
    K_OTHER,
    K_FORGET,
    K_FOLDER,
    K_CHOOSE,
    K_LINE,
    K_CANCEL,
    K_OPEN,
    K_COUNT,
    K_LIST = K_COUNT, /* the select's list, open */
};

static const char *const LABELS[K_COUNT] = {
    [K_OTHER] = "Other profile...", [K_FORGET] = "Forget", [K_CHOOSE] = "Choose...",
    [K_CANCEL] = "Cancel",          [K_OPEN] = "Open",
};

static const char *const NAMES[K_COUNT] = {
    [K_PROFILE] = "profile", [K_OTHER] = "other", [K_FORGET] = "forget", [K_FOLDER] = "folder",
    [K_CHOOSE] = "choose",   [K_LINE] = "line",   [K_CANCEL] = "cancel", [K_OPEN] = "open",
};

static const char *const ROWS[] = {"Agent", "Profile", "Folder", "Command"};

typedef struct {
    double x, y, w, h;
} box;

/* A line of wrapped text: a byte range of its source. */
typedef struct {
    size_t at, n;
} span;
typedef SK_VEC(span) spans;

/* Where the last draw put everything, in points. */
typedef struct {
    box dialog;
    double title_y;
    box row[4];
    box label[SK_PROFILE_AGENTS]; /* a radio with its name: what a click takes */
    box radio[SK_PROFILE_AGENTS];
    double agent_base;
    box ctl[K_COUNT];
    sk_buf env, dropped;
    box env_box, dropped_box, hint_box, error_box; /* h 0: hidden */
    spans env_l, dropped_l, hint_l, error_l;
    box list;
    size_t list_rows;
} layout;

/* A text field, as field.c edits it. */
typedef sk_field field;

typedef enum { PICK_FOLDER, PICK_PROFILE } pick_what;

struct sk_chooser {
    sk_chooser_config cfg;
    bool open;
    sk_newtab_state st;
    sk_profile_agent agent;
    size_t prof; /* the profile chosen, in the agent's list */
    field folder, line;
    char *error;
    char *preedit;
    int focus, hover, pressed;
    bool ring;      /* the focus came by the keyboard: a button, a radio and the select show it */
    bool selecting; /* the left button, pressed in the focused field, drags its selection */
    bool list;      /* the profile list is open */
    size_t hot, top; /* its highlighted row, and its first row shown */
    /* The answers this dialog waits for, from the picker and the
     * clipboard: tokens, 0 for none. */
    uintptr_t pick_token, paste_token;
    pick_what picking;
    sk_cursor cursor;
    bool laid;
    sk_chooser_style style;
    sk_rect area;
    layout lay;
};

/* The dialogs alive, which a late answer finds by its token. The UI
 * thread's. */
static SK_VEC(sk_chooser *) alive;
static uintptr_t tokens;

/* ---- The dialog's model, as chooser.js keeps it ---- */

static sk_newtab_profiles *profiles_of(sk_chooser *c)
{
    return &c->st.agents[c->agent].profiles;
}

static const sk_newtab_profile *chosen(const sk_chooser *c)
{
    const sk_newtab_profiles *p = &c->st.agents[c->agent].profiles;
    return c->prof < p->len ? &p->ptr[c->prof] : NULL;
}

static bool forget_enabled(const sk_chooser *c)
{
    const sk_newtab_profile *p = chosen(c);
    return p && p->remembered;
}

static bool is_field(int k)
{
    return k == K_FOLDER || k == K_LINE;
}

static bool is_button(int k)
{
    return k == K_OTHER || k == K_FORGET || k == K_CHOOSE || k == K_CANCEL || k == K_OPEN;
}

static field *field_of(sk_chooser *c, int k)
{
    return k == K_FOLDER ? &c->folder : k == K_LINE ? &c->line : NULL;
}

static void say(sk_chooser *c, const char *text)
{
    free(c->error);
    c->error = text && *text ? sk_xstrdup(text) : NULL;
}

/* fillProfiles: the select on dir, else on the first. */
static void fill_profiles(sk_chooser *c, const char *dir)
{
    sk_newtab_profiles *p = profiles_of(c);
    c->prof = 0;
    for (size_t i = 0; dir && i < p->len; i++) {
        if (!strcmp(p->ptr[i].dir, dir)) {
            c->prof = i;
            break;
        }
    }
}

/* pickAgent: its radio checked, its profile, its command line, no
 * error. */
static void pick_agent(sk_chooser *c, sk_profile_agent a)
{
    c->agent = a;
    fill_profiles(c, c->st.agents[a].profile);
    sk_field_set(&c->line, c->st.agents[a].line);
    say(c, NULL);
}

/* The variable the tab runs with (showProfile). */
static void env_text(const sk_chooser *c, sk_buf *out)
{
    const char *v = sk_profile_agents[c->agent].variable;
    const sk_newtab_profile *p = chosen(c);
    if (p && p->is_default)
        sk_buf_printf(out, "%s unset, the default profile", v);
    else
        sk_buf_printf(out, "%s=%s", v, p ? p->shown : "");
}

/* An option's text, its white space collapsed as an option's text is. */
static void option_text(const sk_newtab_profile *p, sk_buf *out)
{
    sk_buf_append_c(out, p->shown);
    if (p->is_default)
        sk_buf_append_c(out, " (default)");
}

/* ---- Layout ---- */

static double line_h(double size)
{
    return size * LH;
}

static double ctl_h(void)
{
    return line_h(CTL_FONT) + 2 * (CTL_PAD_Y + BORDER);
}

static double measure(sk_fontset *set, int style, double size, const char *s, size_t n)
{
    return sk_measure_text(set, style, (float)size, s, n);
}

/* The baseline in a line box lh high, the font's ascent and descent
 * centred in it, as CSS sets a line. */
static double base_in(sk_fontset *set, int style, double size, double lh)
{
    sk_font_vmetrics m;
    sk_font_vmetrics_at(sk_fontset_primary(set, style), (float)size, &m);
    return (lh - (m.ascent + m.descent)) / 2 + m.ascent;
}

static double button_w(const sk_chooser *c, int k)
{
    const char *s = LABELS[k];
    return measure(c->style.sans, 0, CTL_FONT, s, strlen(s)) + 2 * (BUTTON_PAD + BORDER);
}

/* n bytes of s wrapped to w, as CSS breaks a line: at the last space
 * that fits, else after the last character that does (overflow-wrap:
 * anywhere, which the hints that hold paths have), and at each \n,
 * which pre-wrap keeps. A line always takes a character. */
static void wrap(sk_fontset *set, int style, double size, double w, const char *s, size_t n, spans *out)
{
    sk_buf t = {0};
    sk_buf_append(&t, s, n);
    for (size_t i = 0;;) {
        size_t end = i;
        while (end < n && s[end] != '\n')
            end++;
        size_t a = i;
        do {
            size_t fit = a, space = SIZE_MAX;
            for (size_t j = a; j < end;) {
                size_t k = sk_field_next(&t, j);
                if (j > a && measure(set, style, size, s + a, k - a) > w + 0.01)
                    break;
                if (s[j] == ' ')
                    space = j;
                fit = k;
                j = k;
            }
            if (fit >= end) {
                sk_vec_push(out, ((span){a, end - a}));
                break;
            }
            size_t cut = space != SIZE_MAX && space > a ? space : fit;
            sk_vec_push(out, ((span){a, cut - a}));
            a = cut;
            while (a < end && s[a] == ' ')
                a++;
        } while (a < end);
        if (end >= n)
            break;
        i = end + 1;
    }
    sk_buf_free(&t);
}

static void layout_free(layout *L)
{
    sk_buf_free(&L->env);
    sk_buf_free(&L->dropped);
    sk_vec_free(&L->env_l);
    sk_vec_free(&L->dropped_l);
    sk_vec_free(&L->hint_l);
    sk_vec_free(&L->error_l);
}

/* The select's list below it, or above it when the window has more room
 * there, its highlighted row in view. */
static void lay_list(sk_chooser *c)
{
    layout *L = &c->lay;
    double s = c->style.scale;
    size_t n = profiles_of(c)->len;
    box sel = L->ctl[K_PROFILE];
    double top = c->area.y / s, bottom = (c->area.y + c->area.h) / s;
    double below = bottom - (sel.y + sel.h) - 2, above = sel.y - top - 2;
    size_t want = SK_MIN(n, (size_t)LIST_MAX);
    size_t fit_below = below > 0 ? (size_t)(below / LIST_ROW) : 0;
    size_t fit_above = above > 0 ? (size_t)(above / LIST_ROW) : 0;
    bool up = fit_below < want && fit_above > fit_below;
    size_t rows = SK_MAX((size_t)1, SK_MIN(want, up ? fit_above : fit_below));
    double h = (double)rows * LIST_ROW + 2 * BORDER;
    L->list = (box){sel.x, up ? sel.y - h : sel.y + sel.h, sel.w, h};
    L->list_rows = rows;
    if (c->hot < c->top)
        c->top = c->hot;
    if (c->hot >= c->top + rows)
        c->top = c->hot - rows + 1;
    if (c->top + rows > n)
        c->top = n > rows ? n - rows : 0;
}

static void scroll_field(sk_chooser *c, int k);

/* The dialog as the page lays it out: .dialog centred 48 below the
 * overlay's top, its rows, hints, error and buttons in the flow. */
static void lay_out(sk_chooser *c)
{
    layout *L = &c->lay;
    layout_free(L);
    sk_fontset *sans = c->style.sans, *mono = c->style.mono;
    double s = c->style.scale;
    double ax = c->area.x / s, ay = c->area.y / s, aw = c->area.w / s;
    double dw = SK_MAX(SK_MIN((double)DIALOG_W, aw - SIDE), 2.0 * (BORDER + PAD_X) + HINT_X + 40);
    double dx = ax + (aw - dw) / 2, dy = ay + TOP_PAD;
    double x0 = dx + BORDER + PAD_X, cw = dw - 2 * (BORDER + PAD_X), sx = x0 + NAME_W + GAP;
    double ch = ctl_h(), lh = line_h(FONT), hint_lh = line_h(HINT_FONT);
    double y = dy + BORDER + PAD_T;
    L->title_y = y;
    y += lh + TITLE_MB;

    /* Agent: each radio's margin box ends on the baseline, so a line box
     * of a radio may reach above the strut's. */
    double base = base_in(sans, 0, FONT, lh);
    double over = SK_MAX(0.0, RADIO_MT + RADIO - base);
    L->row[0] = (box){x0, y, cw, lh + over};
    L->agent_base = y + over + base;
    double lx = sx;
    for (int a = 0; a < SK_PROFILE_AGENTS; a++) {
        sk_buf t = {0};
        sk_buf_printf(&t, " %s", sk_profile_agents[a].name);
        double w = RADIO_ML + RADIO + RADIO_MR + measure(sans, 0, FONT, t.ptr, t.len);
        sk_buf_free(&t);
        L->label[a] = (box){lx, y, w, lh + over};
        L->radio[a] = (box){lx + RADIO_ML, L->agent_base - RADIO, RADIO, RADIO};
        lx += w + AGENTS_GAP;
    }
    y += L->row[0].h + ROW_M;

    /* Profile: the select takes what the buttons leave. */
    L->row[1] = (box){x0, y, cw, ch};
    double bf = button_w(c, K_FORGET), bo = button_w(c, K_OTHER);
    L->ctl[K_FORGET] = (box){x0 + cw - bf, y, bf, ch};
    L->ctl[K_OTHER] = (box){L->ctl[K_FORGET].x - GAP - bo, y, bo, ch};
    L->ctl[K_PROFILE] = (box){sx, y, SK_MAX(0.0, L->ctl[K_OTHER].x - GAP - sx), ch};
    y += ch + ROW_M;

    env_text(c, &L->env);
    wrap(mono, 0, HINT_FONT, cw - HINT_X, L->env.ptr, L->env.len, &L->env_l);
    L->env_box = (box){x0 + HINT_X, y, cw - HINT_X, (double)L->env_l.len * hint_lh};
    y += L->env_box.h + ROW_M;

    L->row[2] = (box){x0, y, cw, ch};
    double bc = button_w(c, K_CHOOSE);
    L->ctl[K_CHOOSE] = (box){x0 + cw - bc, y, bc, ch};
    L->ctl[K_FOLDER] = (box){sx, y, SK_MAX(0.0, L->ctl[K_CHOOSE].x - GAP - sx), ch};
    y += ch + ROW_M;

    L->row[3] = (box){x0, y, cw, ch};
    L->ctl[K_LINE] = (box){sx, y, x0 + cw - sx, ch};
    y += ch + ROW_M;

    const char *dropped = c->st.agents[c->agent].dropped;
    L->dropped_box = (box){x0 + HINT_X, y, cw - HINT_X, 0};
    if (dropped && *dropped) {
        sk_buf_printf(&L->dropped, "%s%s", DROPPED, dropped);
        wrap(mono, 0, HINT_FONT, cw - HINT_X, L->dropped.ptr, L->dropped.len, &L->dropped_l);
        L->dropped_box.h = (double)L->dropped_l.len * hint_lh;
        y += L->dropped_box.h;
    }
    wrap(sans, 0, HINT_FONT, cw - HINT_X, HINT, strlen(HINT), &L->hint_l);
    L->hint_box = (box){x0 + HINT_X, y, cw - HINT_X, (double)L->hint_l.len * hint_lh};
    y += L->hint_box.h;

    L->error_box = (box){x0 + HINT_X, y, cw - HINT_X, 0};
    if (c->error) {
        y += ERROR_MT;
        wrap(mono, 0, HINT_FONT, cw - HINT_X - 2 * ERROR_PAD, c->error, strlen(c->error), &L->error_l);
        double lines = (double)L->error_l.len * HINT_FONT * ERROR_LH;
        L->error_box = (box){x0 + HINT_X, y, cw - HINT_X, 2 * ERROR_PAD + lines};
        y += L->error_box.h;
    }

    y += BUTTONS_MT;
    double bp = button_w(c, K_OPEN), bn = button_w(c, K_CANCEL);
    L->ctl[K_OPEN] = (box){x0 + cw - bp, y, bp, ch};
    L->ctl[K_CANCEL] = (box){L->ctl[K_OPEN].x - GAP - bn, y, bn, ch};
    y += ch + PAD_B + BORDER;
    L->dialog = (box){dx, dy, dw, y - dy};

    for (int a = 0; a < SK_PROFILE_AGENTS; a++)
        L->ctl[K_AGENT + a] = L->label[a];
    scroll_field(c, K_FOLDER);
    scroll_field(c, K_LINE);
    if (c->list)
        lay_list(c);
}

static void relayout(sk_chooser *c)
{
    if (c->open && c->laid)
        lay_out(c);
}

/* Whether field k shows an input method's composition. */
static bool composing(const sk_chooser *c, int k)
{
    return c->focus == k && c->preedit && *c->preedit;
}

/* What a field shows: its text, with a composition in place of the
 * selection, which it replaces once committed, and where the caret is
 * in that. */
static void shown_text(const sk_chooser *c, int k, sk_buf *out, size_t *caret)
{
    const field *f = k == K_FOLDER ? &c->folder : &c->line;
    if (!composing(c, k)) {
        sk_buf_append(out, f->text.ptr, f->text.len);
        *caret = f->caret;
        return;
    }
    sk_buf_append(out, f->text.ptr, sk_field_sel_lo(f));
    sk_buf_append_c(out, c->preedit);
    *caret = out->len;
    sk_buf_append(out, f->text.ptr + sk_field_sel_hi(f), f->text.len - sk_field_sel_hi(f));
}

/* A field scrolls to keep its caret in view while it has the focus, and
 * back to its start when it loses it. */
static void scroll_field(sk_chooser *c, int k)
{
    field *f = field_of(c, k);
    if (c->focus != k) {
        f->scroll = 0;
        return;
    }
    double room = c->lay.ctl[k].w - 2 * (BORDER + FIELD_PAD);
    sk_buf t = {0};
    size_t at;
    shown_text(c, k, &t, &at);
    double whole = measure(c->style.mono, 0, CTL_FONT, t.ptr, t.len);
    double x = measure(c->style.mono, 0, CTL_FONT, t.ptr, at);
    sk_buf_free(&t);
    if (x - f->scroll > room)
        f->scroll = x - room;
    if (x - f->scroll < 0)
        f->scroll = x;
    if (whole - f->scroll < room)
        f->scroll = SK_MAX(0.0, whole - room);
}

static bool inside(box b, double x, double y)
{
    return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

/* The control at (x, y), K_LIST for the open list, or -1. */
static int hit(const sk_chooser *c, double x, double y)
{
    const layout *L = &c->lay;
    if (c->list && inside(L->list, x, y))
        return K_LIST;
    for (int k = 0; k < K_COUNT; k++)
        if (inside(L->ctl[k], x, y))
            return k;
    return -1;
}

/* The list's row at y, or SIZE_MAX. */
static size_t row_at(const sk_chooser *c, double y)
{
    const layout *L = &c->lay;
    double r = (y - L->list.y - BORDER) / LIST_ROW;
    if (r < 0 || r >= (double)L->list_rows)
        return SIZE_MAX;
    size_t i = c->top + (size_t)r;
    return i < c->st.agents[c->agent].profiles.len ? i : SIZE_MAX;
}

/* The character boundary of field k nearest to x. */
static size_t offset_at(sk_chooser *c, int k, double x)
{
    field *f = field_of(c, k);
    double want = x - (c->lay.ctl[k].x + BORDER + FIELD_PAD - f->scroll);
    size_t best = 0;
    double best_d = fabs(want);
    for (size_t i = sk_field_next(&f->text, 0); i > 0 && i <= f->text.len; i = sk_field_next(&f->text, i)) {
        double d = fabs(want - measure(c->style.mono, 0, CTL_FONT, f->text.ptr, i));
        if (d < best_d) {
            best = i;
            best_d = d;
        }
        if (i == f->text.len)
            break;
    }
    return best;
}

/* ---- Drawing ---- */

static sk_rect px_of(const sk_chooser *c, box b)
{
    double s = c->style.scale;
    int left = (int)lround(b.x * s), top = (int)lround(b.y * s);
    int right = (int)lround((b.x + b.w) * s), bottom = (int)lround((b.y + b.h) * s);
    return sk_rect_make(left, top, right - left, bottom - top);
}

static int line_px(const sk_chooser *c)
{
    return SK_MAX(1, (int)lround(c->style.scale));
}

static float text_at(const sk_chooser *c, sk_image *dst, sk_fontset *set, int style, double size, double x,
                     double base, const char *s, size_t n, sk_color col, const sk_rect *clip)
{
    double sc = c->style.scale;
    return sk_draw_text(dst, c->style.cache, set, style, (float)(size * sc), (float)(x * sc), (float)(base * sc), s,
                        n, col, clip);
}

/* A box with a border of one point. */
static void framed(const sk_chooser *c, sk_image *dst, box b, sk_color edge, sk_color face, const sk_rect *clip)
{
    sk_rect r = px_of(c, b);
    int lw = line_px(c);
    sk_fill_rect(dst, r, edge, SK_BLEND_SRC_OVER, clip);
    sk_fill_rect(dst, sk_rect_make(r.x + lw, r.y + lw, r.w - 2 * lw, r.h - 2 * lw), face, SK_BLEND_SRC_OVER, clip);
}

/* The focus ring: RING points around the box. */
static void ring_around(const sk_chooser *c, sk_image *dst, box b, const sk_rect *clip)
{
    sk_rect r = px_of(c, b);
    int w = (int)lround(RING * c->style.scale);
    sk_fill_rect(dst, sk_rect_make(r.x - w, r.y - w, r.w + 2 * w, w), COL_RING, SK_BLEND_SRC_OVER, clip);
    sk_fill_rect(dst, sk_rect_make(r.x - w, r.y + r.h, r.w + 2 * w, w), COL_RING, SK_BLEND_SRC_OVER, clip);
    sk_fill_rect(dst, sk_rect_make(r.x - w, r.y, w, r.h), COL_RING, SK_BLEND_SRC_OVER, clip);
    sk_fill_rect(dst, sk_rect_make(r.x + r.w, r.y, w, r.h), COL_RING, SK_BLEND_SRC_OVER, clip);
}

static void disc(sk_image *dst, double cx, double cy, double r, sk_color col, const sk_rect *clip)
{
    sk_path *p = sk_path_new();
    sk_path_ellipse(p, (float)(cx - r), (float)(cy - r), (float)(2 * r), (float)(2 * r));
    sk_fill_path(dst, p, NULL, SK_FILL_NONZERO, col, clip);
    sk_path_free(p);
}

/* box-shadow: 0 SHADOW_Y SHADOW_BLUR of b: the box moved down and
 * blurred by a Gaussian of half the blur radius, as CSS defines it. */
static void shadow(const sk_chooser *c, sk_image *dst, box b, const sk_rect *clip)
{
    double s = c->style.scale, sigma = SHADOW_BLUR / 2.0 * s, k = 1 / (sigma * sqrt(2.0));
    double l = b.x * s, r = (b.x + b.w) * s, t = (b.y + SHADOW_Y) * s, u = (b.y + b.h + SHADOW_Y) * s;
    sk_rect all = sk_rect_make((int)floor(l - 3 * sigma), (int)floor(t - 3 * sigma), 0, 0);
    all.w = (int)ceil(r + 3 * sigma) - all.x;
    all.h = (int)ceil(u + 3 * sigma) - all.y;
    all = sk_rect_intersect(all, clip ? sk_rect_intersect(*clip, sk_image_bounds(dst)) : sk_image_bounds(dst));
    if (sk_rect_empty(all))
        return;
    double *fx = sk_xcalloc((size_t)all.w, sizeof *fx), *fy = sk_xcalloc((size_t)all.h, sizeof *fy);
    for (int i = 0; i < all.w; i++) {
        double x = all.x + i + 0.5;
        fx[i] = 0.5 * (erf((x - l) * k) - erf((x - r) * k));
    }
    for (int j = 0; j < all.h; j++) {
        double y = all.y + j + 0.5;
        fy[j] = 0.5 * (erf((y - t) * k) - erf((y - u) * k));
    }
    uint8_t *a = sk_xmalloc((size_t)all.w * (size_t)all.h);
    for (int j = 0; j < all.h; j++)
        for (int i = 0; i < all.w; i++)
            a[(size_t)j * (size_t)all.w + (size_t)i] = (uint8_t)lround(255 * fx[i] * fy[j]);
    sk_mask m = {a, all.w, all.h, all.w};
    sk_fill_mask(dst, all.x, all.y, &m, COL_SHADOW, NULL);
    free(a);
    free(fx);
    free(fy);
}

static void draw_radio(const sk_chooser *c, sk_image *dst, int a, const sk_rect *clip)
{
    const layout *L = &c->lay;
    double s = c->style.scale;
    box b = L->radio[a];
    double cx = (b.x + b.w / 2) * s, cy = (b.y + b.h / 2) * s, r = b.w / 2 * s;
    bool on = (int)c->agent == a, hover = c->hover == K_AGENT + a;
    if (c->focus == K_AGENT + a && c->ring) {
        sk_path *p = sk_path_new();
        double o = r + RING * s;
        sk_path_ellipse(p, (float)(cx - o), (float)(cy - o), (float)(2 * o), (float)(2 * o));
        sk_path_ellipse(p, (float)(cx - r), (float)(cy - r), (float)(2 * r), (float)(2 * r));
        sk_fill_path(dst, p, NULL, SK_FILL_EVENODD, COL_RING, clip);
        sk_path_free(p);
    }
    sk_color edge = on ? (hover ? COL_RADIO_ON_HOVER : COL_RADIO_ON) : (hover ? COL_RADIO_EDGE_HOVER : COL_RADIO_EDGE);
    disc(dst, cx, cy, r, edge, clip);
    disc(dst, cx, cy, r - s, COL_RADIO_FACE, clip);
    if (on)
        disc(dst, cx, cy, r - 3 * s, edge, clip);
    sk_buf t = {0};
    sk_buf_printf(&t, " %s", sk_profile_agents[a].name);
    text_at(c, dst, c->style.sans, 0, FONT, b.x + RADIO + RADIO_MR, L->agent_base, t.ptr, t.len, COL_TEXT, clip);
    sk_buf_free(&t);
}

static void draw_button(const sk_chooser *c, sk_image *dst, int k, const sk_rect *clip)
{
    box b = c->lay.ctl[k];
    bool off = k == K_FORGET && !forget_enabled(c);
    sk_color face = k == K_OPEN || (c->hover == k && !off) ? COL_HOVER : COL_BUTTON;
    framed(c, dst, b, COL_BORDER, face, clip);
    const char *s = LABELS[k];
    double w = measure(c->style.sans, 0, CTL_FONT, s, strlen(s));
    double base = b.y + BORDER + CTL_PAD_Y + base_in(c->style.sans, 0, CTL_FONT, line_h(CTL_FONT));
    text_at(c, dst, c->style.sans, 0, CTL_FONT, b.x + (b.w - w) / 2, base, s, strlen(s), off ? COL_DISABLED : COL_TEXT,
            clip);
    if (c->focus == k && c->ring)
        ring_around(c, dst, b, clip);
}

static double ctl_base(const sk_chooser *c, box b)
{
    return b.y + BORDER + CTL_PAD_Y + base_in(c->style.mono, 0, CTL_FONT, line_h(CTL_FONT));
}

static void draw_select(const sk_chooser *c, sk_image *dst, const sk_rect *clip)
{
    box b = c->lay.ctl[K_PROFILE];
    double s = c->style.scale;
    framed(c, dst, b, COL_BORDER, COL_FIELD, clip);
    const sk_newtab_profile *p = chosen(c);
    box text = {b.x + BORDER + FIELD_PAD, b.y + BORDER, b.w - 2 * (BORDER + FIELD_PAD) - ARROW_W, b.h - 2 * BORDER};
    sk_rect in = sk_rect_intersect(px_of(c, text), *clip);
    if (p) {
        sk_buf t = {0};
        option_text(p, &t);
        text_at(c, dst, c->style.mono, 0, CTL_FONT, b.x + BORDER + FIELD_PAD, ctl_base(c, b), t.ptr, t.len,
                COL_FIELD_TEXT, &in);
        sk_buf_free(&t);
    }
    /* The arrow, a small triangle pointing down. */
    double ax = (b.x + b.w - BORDER - ARROW_W / 2.0) * s, ay = (b.y + b.h / 2) * s;
    sk_path *path = sk_path_new();
    sk_path_move(path, (float)(ax - 3.5 * s), (float)(ay - 1.75 * s));
    sk_path_line(path, (float)(ax + 3.5 * s), (float)(ay - 1.75 * s));
    sk_path_line(path, (float)ax, (float)(ay + 1.75 * s));
    sk_path_close(path);
    sk_fill_path(dst, path, NULL, SK_FILL_NONZERO, COL_FIELD_TEXT, clip);
    sk_path_free(path);
    if (c->focus == K_PROFILE && (c->ring || c->list))
        ring_around(c, dst, b, clip);
}

static void draw_field(const sk_chooser *c, sk_image *dst, int k, const sk_rect *clip)
{
    const field *f = k == K_FOLDER ? &c->folder : &c->line;
    box b = c->lay.ctl[k];
    double s = c->style.scale;
    sk_fontset *mono = c->style.mono;
    framed(c, dst, b, COL_BORDER, COL_FIELD, clip);
    box content = {b.x + BORDER + FIELD_PAD, b.y + BORDER + CTL_PAD_Y, b.w - 2 * (BORDER + FIELD_PAD),
                   line_h(CTL_FONT)};
    /* The text clips at the padding box, where the caret may sit. */
    sk_rect in = sk_rect_intersect(px_of(c, (box){b.x + BORDER, b.y + BORDER, b.w - 2 * BORDER, b.h - 2 * BORDER}),
                                   *clip);
    bool focused = c->focus == k, comp = composing(c, k);
    sk_buf t = {0};
    size_t at;
    shown_text(c, k, &t, &at);
    double tx = content.x - f->scroll, base = ctl_base(c, b);
    if (focused && sk_field_has_sel(f) && !comp) {
        double a = measure(mono, 0, CTL_FONT, t.ptr, sk_field_sel_lo(f)), z = measure(mono, 0, CTL_FONT, t.ptr, sk_field_sel_hi(f));
        sk_fill_rect(dst, px_of(c, (box){tx + a, content.y, z - a, content.h}), COL_SELECTION, SK_BLEND_SRC_OVER, &in);
    }
    text_at(c, dst, mono, 0, CTL_FONT, tx, base, t.ptr, t.len, COL_FIELD_TEXT, &in);
    if (focused) {
        sk_font_vmetrics m;
        sk_font_vmetrics_at(sk_fontset_primary(mono, 0), (float)CTL_FONT, &m);
        double x = tx + measure(mono, 0, CTL_FONT, t.ptr, at);
        /* A composition is underlined, the caret after it. */
        if (comp) {
            double p = measure(mono, 0, CTL_FONT, t.ptr, sk_field_sel_lo(f));
            sk_fill_rect(dst, px_of(c, (box){tx + p, base + 1, x - (tx + p), 1}), COL_FIELD_TEXT, SK_BLEND_SRC_OVER,
                         &in);
        }
        if (!sk_field_has_sel(f) || comp) {
            sk_rect caret = px_of(c, (box){x, base - m.ascent, 0, m.ascent + m.descent});
            caret.w = line_px(c);
            caret.x = (int)floor(x * s);
            sk_fill_rect(dst, caret, COL_FIELD_TEXT, SK_BLEND_SRC_OVER, &in);
        }
        ring_around(c, dst, b, clip);
    }
    sk_buf_free(&t);
}

/* Wrapped lines from the top of b, lh apart. */
static void draw_lines(const sk_chooser *c, sk_image *dst, sk_fontset *set, double size, double lh, double x,
                       double top, const char *s, const spans *l, sk_color col, const sk_rect *clip)
{
    double base = base_in(set, 0, size, lh);
    for (size_t i = 0; i < l->len; i++)
        text_at(c, dst, set, 0, size, x, top + (double)i * lh + base, s + l->ptr[i].at, l->ptr[i].n, col, clip);
}

static void draw_list(const sk_chooser *c, sk_image *dst, const sk_rect *clip)
{
    const layout *L = &c->lay;
    const sk_newtab_profiles *p = &c->st.agents[c->agent].profiles;
    framed(c, dst, L->list, COL_BORDER, COL_FIELD, clip);
    double base = base_in(c->style.mono, 0, CTL_FONT, LIST_ROW);
    for (size_t r = 0; r < L->list_rows && c->top + r < p->len; r++) {
        size_t i = c->top + r;
        box row = {L->list.x + BORDER, L->list.y + BORDER + (double)r * LIST_ROW, L->list.w - 2 * BORDER, LIST_ROW};
        sk_rect in = sk_rect_intersect(px_of(c, row), *clip);
        if (i == c->hot)
            sk_fill_rect(dst, in, COL_HOVER, SK_BLEND_SRC_OVER, NULL);
        sk_buf t = {0};
        option_text(&p->ptr[i], &t);
        text_at(c, dst, c->style.mono, 0, CTL_FONT, row.x + FIELD_PAD, row.y + base, t.ptr, t.len,
                i == c->hot ? COL_LIST_TEXT : COL_FIELD_TEXT, &in);
        sk_buf_free(&t);
    }
}

void sk_chooser_draw(sk_chooser *c, sk_image *dst, sk_rect area, const sk_chooser_style *style)
{
    if (!c->open)
        return;
    c->style = *style;
    c->area = area;
    c->laid = true;
    lay_out(c);
    const layout *L = &c->lay;
    sk_rect clip = sk_rect_intersect(area, sk_image_bounds(dst));
    sk_fontset *sans = style->sans, *mono = style->mono;
    sk_fill_rect(dst, clip, COL_OVERLAY, SK_BLEND_SRC_OVER, NULL);
    shadow(c, dst, L->dialog, &clip);
    framed(c, dst, L->dialog, COL_BORDER, COL_DIALOG, &clip);
    double x0 = L->row[0].x, lh = line_h(FONT);
    text_at(c, dst, sans, SK_STYLE_BOLD, FONT, x0, L->title_y + base_in(sans, SK_STYLE_BOLD, FONT, lh), "New tab", 7,
            COL_TITLE, &clip);
    for (size_t i = 0; i < SK_LEN(ROWS); i++) {
        box r = L->row[i];
        double top = r.y + (r.h - lh) / 2;
        text_at(c, dst, sans, 0, FONT, x0, top + base_in(sans, 0, FONT, lh), ROWS[i], strlen(ROWS[i]), COL_NAME, &clip);
    }
    for (int a = 0; a < SK_PROFILE_AGENTS; a++)
        draw_radio(c, dst, a, &clip);
    draw_select(c, dst, &clip);
    double hint_lh = line_h(HINT_FONT);
    draw_lines(c, dst, mono, HINT_FONT, hint_lh, L->env_box.x, L->env_box.y, L->env.ptr, &L->env_l, COL_NAME, &clip);
    draw_field(c, dst, K_FOLDER, &clip);
    draw_field(c, dst, K_LINE, &clip);
    if (L->dropped_box.h > 0)
        draw_lines(c, dst, mono, HINT_FONT, hint_lh, L->dropped_box.x, L->dropped_box.y, L->dropped.ptr,
                   &L->dropped_l, COL_DROPPED, &clip);
    draw_lines(c, dst, sans, HINT_FONT, hint_lh, L->hint_box.x, L->hint_box.y, HINT, &L->hint_l, COL_NAME, &clip);
    if (c->error) {
        sk_fill_rect(dst, px_of(c, L->error_box), COL_ERROR_BG, SK_BLEND_SRC_OVER, &clip);
        draw_lines(c, dst, mono, HINT_FONT, HINT_FONT * ERROR_LH, L->error_box.x + ERROR_PAD,
                   L->error_box.y + ERROR_PAD, c->error, &L->error_l, COL_ERROR, &clip);
    }
    for (int k = K_OTHER; k < K_COUNT; k++)
        if (is_button(k))
            draw_button(c, dst, k, &clip);
    if (c->list)
        draw_list(c, dst, &clip);
}

/* ---- Late answers: the picker and the clipboard ---- */

static sk_chooser *waiting(uintptr_t token, bool paste)
{
    for (size_t i = 0; token && i < alive.len; i++) {
        sk_chooser *c = alive.ptr[i];
        if ((paste ? c->paste_token : c->pick_token) == token)
            return c;
    }
    return NULL;
}

static void changed(sk_chooser *c)
{
    relayout(c);
    if (c->cfg.changed)
        c->cfg.changed(c->cfg.ud);
}

/* otherProfile and chooseFolder, once the picker answered. */
static void picked(void *ud, const char *dir, const char *err)
{
    sk_chooser *c = waiting((uintptr_t)ud, false);
    if (!c)
        return;
    c->pick_token = 0;
    if (err) {
        say(c, err);
    } else if (!dir) {
        return;
    } else if (c->picking == PICK_FOLDER) {
        sk_buf t = {0};
        sk_identity_tildify(&t, dir, c->st.home);
        sk_field_set(&c->folder, sk_buf_cstr(&t));
        sk_buf_free(&t);
    } else {
        sk_newtab_profiles *p = profiles_of(c);
        bool listed = false;
        for (size_t i = 0; i < p->len && !listed; i++)
            listed = !strcmp(p->ptr[i].dir, dir);
        if (!listed) {
            sk_buf t = {0};
            sk_identity_tildify(&t, dir, c->st.home);
            sk_newtab_profile n = {sk_xstrdup(dir), sk_xstrdup(sk_buf_cstr(&t)), false, false};
            sk_vec_push(p, n);
            sk_buf_free(&t);
        }
        fill_profiles(c, dir);
    }
    changed(c);
}

static void browse(sk_chooser *c, pick_what what)
{
    c->picking = what;
    c->pick_token = ++tokens;
    char *start = what == PICK_FOLDER
                      ? sk_newtab_browse_start(c->folder.text.ptr, c->st.home, c->st.base)
                      : sk_xstrdup(c->st.home ? c->st.home : "/");
    sk_picker_opts o = {what == PICK_FOLDER ? "Folder" : "Other profile", start, what == PICK_PROFILE};
    void (*pick)(sk_window *, sk_loop *, const sk_picker_opts *, sk_picker_done, void *) =
        c->cfg.pick_folder ? c->cfg.pick_folder : sk_picker_folder;
    pick(c->cfg.win, c->cfg.loop, &o, picked, (void *)c->pick_token);
    free(start);
}

static field *focused_field(sk_chooser *c)
{
    return field_of(c, c->focus);
}

static void pasted(void *ud, const char *utf8)
{
    sk_chooser *c = waiting((uintptr_t)ud, true);
    if (!c)
        return;
    c->paste_token = 0;
    field *f = focused_field(c);
    if (!c->open || !f || !utf8 || !*utf8)
        return;
    sk_field_insert(f, utf8);
    changed(c);
}

static void paste(sk_chooser *c, bool primary)
{
    c->paste_token = ++tokens;
    void (*get)(bool, void (*)(void *, const char *), void *) =
        c->cfg.clipboard_get ? c->cfg.clipboard_get : sk_clipboard_get;
    get(primary, pasted, (void *)c->paste_token);
}

/* X11's PRIMARY holds what was last selected, as Chromium sets it. */
static void selected(sk_chooser *c, const field *f)
{
    if (!sk_field_has_sel(f))
        return;
    char *s = sk_field_selection(f);
    (c->cfg.primary_set ? c->cfg.primary_set : sk_primary_set)(s);
    free(s);
}

/* ---- Life ---- */

sk_chooser *sk_chooser_new(const sk_chooser_config *cfg)
{
    sk_chooser *c = sk_xcalloc(1, sizeof *c);
    c->cfg = *cfg;
    sk_field_init(&c->folder);
    sk_field_init(&c->line);
    c->focus = c->hover = c->pressed = -1;
    c->cursor = SK_CURSOR_ARROW;
    sk_vec_push(&alive, c);
    return c;
}

void sk_chooser_close(sk_chooser *c)
{
    if (!c->open)
        return;
    c->open = false;
    sk_newtab_state_free(&c->st);
    sk_field_set(&c->folder, "");
    sk_field_set(&c->line, "");
    say(c, NULL);
    free(c->preedit);
    c->preedit = NULL;
    c->focus = c->hover = c->pressed = -1;
    c->list = c->selecting = c->ring = false;
    c->pick_token = c->paste_token = 0;
    c->cursor = SK_CURSOR_ARROW;
}

void sk_chooser_free(sk_chooser *c)
{
    if (!c)
        return;
    sk_chooser_close(c);
    for (size_t i = 0; i < alive.len; i++) {
        if (alive.ptr[i] == c) {
            alive.ptr[i] = alive.ptr[--alive.len];
            break;
        }
    }
    if (!alive.len)
        sk_vec_free(&alive);
    sk_buf_free(&c->folder.text);
    sk_buf_free(&c->line.text);
    layout_free(&c->lay);
    free(c);
}

void sk_chooser_open(sk_chooser *c, sk_newtab_state *st)
{
    if (c->open) {
        sk_newtab_state_free(st);
        return;
    }
    c->st = *st;
    memset(st, 0, sizeof *st);
    c->open = true;
    pick_agent(c, c->st.agent);
    sk_field_set(&c->folder, c->st.folder);
    c->focus = K_LINE;
    relayout(c);
}

bool sk_chooser_is_open(const sk_chooser *c)
{
    return c->open;
}

void sk_chooser_pick(const sk_chooser *c, sk_newtab_pick *out)
{
    const sk_newtab_profile *p = chosen(c);
    out->agent = sk_xstrdup(sk_profile_agents[c->agent].kind);
    out->profile = p ? sk_xstrdup(p->dir) : NULL;
    out->folder = sk_xstrdup(sk_buf_cstr(&c->folder.text));
    out->line = sk_xstrdup(sk_buf_cstr(&c->line.text));
}

void sk_chooser_fail(sk_chooser *c, const char *err)
{
    if (!c->open)
        return;
    say(c, err);
    relayout(c);
}

void sk_chooser_forgot(sk_chooser *c, sk_newtab_state *st)
{
    if (!c->open) {
        sk_newtab_state_free(st);
        return;
    }
    sk_newtab_state_free(&c->st);
    c->st = *st;
    memset(st, 0, sizeof *st);
    fill_profiles(c, c->st.agents[c->agent].profile);
    /* A focused button that becomes disabled loses the focus. */
    if (c->focus == K_FORGET && !forget_enabled(c))
        c->focus = K_PROFILE;
    relayout(c);
}

/* ---- Input ---- */

static sk_chooser_result result(sk_chooser_act act, bool redraw)
{
    sk_chooser_result r = {act, redraw};
    return r;
}

static sk_chooser_result activate(sk_chooser *c, int k)
{
    switch (k) {
    case K_OTHER:
        browse(c, PICK_PROFILE);
        return result(SK_CHOOSER_NONE, true);
    case K_FORGET:
        return result(forget_enabled(c) ? SK_CHOOSER_FORGET : SK_CHOOSER_NONE, true);
    case K_CHOOSE:
        browse(c, PICK_FOLDER);
        return result(SK_CHOOSER_NONE, true);
    case K_CANCEL:
        sk_chooser_close(c);
        return result(SK_CHOOSER_CLOSED, true);
    case K_OPEN:
        return result(SK_CHOOSER_OPEN, true);
    default:
        return result(SK_CHOOSER_NONE, false);
    }
}

static void choose_profile(sk_chooser *c, size_t i)
{
    if (i < profiles_of(c)->len)
        c->prof = i;
}

static void open_list(sk_chooser *c)
{
    c->list = true;
    c->hot = c->prof;
    c->top = 0;
}

/* The radios, the select and the fields in the order Tab takes them, a
 * disabled Forget left out; the radio group is one stop, its checked
 * radio. */
static void step_focus(sk_chooser *c, int d)
{
    int stops[K_COUNT];
    int n = 0;
    stops[n++] = K_AGENT + (int)c->agent;
    for (int k = K_PROFILE; k < K_COUNT; k++)
        if (k != K_FORGET || forget_enabled(c))
            stops[n++] = k;
    int at = -1;
    for (int i = 0; i < n; i++)
        if (stops[i] == c->focus || (i == 0 && c->focus >= K_AGENT && c->focus < K_PROFILE))
            at = i;
    int next = at < 0 ? (d > 0 ? 0 : n - 1) : (at + n + d) % n;
    c->focus = stops[next];
    c->ring = true;
    c->selecting = false;
    /* Tab into a text field selects all of it. */
    field *f = focused_field(c);
    if (f) {
        f->anchor = 0;
        f->caret = f->text.len;
    }
}

/* A key in a text field, as field.c edits one, with the clipboard's
 * part done here. False when it is not a field's key. */
static bool field_key(sk_chooser *c, field *f, const sk_key_event *k)
{
    sk_field_key_result r = sk_field_key(f, k);
    if (r.copy)
        (c->cfg.clipboard_set ? c->cfg.clipboard_set : sk_clipboard_set)(r.copy);
    free(r.copy);
    if (r.paste)
        paste(c, false);
    if (r.selected)
        selected(c, f);
    return r.took;
}

static bool radio_key(sk_chooser *c, const sk_key_event *k, uint32_t m)
{
    if (m)
        return false;
    bool back = k->key == SK_KEY_LEFT || k->key == SK_KEY_UP, on = k->key == SK_KEY_RIGHT || k->key == SK_KEY_DOWN;
    int d = back ? -1 : on ? 1 : 0;
    if (!d)
        return k->key == SK_KEY_NONE && !strcmp(k->text, " ");
    int a = ((int)c->agent + d + SK_PROFILE_AGENTS) % SK_PROFILE_AGENTS;
    pick_agent(c, (sk_profile_agent)a);
    c->focus = K_AGENT + a;
    c->ring = true;
    return true;
}

static bool select_key(sk_chooser *c, const sk_key_event *k, uint32_t m)
{
    size_t n = profiles_of(c)->len;
    bool alt = m == SK_MOD_ALT;
    if (m && !alt)
        return false;
    switch (k->key) {
    case SK_KEY_UP:
    case SK_KEY_DOWN:
        if (alt)
            open_list(c);
        else if (k->key == SK_KEY_UP && c->prof > 0)
            choose_profile(c, c->prof - 1);
        else if (k->key == SK_KEY_DOWN)
            choose_profile(c, c->prof + 1);
        return true;
    case SK_KEY_HOME:
    case SK_KEY_PAGE_UP:
        choose_profile(c, 0);
        return !alt;
    case SK_KEY_END:
    case SK_KEY_PAGE_DOWN:
        choose_profile(c, n ? n - 1 : 0);
        return !alt;
    case SK_KEY_NONE:
        if (!strcmp(k->text, " ")) {
            open_list(c);
            return true;
        }
        return false;
    default:
        /* F4, as Windows and Linux open a select. */
        if ((int)k->key == SK_KEY_F1 + 3) {
            open_list(c);
            return true;
        }
        return false;
    }
}

/* A key while the list is open: it moves the highlight, picks, or
 * closes. Tab picks and goes on. */
static bool list_key(sk_chooser *c, const sk_key_event *k, uint32_t m, bool *tab)
{
    size_t n = profiles_of(c)->len, page = SK_MAX((size_t)1, c->lay.list_rows);
    *tab = false;
    switch (k->key) {
    case SK_KEY_UP:
    case SK_KEY_DOWN:
        if (m == SK_MOD_ALT) {
            choose_profile(c, c->hot);
            c->list = false;
        } else if (k->key == SK_KEY_UP) {
            c->hot = c->hot ? c->hot - 1 : 0;
        } else if (c->hot + 1 < n) {
            c->hot++;
        }
        return true;
    case SK_KEY_HOME:
        c->hot = 0;
        return true;
    case SK_KEY_END:
        c->hot = n ? n - 1 : 0;
        return true;
    case SK_KEY_PAGE_UP:
        c->hot = c->hot > page ? c->hot - page : 0;
        return true;
    case SK_KEY_PAGE_DOWN:
        c->hot = SK_MIN(c->hot + page, n ? n - 1 : 0);
        return true;
    case SK_KEY_ENTER:
    case SK_KEY_KP_ENTER:
        choose_profile(c, c->hot);
        c->list = false;
        return true;
    case SK_KEY_ESCAPE:
        c->list = false;
        return true;
    case SK_KEY_TAB:
        choose_profile(c, c->hot);
        c->list = false;
        *tab = true;
        return true;
    case SK_KEY_NONE:
        if (!strcmp(k->text, " ")) {
            choose_profile(c, c->hot);
            c->list = false;
        }
        return true;
    default:
        return true;
    }
}

sk_chooser_result sk_chooser_key(sk_chooser *c, const sk_key_event *k)
{
    sk_chooser_result r = result(SK_CHOOSER_NONE, false);
    if (!c->open)
        return r;
    uint32_t m = k->mods & (SK_MOD_SHIFT | SK_MOD_CTRL | SK_MOD_ALT | SK_MOD_SUPER);
    bool enter = k->key == SK_KEY_ENTER || k->key == SK_KEY_KP_ENTER;
    bool space = k->key == SK_KEY_NONE && !strcmp(k->text, " ");
    if (c->list) {
        bool tab;
        r.redraw = list_key(c, k, m, &tab);
        if (!tab) {
            relayout(c);
            return r;
        }
    }
    if (k->key == SK_KEY_ESCAPE) {
        sk_chooser_close(c);
        return result(SK_CHOOSER_CLOSED, true);
    }
    if (k->key == SK_KEY_TAB && !(m & ~(uint32_t)SK_MOD_SHIFT)) {
        step_focus(c, m & SK_MOD_SHIFT ? -1 : 1);
        relayout(c);
        return result(SK_CHOOSER_NONE, true);
    }
    field *f = focused_field(c);
    if (f && !enter) {
        r.redraw = field_key(c, f, k);
    } else if (c->focus >= K_AGENT && c->focus < K_PROFILE && !enter) {
        r.redraw = radio_key(c, k, m);
    } else if (c->focus == K_PROFILE && !enter) {
        r.redraw = select_key(c, k, m);
    } else if (is_button(c->focus) && (enter || space) && !(m & ~(uint32_t)SK_MOD_SHIFT)) {
        r = activate(c, c->focus);
    } else if (enter && !(m & (SK_MOD_CTRL | SK_MOD_ALT | SK_MOD_SUPER))) {
        /* Implicit submission, as Enter in a form's control submits it. */
        r = result(SK_CHOOSER_OPEN, true);
    }
    relayout(c);
    return r;
}

sk_chooser_result sk_chooser_text(sk_chooser *c, const char *utf8)
{
    field *f = c->open ? focused_field(c) : NULL;
    if (!f || !utf8 || !*utf8)
        return result(SK_CHOOSER_NONE, false);
    sk_field_insert(f, utf8);
    relayout(c);
    return result(SK_CHOOSER_NONE, true);
}

sk_chooser_result sk_chooser_preedit(sk_chooser *c, const char *utf8)
{
    if (!c->open)
        return result(SK_CHOOSER_NONE, false);
    bool had = c->preedit != NULL;
    free(c->preedit);
    c->preedit = utf8 && *utf8 && focused_field(c) ? sk_xstrdup(utf8) : NULL;
    relayout(c);
    return result(SK_CHOOSER_NONE, had || c->preedit);
}

static bool mouse_move(sk_chooser *c, double x, double y)
{
    bool redraw = false;
    if (c->selecting) {
        field *f = focused_field(c);
        size_t at = offset_at(c, c->focus, x);
        if (f && at != f->caret) {
            f->caret = at;
            redraw = true;
        }
    }
    int h = hit(c, x, y);
    if (h == K_LIST) {
        size_t row = row_at(c, y);
        if (row != SIZE_MAX && row != c->hot) {
            c->hot = row;
            redraw = true;
        }
    }
    if (h != c->hover) {
        c->hover = h;
        redraw = true;
    }
    c->cursor = c->selecting || is_field(h) ? SK_CURSOR_IBEAM : SK_CURSOR_ARROW;
    return redraw;
}

static sk_chooser_result mouse_down(sk_chooser *c, double x, double y, int button, int clicks, uint32_t mods)
{
    int h = hit(c, x, y);
    if (c->list) {
        /* A press anywhere closes the list; on a row it picks it. */
        if (h == K_LIST && button == 1) {
            size_t row = row_at(c, y);
            if (row != SIZE_MAX)
                choose_profile(c, row);
        }
        c->list = false;
        return result(SK_CHOOSER_NONE, true);
    }
    if (h < 0)
        return result(SK_CHOOSER_NONE, false);
    if (button == 2 && is_field(h)) {
        /* X11's middle click pastes PRIMARY where it lands. */
        c->focus = h;
        c->ring = false;
        field *f = focused_field(c);
        f->caret = f->anchor = offset_at(c, h, x);
        paste(c, true);
        return result(SK_CHOOSER_NONE, true);
    }
    if (button != 1)
        return result(SK_CHOOSER_NONE, false);
    c->ring = false;
    if (h < K_PROFILE) {
        if (h - K_AGENT != (int)c->agent)
            pick_agent(c, (sk_profile_agent)(h - K_AGENT));
        c->focus = h;
    } else if (h == K_PROFILE) {
        c->focus = h;
        open_list(c);
    } else if (is_field(h)) {
        c->focus = h;
        field *f = focused_field(c);
        size_t at = offset_at(c, h, x);
        if (clicks >= 3) {
            f->anchor = 0;
            f->caret = f->text.len;
            selected(c, f);
        } else if (clicks == 2) {
            sk_field_word_around(&f->text, at, &f->anchor, &f->caret);
            selected(c, f);
        } else {
            f->caret = at;
            if (!(mods & SK_MOD_SHIFT))
                f->anchor = at;
            c->selecting = true;
        }
        c->cursor = SK_CURSOR_IBEAM;
    } else if (is_button(h)) {
        if (h == K_FORGET && !forget_enabled(c))
            return result(SK_CHOOSER_NONE, false);
        c->focus = h;
        c->pressed = h;
    }
    return result(SK_CHOOSER_NONE, true);
}

static sk_chooser_result mouse_up(sk_chooser *c, double x, double y, int button)
{
    if (button != 1)
        return result(SK_CHOOSER_NONE, false);
    bool redraw = false;
    if (c->selecting) {
        c->selecting = false;
        field *f = focused_field(c);
        if (f)
            selected(c, f);
        redraw = true;
    }
    if (c->pressed >= 0) {
        int p = c->pressed;
        c->pressed = -1;
        if (hit(c, x, y) == p)
            return activate(c, p);
        redraw = true;
    }
    return result(SK_CHOOSER_NONE, redraw);
}

sk_chooser_result sk_chooser_mouse(sk_chooser *c, const sk_event *ev)
{
    sk_chooser_result r = result(SK_CHOOSER_NONE, false);
    if (!c->open || !c->laid)
        return r;
    switch (ev->type) {
    case SK_EV_MOUSE_MOVE:
        r.redraw = mouse_move(c, ev->mouse.x, ev->mouse.y);
        break;
    case SK_EV_MOUSE_LEAVE:
        r.redraw = c->hover != -1;
        c->hover = -1;
        break;
    case SK_EV_MOUSE_DOWN:
        r = mouse_down(c, ev->mouse.x, ev->mouse.y, ev->mouse.button, ev->mouse.clicks, ev->mouse.mods);
        break;
    case SK_EV_MOUSE_UP:
        r = mouse_up(c, ev->mouse.x, ev->mouse.y, ev->mouse.button);
        break;
    case SK_EV_SCROLL:
        if (c->list && hit(c, ev->scroll.x, ev->scroll.y) == K_LIST) {
            size_t n = profiles_of(c)->len, rows = c->lay.list_rows;
            double lines = ev->scroll.precise ? ev->scroll.dy / LIST_ROW : ev->scroll.dy;
            long d = (long)(lines > 0 ? ceil(lines) : floor(lines));
            long top = (long)c->top - d;
            long last = n > rows ? (long)(n - rows) : 0;
            c->top = (size_t)SK_MAX(0L, SK_MIN(top, last));
            c->hot = SK_MIN(SK_MAX(c->hot, c->top), c->top + rows - 1);
            r.redraw = true;
        }
        break;
    default:
        break;
    }
    if (c->open)
        relayout(c);
    return r;
}

sk_cursor sk_chooser_cursor(const sk_chooser *c)
{
    return c->open ? c->cursor : SK_CURSOR_ARROW;
}

bool sk_chooser_ime_rect(const sk_chooser *c, int *x, int *y, int *w, int *h)
{
    if (!c->open || !c->laid || !is_field(c->focus))
        return false;
    /* The caret, or where a composition starts. */
    const field *f = c->focus == K_FOLDER ? &c->folder : &c->line;
    box b = c->lay.ctl[c->focus];
    double at = measure(c->style.mono, 0, CTL_FONT, f->text.ptr, composing(c, c->focus) ? sk_field_sel_lo(f) : f->caret);
    *x = (int)lround(b.x + BORDER + FIELD_PAD - f->scroll + at);
    *y = (int)lround(b.y + BORDER + CTL_PAD_Y);
    *w = 1;
    *h = (int)lround(line_h(CTL_FONT));
    return true;
}

bool sk_chooser_box(const sk_chooser *c, const char *name, double out[4])
{
    if (!c->open || !c->laid)
        return false;
    const layout *L = &c->lay;
    box b = {0, 0, 0, 0};
    if (!strcmp(name, "dialog"))
        b = L->dialog;
    else if (!strcmp(name, "env"))
        b = L->env_box;
    else if (!strcmp(name, "dropped"))
        b = L->dropped_box;
    else if (!strcmp(name, "hint"))
        b = L->hint_box;
    else if (!strcmp(name, "error"))
        b = L->error_box;
    else if (!strcmp(name, "list") && c->list)
        b = L->list;
    else if (!strncmp(name, "agent", 5) && name[5] >= '0' && name[5] < '0' + SK_PROFILE_AGENTS && !name[6])
        b = L->label[name[5] - '0'];
    else if (!strncmp(name, "radio", 5) && name[5] >= '0' && name[5] < '0' + SK_PROFILE_AGENTS && !name[6])
        b = L->radio[name[5] - '0'];
    for (int k = K_PROFILE; k < K_COUNT && b.w <= 0 && b.h <= 0; k++)
        if (!strcmp(name, NAMES[k]))
            b = L->ctl[k];
    out[0] = b.x;
    out[1] = b.y;
    out[2] = b.w;
    out[3] = b.h;
    return b.w > 0 && b.h > 0;
}

void sk_chooser_json(const sk_chooser *c, sk_jw *w)
{
    sk_jw_object(w);
    sk_jw_key(w, SK_STR("open"));
    sk_jw_bool(w, c->open);
    if (c->open) {
        const sk_newtab_profile *p = chosen(c);
        const sk_newtab_profiles *ps = &c->st.agents[c->agent].profiles;
        sk_jw_key(w, SK_STR("agent"));
        sk_jw_string(w, sk_str_c(sk_profile_agents[c->agent].kind));
        sk_jw_key(w, SK_STR("profile"));
        if (p)
            sk_jw_string(w, sk_str_c(p->dir));
        else
            sk_jw_null(w);
        sk_jw_key(w, SK_STR("profiles"));
        sk_jw_array(w);
        for (size_t i = 0; i < ps->len; i++) {
            sk_buf t = {0};
            option_text(&ps->ptr[i], &t);
            sk_jw_string(w, sk_buf_str(&t));
            sk_buf_free(&t);
        }
        sk_jw_array_end(w);
        sk_buf env = {0};
        env_text(c, &env);
        sk_jw_key(w, SK_STR("env"));
        sk_jw_string(w, sk_buf_str(&env));
        sk_buf_free(&env);
        sk_jw_key(w, SK_STR("forget"));
        sk_jw_bool(w, forget_enabled(c));
        sk_jw_key(w, SK_STR("folder"));
        sk_jw_string(w, sk_buf_str(&c->folder.text));
        sk_jw_key(w, SK_STR("line"));
        sk_jw_string(w, sk_buf_str(&c->line.text));
        sk_jw_key(w, SK_STR("dropped"));
        sk_jw_string(w, sk_str_c(c->st.agents[c->agent].dropped ? c->st.agents[c->agent].dropped : ""));
        sk_jw_key(w, SK_STR("error"));
        if (c->error)
            sk_jw_string(w, sk_str_c(c->error));
        else
            sk_jw_null(w);
        sk_jw_key(w, SK_STR("focus"));
        if (c->focus >= K_AGENT && c->focus < K_PROFILE)
            sk_jw_string(w, SK_STR("agent"));
        else if (c->focus >= K_PROFILE && c->focus < K_COUNT)
            sk_jw_string(w, sk_str_c(NAMES[c->focus]));
        else
            sk_jw_null(w);
        sk_jw_key(w, SK_STR("list"));
        sk_jw_bool(w, c->list);
    }
    sk_jw_object_end(w);
}
