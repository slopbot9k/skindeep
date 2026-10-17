#include "rename.h"

#include "../base/utf8.h"
#include "../gfx/draw.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* app.css's #tab-menu, in points. */
#define POP_MIN_W 110.0
#define POP_PAD_Y 4.0
#define POP_PAD_X 14.0
#define POP_LINE 24.0
#define POP_FONT 13.0
#define POP_RADIUS 4.0
#define POP_SHADOW_Y 6.0
#define POP_SHADOW_BLUR 18.0
#define POP_EDGE 120.0 /* tabs.js keeps the drop-down this far from the right edge */
/* #tabs .rename. */
#define FIELD_FONT 12.0
#define FIELD_LINE 16.0
#define FIELD_PAD 3.0
#define FIELD_RADIUS 2.0

#define C_POP_BG SK_RGB(0x1b, 0x1f, 0x26)
#define C_POP_TEXT SK_RGB(0xe6, 0xe9, 0xee)
#define C_POP_BORDER SK_RGB(0x3a, 0x3f, 0x47)
#define C_LIT SK_RGB(0x2f, 0x5d, 0x8a)
#define C_POP_SHADOW SK_RGBA(0, 0, 0, 153)
#define C_FIELD_BG SK_RGB(0, 0, 0)
#define C_FIELD_TEXT SK_RGB(0xff, 0xff, 0xff)
#define C_FIELD_BORDER SK_RGB(0x6f, 0xa8, 0xdc)

static const char ITEM[] = "Rename";

struct sk_rename {
    /* The drop-down: its tab, its border box, the item lit. */
    bool popup;
    int popup_tab;
    double px, py, pw, ph;
    bool hot;
    /* The field. */
    bool editing;
    int tab;
    sk_field f;
    double fx, fy, fw, fh;
    bool dragging;
};

sk_rename *sk_rename_new(void)
{
    sk_rename *r = sk_xcalloc(1, sizeof *r);
    sk_field_init(&r->f);
    return r;
}

void sk_rename_free(sk_rename *r)
{
    if (!r)
        return;
    sk_field_free(&r->f);
    free(r);
}

static sk_rect prect(double k, double x, double y, double w, double h)
{
    int x0 = (int)lround(x * k), y0 = (int)lround(y * k);
    int x1 = (int)lround((x + w) * k), y1 = (int)lround((y + h) * k);
    return sk_rect_make(x0, y0, x1 - x0, y1 - y0);
}

static void round_rect(sk_image *dst, double k, double x, double y, double w, double h, double radius, sk_color c)
{
    sk_path *p = sk_path_new();
    sk_path_round_rect(p, (float)(x * k), (float)(y * k), (float)(w * k), (float)(h * k), (float)(radius * k));
    sk_fill_path(dst, p, NULL, SK_FILL_NONZERO, c, NULL);
    sk_path_free(p);
}

/* ---- The drop-down ---- */

void sk_rename_popup(sk_rename *r, int id, double x, double y, double win_w)
{
    r->popup = true;
    r->popup_tab = id;
    r->px = SK_MIN(x, win_w - POP_EDGE);
    r->py = y;
    /* Its width is measured at the first draw; min-width until then. */
    r->pw = POP_MIN_W + 2;
    r->ph = 2 + 2 * POP_PAD_Y + POP_LINE;
    r->hot = false;
}

bool sk_rename_popup_open(const sk_rename *r)
{
    return r->popup;
}

void sk_rename_popup_close(sk_rename *r)
{
    r->popup = false;
    r->hot = false;
}

static bool on_item(const sk_rename *r, double x, double y)
{
    double iy = r->py + 1 + POP_PAD_Y;
    return x >= r->px + 1 && x < r->px + r->pw - 1 && y >= iy && y < iy + POP_LINE;
}

bool sk_rename_popup_press(sk_rename *r, double x, double y, int button, bool *rename, int *id)
{
    *rename = false;
    *id = r->popup_tab;
    bool inside = x >= r->px && x < r->px + r->pw && y >= r->py && y < r->py + r->ph;
    if (!inside) {
        sk_rename_popup_close(r);
        return false;
    }
    if (button == 1 && on_item(r, x, y)) {
        *rename = true;
        sk_rename_popup_close(r);
    }
    return true;
}

bool sk_rename_popup_hover(sk_rename *r, double x, double y)
{
    bool hot = r->popup && on_item(r, x, y);
    bool changed = hot != r->hot;
    r->hot = hot;
    return changed;
}

sk_rect sk_rename_popup_bounds(const sk_rename *r, double scale)
{
    if (!r->popup)
        return sk_rect_make(0, 0, 0, 0);
    double ext = 1.5 * POP_SHADOW_BLUR;
    sk_rect a = prect(scale, r->px - ext, r->py + POP_SHADOW_Y - ext, r->pw + 2 * ext, r->ph + 2 * ext);
    return sk_rect_union(a, prect(scale, r->px, r->py, r->pw, r->ph));
}

void sk_rename_popup_draw(sk_rename *r, sk_image *dst, const sk_menu_style *st)
{
    if (!r->popup)
        return;
    double k = st->scale;
    float fpx = (float)(POP_FONT * k);
    double tw = sk_measure_text(st->sans, 0, fpx, ITEM, strlen(ITEM)) / k;
    r->pw = SK_MAX(POP_MIN_W, 2 * POP_PAD_X + tw) + 2;
    sk_shadow_rect(dst, r->px * k, (r->py + POP_SHADOW_Y) * k, r->pw * k, r->ph * k, POP_SHADOW_BLUR * k,
                   C_POP_SHADOW, NULL);
    round_rect(dst, k, r->px, r->py, r->pw, r->ph, POP_RADIUS, C_POP_BORDER);
    round_rect(dst, k, r->px + 1, r->py + 1, r->pw - 2, r->ph - 2, POP_RADIUS - 1, C_POP_BG);
    double iy = r->py + 1 + POP_PAD_Y;
    if (r->hot)
        sk_fill_rect(dst, prect(k, r->px + 1, iy, r->pw - 2, POP_LINE), C_LIT, SK_BLEND_SRC_OVER, NULL);
    float base = sk_baseline_in(st->sans, 0, fpx, (float)(iy * k), (float)(POP_LINE * k));
    sk_draw_text(dst, st->cache, st->sans, 0, fpx, (float)((r->px + 1 + POP_PAD_X) * k), base, ITEM, strlen(ITEM),
                 C_POP_TEXT, NULL);
}

/* ---- The field ---- */

void sk_rename_edit(sk_rename *r, int id, const char *name)
{
    r->editing = true;
    r->tab = id;
    r->dragging = false;
    sk_field_set(&r->f, name);
    sk_field_select_all(&r->f);
}

bool sk_rename_editing(const sk_rename *r)
{
    return r->editing;
}

int sk_rename_tab(const sk_rename *r)
{
    return r->editing ? r->tab : 0;
}

const char *sk_rename_text(const sk_rename *r)
{
    return sk_buf_cstr(&r->f.text);
}

sk_field *sk_rename_field(sk_rename *r)
{
    return &r->f;
}

void sk_rename_end(sk_rename *r)
{
    r->editing = false;
    r->dragging = false;
    sk_field_set(&r->f, "");
}

/* maxLength 120: what passes it is cut at a character. */
static void cap(sk_field *f)
{
    size_t chars = 0, at = 0;
    while (at < f->text.len && chars < 120) {
        at = sk_field_next(&f->text, at);
        chars++;
    }
    if (at < f->text.len)
        sk_field_replace(f, at, f->text.len, NULL, 0);
}

sk_rename_how sk_rename_key(sk_rename *r, const sk_key_event *k, sk_field_key_result *clip)
{
    memset(clip, 0, sizeof *clip);
    if (!r->editing)
        return SK_RENAME_GOES_ON;
    if (k->key == SK_KEY_ENTER || k->key == SK_KEY_KP_ENTER)
        return SK_RENAME_COMMIT;
    if (k->key == SK_KEY_ESCAPE)
        return SK_RENAME_CANCEL;
    *clip = sk_field_key(&r->f, k);
    cap(&r->f);
    return SK_RENAME_GOES_ON;
}

void sk_rename_place(sk_rename *r, double x, double y, double w, double h)
{
    r->fx = x;
    r->fy = y;
    r->fw = w;
    r->fh = h;
}

static double measure(const sk_menu_style *st, const char *s, size_t n)
{
    return sk_measure_text(st->mono, 0, (float)(FIELD_FONT * st->scale), s, n) / st->scale;
}

/* The content box's left edge and width, in points. */
static double content_x(const sk_rename *r)
{
    return r->fx + 1 + FIELD_PAD;
}

static double content_w(const sk_rename *r)
{
    return SK_MAX(0.0, r->fw - 2 - 2 * FIELD_PAD);
}

/* The character boundary nearest x. */
static size_t offset_at(const sk_rename *r, const sk_menu_style *st, double x)
{
    double at = x - content_x(r) + r->f.scroll;
    const sk_buf *t = &r->f.text;
    size_t best = 0;
    double best_d = fabs(at);
    for (size_t i = 0; i < t->len;) {
        i = sk_field_next(t, i);
        double d = fabs(measure(st, t->ptr, i) - at);
        if (d < best_d) {
            best = i;
            best_d = d;
        }
    }
    return best;
}

sk_rename_how sk_rename_mouse(sk_rename *r, const sk_event *ev, const sk_menu_style *st, bool *selected)
{
    *selected = false;
    if (!r->editing)
        return SK_RENAME_GOES_ON;
    double x = ev->mouse.x, y = ev->mouse.y;
    bool inside = x >= r->fx && x < r->fx + r->fw && y >= r->fy && y < r->fy + r->fh;
    sk_field *f = &r->f;
    switch (ev->type) {
    case SK_EV_MOUSE_DOWN:
        /* A press elsewhere takes the focus away: the blur commits. */
        if (!inside)
            return SK_RENAME_COMMIT;
        if (ev->mouse.button != 1)
            return SK_RENAME_GOES_ON;
        if (ev->mouse.clicks >= 3) {
            sk_field_select_all(f);
        } else if (ev->mouse.clicks == 2) {
            sk_field_word_around(&f->text, offset_at(r, st, x), &f->anchor, &f->caret);
        } else {
            f->caret = offset_at(r, st, x);
            if (!(ev->mouse.mods & SK_MOD_SHIFT))
                f->anchor = f->caret;
            r->dragging = true;
        }
        *selected = sk_field_has_sel(f);
        return SK_RENAME_GOES_ON;
    case SK_EV_MOUSE_MOVE:
        if (r->dragging)
            f->caret = offset_at(r, st, x);
        return SK_RENAME_GOES_ON;
    case SK_EV_MOUSE_UP:
        if (r->dragging) {
            r->dragging = false;
            *selected = sk_field_has_sel(f);
        }
        return SK_RENAME_GOES_ON;
    default:
        return SK_RENAME_GOES_ON;
    }
}

/* The scroll that keeps the caret in the content box. */
static void keep_caret(sk_rename *r, const sk_menu_style *st)
{
    sk_field *f = &r->f;
    double cw = content_w(r), total = measure(st, f->text.ptr, f->text.len);
    double at = measure(st, f->text.ptr, f->caret);
    if (total <= cw)
        f->scroll = 0;
    else if (at - f->scroll > cw)
        f->scroll = at - cw;
    else if (at < f->scroll)
        f->scroll = at;
    f->scroll = SK_MAX(0.0, SK_MIN(f->scroll, SK_MAX(0.0, total - cw)));
}

void sk_rename_draw(sk_rename *r, sk_image *dst, const sk_menu_style *st)
{
    if (!r->editing)
        return;
    double k = st->scale;
    keep_caret(r, st);
    round_rect(dst, k, r->fx, r->fy, r->fw, r->fh, FIELD_RADIUS, C_FIELD_BORDER);
    round_rect(dst, k, r->fx + 1, r->fy + 1, r->fw - 2, r->fh - 2, FIELD_RADIUS - 1, C_FIELD_BG);
    sk_rect clip = prect(k, content_x(r), r->fy + 1, content_w(r), r->fh - 2);
    double top = r->fy + 1 + (r->fh - 2 - FIELD_LINE) / 2, x0 = content_x(r) - r->f.scroll;
    const sk_field *f = &r->f;
    if (sk_field_has_sel(f)) {
        double a = measure(st, f->text.ptr, sk_field_sel_lo(f)), b = measure(st, f->text.ptr, sk_field_sel_hi(f));
        sk_fill_rect(dst, prect(k, x0 + a, top, b - a, FIELD_LINE), C_LIT, SK_BLEND_SRC_OVER, &clip);
    }
    float fpx = (float)(FIELD_FONT * k);
    float base = sk_baseline_in(st->mono, 0, fpx, (float)(top * k), (float)(FIELD_LINE * k));
    sk_draw_text(dst, st->cache, st->mono, 0, fpx, (float)(x0 * k), base, f->text.ptr, f->text.len, C_FIELD_TEXT,
                 &clip);
    if (!sk_field_has_sel(f)) {
        double cx = x0 + measure(st, f->text.ptr, f->caret);
        sk_rect caret = prect(k, cx, top + 1, 0, FIELD_LINE - 2);
        caret.w = SK_MAX(1, (int)lround(k));
        sk_fill_rect(dst, caret, C_FIELD_TEXT, SK_BLEND_SRC_OVER, &clip);
    }
}

void sk_rename_caret(const sk_rename *r, const sk_menu_style *st, int *x, int *y, int *w, int *h)
{
    double cx = content_x(r) - r->f.scroll + measure(st, r->f.text.ptr, r->f.caret);
    *x = (int)lround(cx);
    *y = (int)lround(r->fy + 1);
    *w = 1;
    *h = (int)lround(FIELD_LINE);
}
