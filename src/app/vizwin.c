#include "vizwin.h"

#include "../base/log.h"
#include "../viz/milk.h"
#include "../viz/viz.h"
#include "menu.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* index.html's styles, in points and CSS colours. */
#define FONT_PT 11.0
#define CAPTION_SIDE 8  /* .mode's left and right */
#define CAPTION_FOOT 6  /* .mode's bottom */
#define BUTTON_PAD 6    /* .pick's padding */
#define BUTTON_RADIUS 3
#define ITEM_PAD 8      /* #menu .item's padding */
#define LINE_PT 18      /* .pick's and .item's line-height */
#define CAPTION SK_RGBA(200, 255, 220, 140)      /* rgba(200, 255, 220, 0.55) */
#define PICK_TEXT SK_RGBA(200, 255, 220, 179)    /* 0.7 */
#define PICK_TEXT_ON SK_RGBA(225, 255, 235, 242) /* :hover, .open: 0.95 */
#define PICK_BG SK_RGBA(0, 0, 0, 140)            /* rgba(0, 0, 0, 0.55) */
#define PICK_BORDER SK_RGBA(200, 255, 220, 77)   /* 0.3 */
#define PICK_BORDER_ON SK_RGBA(200, 255, 220, 166) /* 0.65 */
#define ITEM_TEXT SK_RGBA(200, 255, 220, 204)    /* 0.8 */
#define ITEM_BG SK_RGBA(6, 12, 9, 235)           /* rgba(6, 12, 9, 0.92) */
#define ITEM_HOVER SK_RGBA(36, 64, 46, 245)      /* 0.96 */
#define ITEM_CURRENT SK_RGBA(28, 86, 52, 245)    /* 0.96 */
#define OUTLINE SK_RGBA(200, 255, 220, 115)      /* the active pane's, 0.45 */
/* The pointer's tone again every this many ms of a drag. */
#define TONE_MS 60.0
/* viz.js blends one MilkDrop mode into the next over this. */
#define MILK_BLEND 1.5

typedef struct {
    int tab, place;
    bool active, open;
    int shown; /* the mode shown, -1 for none yet */
    sk_synth synth;
    sk_vmode *vm;  /* a Canvas2D mode's renderer */
    sk_milk *milk; /* made the first time a MilkDrop mode shows */
} pane;

struct sk_vizwin {
    sk_vizwin_config c;
    char *title;
    sk_window *win;
    /* The feed's count of openings of the window shown, and of one the
     * user closed, whose frames do not open it again. */
    unsigned opened, refused;
    bool has_refused;
    SK_VEC(pane *) panes;
    int src, st;
    float samples[SK_SYNTH_N];
    size_t n_samples;
    /* The window's size of the last render, in points. */
    int w, h;
    double scale;
    uint64_t last_frame;
    /* The pointer: the tab whose pane a press holds it to, whether the
     * left button is down, the last tone, and where it hovers. */
    int held;
    bool button;
    double tone;
    bool inside;
    double hx, hy;
};

static const char *const SOURCE[] = {"", "sound output", "session and sound output"};

sk_vizwin *sk_vizwin_new(const sk_vizwin_config *c)
{
    sk_vizwin *v = sk_xcalloc(1, sizeof *v);
    v->c = *c;
    v->title = sk_xstrdup(c->title ? c->title : "visualization");
    v->c.title = v->title;
    v->tone = -1e18;
    v->w = SK_VIZWIN_W;
    v->h = SK_VIZWIN_H;
    v->scale = 1;
    return v;
}

static void pane_free(pane *p)
{
    sk_vmode_free(p->vm);
    sk_milk_free(p->milk);
    sk_synth_free(&p->synth);
    free(p);
}

static void drop_panes(sk_vizwin *v)
{
    for (size_t i = 0; i < v->panes.len; i++)
        pane_free(v->panes.ptr[i]);
    v->panes.len = 0;
    v->held = 0;
}

/* The window and its page go: the panes with them. */
static void close_window(sk_vizwin *v)
{
    if (v->win)
        sk_window_free(v->win);
    v->win = NULL;
    v->inside = false;
    v->button = false;
    drop_panes(v);
}

void sk_vizwin_free(sk_vizwin *v)
{
    if (!v)
        return;
    close_window(v);
    sk_vec_free(&v->panes);
    free(v->title);
    free(v);
}

static bool open_window(sk_vizwin *v)
{
    if (v->c.offscreen)
        return true;
    sk_window_opts o = {
        .title = v->title,
        .w = SK_VIZWIN_W,
        .h = SK_VIZWIN_H,
        .min_w = SK_VIZWIN_MIN_W,
        .min_h = SK_VIZWIN_MIN_H,
        .ud = v,
        .hidden = v->c.hidden,
        .no_focus = true,
    };
    char *err = NULL;
    v->win = sk_window_new(&o, &err);
    if (!v->win) {
        sk_log("visualization: cannot open its window: %s", err ? err : "unknown");
        free(err);
        return false;
    }
    sk_window_set_cursor(v->win, SK_CURSOR_ARROW);
    sk_window_size(v->win, &v->w, &v->h);
    v->scale = sk_window_scale(v->win);
    v->last_frame = 0;
    sk_window_request_frame(v->win);
    return true;
}

bool sk_vizwin_is_open(const sk_vizwin *v)
{
    return v->win != NULL;
}

sk_window *sk_vizwin_window(const sk_vizwin *v)
{
    return v->win;
}

void sk_vizwin_set_title(sk_vizwin *v, const char *title)
{
    free(v->title);
    v->title = sk_xstrdup(title ? title : "");
    v->c.title = v->title;
    if (v->win)
        sk_window_set_title(v->win, v->title);
}

/* The seeds of a pane's synthesiser and of each renderer it makes. */
static uint64_t seed_of(int tab, int mode)
{
    return 0x5eedc0deu ^ (uint64_t)(unsigned)tab * 0x9e3779b97f4a7c15u ^ (uint64_t)(unsigned)mode << 40;
}

static pane *pane_new(int tab)
{
    pane *p = sk_xcalloc(1, sizeof *p);
    p->tab = tab;
    p->shown = -1;
    sk_synth_init(&p->synth, seed_of(tab, 0x7f) ^ 0x5eed5157u);
    return p;
}

/* Mode m in pane p, as viz.js's show(): a MilkDrop mode blends from one
 * before it, and from a Canvas2D mode starts at once; a Canvas2D mode
 * is a new renderer on a black canvas. */
static void show(pane *p, int m)
{
    if (m < 0 || m >= SK_VIZ_MODES || m == p->shown)
        return;
    int prev = p->shown;
    p->shown = m;
    sk_vmode_free(p->vm);
    p->vm = NULL;
    if (sk_viz_modes[m].kind == SK_VIZ_MILK) {
        int preset = sk_milk_find(sk_viz_modes[m].id);
        if (!p->milk)
            p->milk = sk_milk_new(preset, seed_of(p->tab, m));
        else
            sk_milk_load(p->milk, preset, prev >= 0 && sk_viz_modes[prev].kind == SK_VIZ_MILK ? MILK_BLEND : 0);
    } else {
        p->vm = sk_vmode_new(m, seed_of(p->tab, m));
    }
}

static pane *pane_of(const sk_vizwin *v, int tab)
{
    for (size_t i = 0; i < v->panes.len; i++)
        if (v->panes.ptr[i]->tab == tab)
            return v->panes.ptr[i];
    return NULL;
}

void sk_vizwin_frame(sk_vizwin *v, const sk_vizfeed_msg *m)
{
    if (!m->open) {
        close_window(v);
        v->has_refused = false;
        return;
    }
    if (v->has_refused && v->refused == m->opened)
        return;
    if (!v->win && !v->c.offscreen) {
        drop_panes(v);
        if (!open_window(v)) {
            v->refused = m->opened;
            v->has_refused = true;
            if (v->c.feed)
                sk_vizfeed_post_closed(v->c.feed, m->opened);
            return;
        }
    }
    v->opened = m->opened;
    v->src = m->frame.src;
    v->st = m->frame.st;
    v->n_samples = m->frame.samples && m->frame.n_samples == SK_SYNTH_N ? SK_SYNTH_N : 0;
    if (v->n_samples)
        memcpy(v->samples, m->frame.samples, sizeof v->samples);
    /* The panes in the frame's order, the gone ones dropped. */
    for (size_t i = 0; i < v->panes.len;) {
        pane *p = v->panes.ptr[i];
        bool kept = false;
        for (size_t k = 0; k < m->frame.n_panes && !kept; k++)
            kept = m->frame.panes[k].tab == p->tab;
        if (kept) {
            i++;
            continue;
        }
        if (v->held == p->tab)
            v->held = 0;
        pane_free(p);
        memmove(v->panes.ptr + i, v->panes.ptr + i + 1, (v->panes.len - i - 1) * sizeof *v->panes.ptr);
        v->panes.len--;
    }
    pane **order = sk_xcalloc(m->frame.n_panes ? m->frame.n_panes : 1, sizeof *order);
    for (size_t k = 0; k < m->frame.n_panes; k++) {
        const sk_viz_pane_frame *e = &m->frame.panes[k];
        pane *p = pane_of(v, e->tab);
        order[k] = p ? p : pane_new(e->tab);
    }
    v->panes.len = 0;
    for (size_t k = 0; k < m->frame.n_panes; k++)
        sk_vec_push(&v->panes, order[k]);
    free(order);
    for (size_t k = 0; k < m->frame.n_panes; k++) {
        const sk_viz_pane_frame *e = &m->frame.panes[k];
        pane *p = v->panes.ptr[k];
        p->place = e->place;
        p->active = e->active;
        p->open = e->open;
        if (v->src != SK_VIZ_SRC_SOUND)
            sk_synth_input(&p->synth, e);
        show(p, e->mode);
    }
}

/* ---- Drawing ---- */

static sk_rect px_rect(sk_rect r, double s)
{
    int x0 = (int)lround(r.x * s), y0 = (int)lround(r.y * s);
    int x1 = (int)lround((r.x + r.w) * s), y1 = (int)lround((r.y + r.h) * s);
    return sk_rect_make(x0, y0, x1 - x0, y1 - y0);
}

/* The line box CSS gives the font at its size, line-height normal: the
 * ascent and the descent each rounded, in points. */
static void line_metrics(sk_vizwin *v, double *ascent, double *descent)
{
    sk_font_vmetrics vm;
    sk_font_vmetrics_at(sk_fontset_primary(v->c.mono, 0), (float)FONT_PT, &vm);
    *ascent = round(vm.ascent);
    *descent = round(vm.descent);
}

/* Text in a line box LINE_PT high from top (points), at x, cut with an
 * ellipsis to maxw, clipped to clip (pixels). */
static void line_text(sk_vizwin *v, sk_image *dst, double s, double x, double top, double maxw, const char *text,
                      sk_color c, const sk_rect *clip)
{
    if (maxw <= 0)
        return;
    float px = (float)(FONT_PT * s);
    float base = sk_baseline_in(v->c.mono, 0, px, (float)(top * s), (float)(LINE_PT * s));
    sk_draw_fit(dst, v->c.cache, v->c.mono, 0, px, (float)(x * s), base, (float)(maxw * s), text, c, false, clip);
}

/* A box with rounded corners, as a path in pixels. */
static void round_box(sk_path *p, sk_rect b, double radius, double inset, double s)
{
    sk_path_round_rect(p, (float)((b.x + inset) * s), (float)((b.y + inset) * s), (float)((b.w - 2 * inset) * s),
                       (float)((b.h - 2 * inset) * s), (float)(radius * s));
}

/* .pick: the pane's button naming its mode. */
static void draw_button(sk_vizwin *v, sk_image *dst, double s, const pane *p, sk_rect b, bool on, const sk_rect *clip)
{
    if (b.w <= 2)
        return;
    sk_path *path = sk_path_new();
    round_box(path, b, BUTTON_RADIUS, 0, s);
    sk_fill_path(dst, path, NULL, SK_FILL_NONZERO, PICK_BG, clip);
    /* The border: the box less the box inside it. */
    round_box(path, b, BUTTON_RADIUS - 1, 1, s);
    sk_fill_path(dst, path, NULL, SK_FILL_EVENODD, on ? PICK_BORDER_ON : PICK_BORDER, clip);
    sk_path_free(path);
    char text[64];
    snprintf(text, sizeof text, "%s \xe2\x96\xbe", sk_viz_modes[p->shown].label);
    sk_rect inner = sk_rect_intersect(px_rect(sk_rect_make(b.x + 1, b.y + 1, b.w - 2, b.h - 2), s), *clip);
    line_text(v, dst, s, b.x + 1 + BUTTON_PAD, b.y + 1, b.w - 2 - 2 * BUTTON_PAD, text, on ? PICK_TEXT_ON : PICK_TEXT,
              &inner);
}

/* .mode: the pane's place, when there is more than one, its mode, and
 * the input when it is not the sessions alone. */
static void draw_caption(sk_vizwin *v, sk_image *dst, double s, const pane *p, sk_rect r, const sk_rect *clip)
{
    sk_buf t = {0};
    if (v->panes.len > 1)
        sk_buf_printf(&t, "%d  ", p->place);
    sk_buf_append_c(&t, sk_viz_modes[p->shown].label);
    if (v->src > 0 && v->src < (int)SK_LEN(SOURCE))
        sk_buf_printf(&t, "  [%s%s]", SOURCE[v->src], v->st == SK_VIZ_SOUND_FAILED ? ": none, see the log" : "");
    double ascent, descent;
    line_metrics(v, &ascent, &descent);
    float px = (float)(FONT_PT * s);
    float base = (float)((r.y + r.h - CAPTION_FOOT - descent) * s);
    sk_draw_fit(dst, v->c.cache, v->c.mono, 0, px, (float)((r.x + CAPTION_SIDE) * s), base,
                (float)((r.w - 2 * CAPTION_SIDE) * s), sk_buf_cstr(&t), CAPTION, false, clip);
    sk_buf_free(&t);
}

/* .pane.active::after: a line a point wide inside the pane. */
static void draw_outline(sk_image *dst, sk_rect pr, double s)
{
    int k = SK_MAX(1, (int)lround(s));
    sk_fill_rect(dst, sk_rect_make(pr.x, pr.y, pr.w, k), OUTLINE, SK_BLEND_SRC_OVER, &pr);
    sk_fill_rect(dst, sk_rect_make(pr.x, pr.y + pr.h - k, pr.w, k), OUTLINE, SK_BLEND_SRC_OVER, &pr);
    sk_fill_rect(dst, sk_rect_make(pr.x, pr.y + k, k, pr.h - 2 * k), OUTLINE, SK_BLEND_SRC_OVER, &pr);
    sk_fill_rect(dst, sk_rect_make(pr.x + pr.w - k, pr.y + k, k, pr.h - 2 * k), OUTLINE, SK_BLEND_SRC_OVER, &pr);
}

static bool inside(sk_rect b, double x, double y)
{
    return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

/* The open drop-down's pane, or -1. */
static int open_pane(const sk_vizwin *v)
{
    for (size_t i = 0; i < v->panes.len; i++)
        if (v->panes.ptr[i]->open && v->panes.ptr[i]->shown >= 0)
            return (int)i;
    return -1;
}

/* The row of the open drop-down under the pointer, or -1. */
static int hover_row(const sk_vizwin *v, const sk_viz_menu *menu)
{
    if (!v->inside || !menu)
        return -1;
    for (int i = 0; i < menu->n_items; i++)
        if (inside(menu->items[i], v->hx, v->hy))
            return i;
    return -1;
}

/* #menu: a row per mode where layout.h puts it, the pane's own marked. */
static void draw_dropdown(sk_vizwin *v, sk_image *dst, double s, const pane *p, const sk_viz_menu *menu)
{
    int hover = hover_row(v, menu);
    for (int i = 0; i < menu->n_items; i++) {
        sk_rect b = menu->items[i];
        bool current = i == p->shown;
        sk_rect pr = px_rect(b, s);
        sk_fill_rect(dst, pr, current ? ITEM_CURRENT : i == hover ? ITEM_HOVER : ITEM_BG, SK_BLEND_SRC_OVER, NULL);
        char text[64];
        snprintf(text, sizeof text, "%s%s", current ? "\xe2\x96\xb8 " : "  ", sk_viz_modes[i].label);
        line_text(v, dst, s, b.x + ITEM_PAD, b.y, b.w - 2 * ITEM_PAD, text,
                  current || i == hover ? SK_RGB(255, 255, 255) : ITEM_TEXT, &pr);
    }
}

void sk_vizwin_render(sk_vizwin *v, sk_image *dst, int w, int h, double scale, double dt)
{
    v->w = w;
    v->h = h;
    v->scale = scale;
    int n = (int)v->panes.len;
    if (!n)
        sk_fill_rect(dst, sk_image_bounds(dst), SK_RGB(0, 0, 0), SK_BLEND_SRC, NULL);
    int iscale = SK_MAX(1, (int)lround(scale));
    /* The open drop-down lies over every pane, and a row of it under
     * the pointer is hovered rather than a button below it. */
    int open_at = open_pane(v);
    sk_viz_menu menu, list;
    memset(&list, 0, sizeof list);
    if (open_at >= 0)
        sk_viz_menu_of(sk_viz_pane_rect(open_at, n, w, h), SK_VIZ_MODES, w, h, &list);
    bool on_list = open_at >= 0 && hover_row(v, &list) >= 0;
    for (int i = 0; i < n; i++) {
        pane *p = v->panes.ptr[i];
        sk_rect r = sk_viz_pane_rect(i, n, w, h), pr = px_rect(r, scale);
        sk_synth_step(&p->synth, dt, v->src, v->n_samples ? v->samples : NULL, v->n_samples);
        if (p->milk && !p->vm && p->shown >= 0) {
            sk_image view = sk_image_view(dst, pr);
            sk_milk_frame(p->milk, p->synth.samples, dt, &view, iscale);
        } else if (p->vm) {
            const sk_image *im = sk_vmode_draw(p->vm, &p->synth, dt, r.w, r.h, scale);
            sk_blit(dst, pr.x, pr.y, im, sk_image_bounds(im), SK_BLEND_SRC, 255, &pr);
        } else {
            sk_fill_rect(dst, pr, SK_RGB(0, 0, 0), SK_BLEND_SRC, NULL);
        }
        if (p->shown < 0)
            continue;
        sk_viz_menu_of(r, SK_VIZ_MODES, w, h, &menu);
        draw_caption(v, dst, scale, p, r, &pr);
        if (n > 1 && p->active)
            draw_outline(dst, pr, scale);
        bool hover = v->inside && !on_list && inside(menu.button, v->hx, v->hy);
        draw_button(v, dst, scale, p, menu.button, hover || i == open_at, &pr);
    }
    if (open_at >= 0)
        draw_dropdown(v, dst, scale, v->panes.ptr[open_at], &list);
}

/* ---- The pointer ---- */

/* viz.js's point(): the press, its moves and its release to the mode of
 * the pane under the press, in the pane's points. */
static void point(sk_vizwin *v, int kind, double x, double y, uint64_t now)
{
    int n = (int)v->panes.len;
    if (kind == SK_VIZ_DOWN) {
        sk_viz_hit hit;
        if (open_pane(v) >= 0 || sk_viz_press(x, y, n, SK_VIZ_MODES, v->w, v->h, -1, &hit))
            return;
    }
    pane *p = NULL;
    int at = -1;
    if (v->held) {
        p = pane_of(v, v->held);
        for (int i = 0; i < n && p; i++)
            if (v->panes.ptr[i] == p)
                at = i;
    } else {
        at = sk_viz_pane_at(x, y, n, v->w, v->h);
        p = at >= 0 ? v->panes.ptr[at] : NULL;
    }
    if (!p || at < 0)
        return;
    if (kind == SK_VIZ_DOWN)
        v->held = p->tab;
    bool down = kind != SK_VIZ_UP && v->held == p->tab;
    if (kind == SK_VIZ_UP)
        v->held = 0;
    sk_rect r = sk_viz_pane_rect(at, n, v->w, v->h);
    double px = x - r.x, py = y - r.y;
    if (p->vm && sk_vmode_has_pointer(p->vm)) {
        sk_vmode_pointer(p->vm, kind, px, py, down);
    } else if (down && p->shown >= 0 && (kind == SK_VIZ_DOWN || (double)now - v->tone > TONE_MS)) {
        v->tone = (double)now;
        sk_synth_touch(&p->synth, px / SK_MAX(1, r.w), py / SK_MAX(1, r.h));
    }
}

void sk_vizwin_pointer(sk_vizwin *v, sk_event_type type, double x, double y, int button, uint64_t now)
{
    if (v->win)
        sk_window_size(v->win, &v->w, &v->h);
    if (type == SK_EV_MOUSE_LEAVE) {
        v->inside = false;
        return;
    }
    v->inside = true;
    v->hx = x;
    v->hy = y;
    if (type == SK_EV_MOUSE_DOWN) {
        if (button != 1)
            return;
        v->button = true;
        /* The feed reads every left press, as viz.js read the window's
         * mouseDown: the drop-downs are its. */
        if (v->c.feed)
            sk_vizfeed_post_press(v->c.feed, x, y, v->w, v->h);
        point(v, SK_VIZ_DOWN, x, y, now);
    } else if (type == SK_EV_MOUSE_UP) {
        if (button != 1)
            return;
        v->button = false;
        if (v->held)
            point(v, SK_VIZ_UP, x, y, now);
    } else if (type == SK_EV_MOUSE_MOVE) {
        point(v, v->held && !v->button ? SK_VIZ_UP : SK_VIZ_MOVE, x, y, now);
    }
}

/* ---- Events ---- */

static void on_frame(sk_vizwin *v)
{
    uint64_t now = sk_now_ms();
    double dt = v->last_frame ? SK_MIN(0.1, (double)(now - v->last_frame) / 1000.0) : 1.0 / 60;
    v->last_frame = now;
    int w, h;
    sk_window_size(v->win, &w, &h);
    sk_vizwin_render(v, sk_window_frame(v->win), w, h, sk_window_scale(v->win), dt);
    sk_window_present(v->win, NULL, 0);
    sk_window_request_frame(v->win);
}

bool sk_vizwin_event(sk_vizwin *v, const sk_event *ev)
{
    if (!v || !v->win || !ev->win || ev->win != v->win)
        return false;
    switch (ev->type) {
    case SK_EV_CLOSE:
        /* The user's close: the feed hears of it, and the frames of this
         * opening open nothing. */
        v->refused = v->opened;
        v->has_refused = true;
        close_window(v);
        if (v->c.feed)
            sk_vizfeed_post_closed(v->c.feed, v->opened);
        break;
    case SK_EV_FRAME:
        on_frame(v);
        break;
    case SK_EV_RESIZE:
        sk_window_request_frame(v->win);
        break;
    case SK_EV_KEY_DOWN:
        /* The space bar, should a system give this window the keys. */
        if ((ev->key.base == ' ' || !strcmp(ev->key.text, " ")) && !ev->key.repeat && v->c.feed)
            sk_vizfeed_post_step(v->c.feed);
        break;
    case SK_EV_MOUSE_DOWN:
    case SK_EV_MOUSE_UP:
    case SK_EV_MOUSE_MOVE:
        sk_vizwin_pointer(v, ev->type, ev->mouse.x, ev->mouse.y, ev->mouse.button, sk_now_ms());
        break;
    case SK_EV_MOUSE_LEAVE:
        sk_vizwin_pointer(v, ev->type, 0, 0, 0, sk_now_ms());
        break;
    default:
        break;
    }
    return true;
}

/* ---- For tests ---- */

size_t sk_vizwin_panes(const sk_vizwin *v)
{
    return v->panes.len;
}

int sk_vizwin_pane_tab(const sk_vizwin *v, size_t i)
{
    return i < v->panes.len ? v->panes.ptr[i]->tab : 0;
}

int sk_vizwin_shown(const sk_vizwin *v, size_t i)
{
    return i < v->panes.len ? v->panes.ptr[i]->shown : -1;
}

const sk_synth *sk_vizwin_synth(const sk_vizwin *v, size_t i)
{
    return i < v->panes.len ? &v->panes.ptr[i]->synth : NULL;
}

int sk_vizwin_held(const sk_vizwin *v)
{
    return v->held;
}
