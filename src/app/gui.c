#include "gui.h"

#include "../base/log.h"
#include "../base/utf8.h"
#include "../core/command.h"
#include "../core/loop.h"
#include "../platform/procinfo.h"
#include "../platform/ui.h"
#include "../term/keys.h"
#include "../ui/termview.h"
#include "clitab.h"
#include "control.h"
#include "identity.h"
#include "launch.h"

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern char **environ;

#define WINDOW_W 1100   /* main.js's window without a skin, in points */
#define WINDOW_H 760
#define WINDOW_MIN_W 600
#define WINDOW_MIN_H 400
#define STRIP_H 30      /* the tab strip, from the second tab on */
#define TAB_MAX_W 220
#define BLINK_MS 600
#define SYNC_HOLD_MS 1000

typedef struct gui gui;

/* A tab's terminal, which both threads hold: the core feeds it, the UI
 * draws it. The last to let go frees it. */
typedef struct {
    sk_term *term;
    atomic_int refs;
    gui *g;
    int id;
} shared_term;

/* The core's record of a tab. */
typedef struct {
    int id;
    shared_term *st;
} ctab;

/* What the UI shows of the tabs: the core posts a new one on each
 * change. Each tab's terminal is held by the snapshot. */
typedef struct {
    int id;
    char *name;
    bool unseen;
    shared_term *st;
} snap_tab;

typedef struct {
    snap_tab *tabs;
    size_t n;
    int active;
    char *title;
} snapshot;

struct gui {
    const sk_options *o;
    const sk_envp *env;
    sk_launch *l;
    sk_loop *loop;

    /* The core thread's. */
    sk_tabs *tabs;
    sk_clitab_config cli;
    SK_VEC(ctab) ctabs;
    int cols, rows;
    int status;

    /* Between the threads. */
    atomic_bool frame_posted;
    atomic_bool core_done;

    /* The UI thread's. */
    sk_window *win;
    sk_fonts *fonts;
    sk_fontset *mono, *sans;
    sk_glyph_cache *cache;
    sk_termview *view;
    sk_termview_style style;
    snapshot snap;
    bool all;
    bool focused, cursor_on;
    int blink_timer, sync_timer;
    uint64_t sync_since;
    sk_term_mouse mouse;
    bool selecting;
    double wheel;
    uint32_t hover_link;
    int ui_cols, ui_rows;
};

/* ---- Terminals shared by the threads ---- */

static void on_reply(void *ud, const char *data, size_t len);

static shared_term *st_new(gui *g, int id, int cols, int rows)
{
    shared_term *s = sk_xcalloc(1, sizeof *s);
    atomic_init(&s->refs, 1);
    s->g = g;
    s->id = id;
    sk_term_callbacks cb = {.ud = s, .reply = on_reply};
    s->term = sk_term_new(cols, rows, &cb);
    return s;
}

static shared_term *st_ref(shared_term *s)
{
    if (s)
        atomic_fetch_add(&s->refs, 1);
    return s;
}

static void st_unref(shared_term *s)
{
    if (s && atomic_fetch_sub(&s->refs, 1) == 1) {
        sk_term_free(s->term);
        free(s);
    }
}

/* ---- The core thread ---- */

static void log_line(void *ud, const char *msg)
{
    (void)ud;
    sk_log("%s", msg);
}

static void tell_line(void *ud, const char *msg)
{
    (void)ud;
    sk_tell("%s", msg);
}

static ctab *find(gui *g, int id)
{
    for (size_t i = 0; i < g->ctabs.len; i++)
        if (g->ctabs.ptr[i].id == id)
            return &g->ctabs.ptr[i];
    return NULL;
}

/* Any thread: one SK_EV_FRAME on its way, however often asked. */
static void ui_frame_asked(void *ud)
{
    gui *g = ud;
    atomic_store(&g->frame_posted, false);
    sk_window_request_frame(g->win);
}

static void ask_frame(gui *g)
{
    if (!atomic_exchange(&g->frame_posted, true))
        sk_ui_post(ui_frame_asked, g);
}

static void free_snapshot(snapshot *s)
{
    for (size_t i = 0; i < s->n; i++) {
        free(s->tabs[i].name);
        st_unref(s->tabs[i].st);
    }
    free(s->tabs);
    free(s->title);
    memset(s, 0, sizeof *s);
}

static void ui_snapshot(void *ud);

/* The tabs as the UI shows them, posted to it. */
static void post_snapshot(gui *g)
{
    snapshot *s = sk_xcalloc(1, sizeof *s);
    size_t n = sk_tabs_count(g->tabs);
    s->tabs = sk_xcalloc(n ? n : 1, sizeof *s->tabs);
    for (size_t i = 0; i < n; i++) {
        sk_tab *tab = sk_tabs_at(g->tabs, i);
        ctab *ct = find(g, tab->id);
        if (!ct)
            continue;
        snap_tab *t = &s->tabs[s->n++];
        t->id = tab->id;
        t->name = sk_xstrdup(sk_tab_name(tab));
        t->unseen = tab->unseen;
        t->st = st_ref(ct->st);
    }
    sk_tab *active = sk_tabs_active(g->tabs);
    s->active = active ? active->id : 0;
    sk_buf title = {0};
    sk_identity_title(&title, active ? sk_tab_name(active) : "skinned");
    s->title = title.ptr;
    /* The UI frees it, with the gui pointer kept beside it. */
    void **pair = sk_xmalloc(2 * sizeof *pair);
    pair[0] = g;
    pair[1] = s;
    sk_ui_post(ui_snapshot, pair);
}

typedef struct {
    gui *g;
    int id;
    size_t n;
    char data[];
} input_msg;

static void do_input(void *ud)
{
    input_msg *m = ud;
    sk_session *s = sk_clitab_session(sk_tabs_get(m->g->tabs, m->id));
    if (s)
        sk_session_input(s, m->data, m->n);
    free(m);
}

/* Any thread: bytes for tab id's CLI, in the order sent. */
static void post_input(gui *g, int id, const char *data, size_t n)
{
    if (!n)
        return;
    input_msg *m = sk_xmalloc(sizeof *m + n);
    m->g = g;
    m->id = id;
    m->n = n;
    memcpy(m->data, data, n);
    sk_loop_post(g->loop, do_input, m);
}

/* What the terminal answers goes back to its CLI, from whichever thread
 * made it answer. */
static void on_reply(void *ud, const char *data, size_t len)
{
    shared_term *s = ud;
    post_input(s->g, s->id, data, len);
}

static void on_output(void *ud, sk_tab *tab, const char *bytes, size_t n)
{
    gui *g = ud;
    ctab *ct = find(g, tab->id);
    if (ct)
        sk_term_feed(ct->st->term, bytes, n);
    if (tab != sk_tabs_active(g->tabs)) {
        if (sk_tabs_output(g->tabs, tab))
            post_snapshot(g);
        return;
    }
    ask_frame(g);
}

static void on_spawn(void *ud, sk_tab *tab, pid_t pid)
{
    (void)ud;
    sk_log("tab %d: spawn %ld", tab->id, (long)pid);
}

typedef struct {
    gui *g;
    int id;
} tab_msg;

/* A new tab's CLI starts once the tabs have it, at the window's size. */
static void do_start(void *ud)
{
    tab_msg *m = ud;
    sk_session *s = sk_clitab_session(sk_tabs_get(m->g->tabs, m->id));
    if (s && sk_session_get_state(s) == SK_SESSION_IDLE)
        sk_session_start(s, m->g->cols, m->g->rows);
    free(m);
}

static int make_tab(void *ud, sk_tab *tab, const sk_tab_spec *spec, char **err)
{
    gui *g = ud;
    if (spec->kind != SK_TAB_CLI) {
        *err = sk_xstrdup("this build opens only a CLI's tabs");
        return -1;
    }
    if (sk_clitab_make(&g->cli, tab, spec, err) != 0)
        return -1;
    ctab ct = {tab->id, st_new(g, tab->id, g->cols, g->rows)};
    sk_vec_push(&g->ctabs, ct);
    tab_msg *m = sk_xmalloc(sizeof *m);
    m->g = g;
    m->id = tab->id;
    sk_loop_post(g->loop, do_start, m);
    return 0;
}

static void on_change(void *ud)
{
    post_snapshot(ud);
}

static void on_active(void *ud, sk_tab *tab)
{
    (void)tab;
    post_snapshot(ud);
}

static void on_gone(void *ud, sk_tab *tab, int code, sk_tab *next)
{
    (void)code, (void)next;
    gui *g = ud;
    for (size_t i = 0; i < g->ctabs.len; i++) {
        if (g->ctabs.ptr[i].id != tab->id)
            continue;
        st_unref(g->ctabs.ptr[i].st);
        g->ctabs.ptr[i] = g->ctabs.ptr[--g->ctabs.len];
        break;
    }
    post_snapshot(g);
}

/* For tests: SKINNED_SHOT=FILE keeps what the window shows when the
 * app ends, read back from the system, as a QOI image. */
static void shot(gui *g)
{
    const char *path = getenv("SKINNED_SHOT");
    sk_image im;
    if (!path || !*path || !g->win || !sk_window_readback(g->win, &im))
        return;
    size_t n = 0;
    uint8_t *q = sk_qoi_encode(&im, &n);
    FILE *f = q ? fopen(path, "wb") : NULL;
    if (!f || fwrite(q, 1, n, f) != n)
        sk_log("cannot write %s", path);
    if (f)
        fclose(f);
    free(q);
    sk_image_free(&im);
}

static void ui_quit(void *ud)
{
    shot(ud);
    sk_ui_quit();
}

static void on_ended(void *ud, int status)
{
    gui *g = ud;
    g->status = status;
    sk_loop_stop(g->loop);
    sk_ui_post(ui_quit, g);
}

static void do_hangup_all(void *ud)
{
    sk_tabs_hangup_all(((gui *)ud)->tabs);
}

static void on_stop(sk_loop *loop, int signo, void *ud)
{
    (void)loop;
    sk_log("signal %d: hanging up", signo);
    do_hangup_all(ud);
}

/* New tab (decision 0023): in the live folder of the active tab's CLI. */
static void do_new_tab(void *ud)
{
    gui *g = ud;
    sk_tab *from = sk_tabs_active(g->tabs);
    sk_session *s = sk_clitab_session(from);
    char *cwd = s && sk_session_pid(s) > 0 ? sk_procinfo_cwd(sk_session_pid(s)) : NULL;
    char *err = NULL;
    if (!sk_tabs_new_tab(g->tabs, cwd ? cwd : g->l->cwd, &err) && err)
        sk_tell("New tab failed: %s", err);
    free(err);
    free(cwd);
}

static void do_close(void *ud)
{
    tab_msg *m = ud;
    sk_tabs_close(m->g->tabs, m->id);
    free(m);
}

static void do_activate(void *ud)
{
    tab_msg *m = ud;
    sk_tabs_activate(m->g->tabs, m->id);
    free(m);
}

static void do_step(void *ud)
{
    tab_msg *m = ud;
    sk_tabs_step(m->g->tabs, m->id, -1);
    free(m);
}

static void post_tab(gui *g, sk_task_cb fn, int id)
{
    tab_msg *m = sk_xmalloc(sizeof *m);
    m->g = g;
    m->id = id;
    sk_loop_post(g->loop, fn, m);
}

typedef struct {
    gui *g;
    int cols, rows;
} size_msg;

/* Every tab follows the window: its terminal reflows, its pty resizes. */
static void do_resize(void *ud)
{
    size_msg *m = ud;
    gui *g = m->g;
    g->cols = m->cols;
    g->rows = m->rows;
    for (size_t i = 0; i < g->ctabs.len; i++) {
        sk_term_resize(g->ctabs.ptr[i].st->term, g->cols, g->rows);
        sk_session *s = sk_clitab_session(sk_tabs_get(g->tabs, g->ctabs.ptr[i].id));
        if (s)
            sk_session_resize(s, g->cols, g->rows);
    }
    free(m);
}

static void reaped(sk_loop *loop, pid_t pid, int wstatus, void *ud)
{
    (void)loop, (void)pid, (void)wstatus, (void)ud;
}

typedef struct {
    gui *g;
    char uri[];
} uri_msg;

/* A link the user opened, with the system's opener, as Electron's
 * shell.openExternal: http and https alone. */
static void do_open(void *ud)
{
    uri_msg *m = ud;
    if (!strncmp(m->uri, "http://", 7) || !strncmp(m->uri, "https://", 8)) {
#ifdef __APPLE__
        char *argv[] = {"open", m->uri, NULL};
#else
        char *argv[] = {"xdg-open", m->uri, NULL};
#endif
        pid_t pid;
        if (posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ) == 0)
            sk_loop_child(m->g->loop, pid, reaped, NULL);
        else
            sk_log("cannot run %s for a link", argv[0]);
    }
    free(m);
}

static void *core_main(void *ud)
{
    gui *g = ud;
    sk_loop_run(g->loop);
    atomic_store(&g->core_done, true);
    return NULL;
}

/* ---- The UI thread ---- */

static snap_tab *active_tab(gui *g)
{
    for (size_t i = 0; i < g->snap.n; i++)
        if (g->snap.tabs[i].id == g->snap.active)
            return &g->snap.tabs[i];
    return NULL;
}

static sk_term *active_term(gui *g)
{
    snap_tab *t = active_tab(g);
    return t ? t->st->term : NULL;
}

static int strip_height(gui *g)
{
    return g->snap.n > 1 ? (int)lround(STRIP_H * sk_window_scale(g->win)) : 0;
}

/* The terminal's part of the frame, in pixels. */
static sk_rect term_area(gui *g)
{
    sk_image *f = sk_window_frame(g->win);
    int top = strip_height(g);
    return sk_rect_make(0, top, f->w, SK_MAX(0, f->h - top));
}

/* The grid that fits, sent to the core when it changed. */
static void fit(gui *g)
{
    sk_rect a = term_area(g);
    int cols, rows;
    sk_termview_fit(g->view, a.w, a.h, &cols, &rows);
    if (cols == g->ui_cols && rows == g->ui_rows)
        return;
    g->ui_cols = cols;
    g->ui_rows = rows;
    size_msg *m = sk_xmalloc(sizeof *m);
    m->g = g;
    m->cols = cols;
    m->rows = rows;
    sk_loop_post(g->loop, do_resize, m);
}

static void focus_term(gui *g)
{
    sk_term *t = active_term(g);
    if (t)
        sk_term_set_focus(t, g->focused);
}

static void ui_snapshot(void *ud)
{
    void **pair = ud;
    gui *g = pair[0];
    snapshot *s = pair[1];
    free(pair);
    bool switched = s->active != g->snap.active;
    bool strip = (s->n > 1) != (g->snap.n > 1);
    free_snapshot(&g->snap);
    g->snap = *s;
    free(s);
    if (!g->win)
        return;
    sk_window_set_title(g->win, g->snap.title);
    if (strip)
        fit(g);
    if (switched) {
        sk_term_mouse_reset(&g->mouse);
        g->selecting = false;
        g->hover_link = 0;
        focus_term(g);
    }
    g->all = true;
    sk_window_request_frame(g->win);
}

static void tab_geometry(gui *g, size_t i, int w, double s, sk_rect *tab, sk_rect *close)
{
    int pad = (int)lround(4 * s), gap = (int)lround(2 * s), plus = (int)lround(28 * s);
    int avail = w - 2 * pad - plus;
    int tw = (int)SK_MIN((double)lround(TAB_MAX_W * s), (double)(avail - gap * (int)g->snap.n) / (double)g->snap.n);
    tw = SK_MAX(tw, (int)lround(40 * s));
    int h = strip_height(g);
    *tab = sk_rect_make(pad + (int)i * (tw + gap), (int)lround(3 * s), tw, h - (int)lround(3 * s));
    int cw = (int)lround(18 * s);
    *close = sk_rect_make(tab->x + tab->w - cw, tab->y, cw, tab->h);
}

static void draw_strip(gui *g, sk_image *f)
{
    double s = sk_window_scale(g->win);
    int h = strip_height(g);
    sk_fill_rect(f, sk_rect_make(0, 0, f->w, h), SK_RGB(30, 32, 38), SK_BLEND_SRC, NULL);
    float px = (float)(12.5 * s);
    for (size_t i = 0; i < g->snap.n; i++) {
        snap_tab *t = &g->snap.tabs[i];
        sk_rect r, x;
        tab_geometry(g, i, f->w, s, &r, &x);
        bool active = t->id == g->snap.active;
        sk_fill_rect(f, r, active ? SK_RGB(0, 0, 0) : SK_RGB(46, 49, 58), SK_BLEND_SRC, NULL);
        float base = (float)(r.y + r.h / 2) + px * 0.35f;
        float lx = (float)(r.x + lround(10 * s));
        if (!active && t->unseen) {
            int d = (int)lround(6 * s);
            sk_fill_rect(f, sk_rect_make(r.x + (int)lround(6 * s), r.y + (r.h - d) / 2, d, d), SK_RGB(120, 190, 255),
                         SK_BLEND_SRC_OVER, NULL);
            lx += (float)(10 * s);
        }
        sk_rect clip = sk_rect_make(r.x, r.y, r.w - x.w, r.h);
        sk_draw_text(f, g->cache, g->sans, 0, px, lx, base, t->name, strlen(t->name),
                     active ? SK_RGB(235, 237, 242) : SK_RGB(170, 174, 184), &clip);
        const char *times = "\xc3\x97";
        sk_draw_text(f, g->cache, g->sans, 0, px, (float)(x.x + lround(4 * s)), base, times, strlen(times),
                     SK_RGB(150, 154, 164), &x);
    }
    sk_rect r, x;
    tab_geometry(g, g->snap.n, f->w, s, &r, &x);
    const char *plus = "+";
    sk_draw_text(f, g->cache, g->sans, 0, (float)(16 * s), (float)(r.x + lround(6 * s)),
                 (float)(r.y + r.h / 2) + (float)(16 * s) * 0.35f, plus, 1, SK_RGB(170, 174, 184), NULL);
}

static void on_blink(void *ud)
{
    gui *g = ud;
    g->cursor_on = !g->cursor_on;
    g->all = true;
    sk_window_request_frame(g->win);
}

static void on_sync_check(void *ud)
{
    gui *g = ud;
    g->sync_timer = 0;
    sk_window_request_frame(g->win);
}

static void draw(gui *g)
{
    sk_image *f = sk_window_frame(g->win);
    int top = strip_height(g);
    sk_rect dirty[2];
    size_t nd = 0;
    if (g->all && top)
        draw_strip(g, f);
    sk_term *t = active_term(g);
    if (t) {
        sk_rect a = term_area(g);
        sk_image area = sk_image_view(f, a);
        sk_term_lock(t);
        bool hold = false;
        if (sk_term_synchronized(t)) {
            /* xterm.js ends synchronized output held past a second. */
            uint64_t now = sk_now_ms();
            if (!g->sync_since)
                g->sync_since = now;
            if (now - g->sync_since >= SYNC_HOLD_MS)
                sk_term_end_sync(t);
            else
                hold = true;
            if (hold && !g->sync_timer)
                g->sync_timer = sk_ui_timer(SYNC_HOLD_MS - (now - g->sync_since) + 1, 0, on_sync_check, g);
        } else {
            g->sync_since = 0;
        }
        if (!hold) {
            if (g->all) {
                sk_term_palette p;
                sk_term_palette_of(t, &p);
                sk_fill_rect(&area, sk_image_bounds(&area), (sk_color)(0xff000000u | p.bg), SK_BLEND_SRC, NULL);
            }
            sk_termview_state st = {
                .focused = g->focused,
                .cursor_on = g->cursor_on,
                .link = g->hover_link,
                .link_row0 = -1,
            };
            sk_rect r = sk_termview_draw(g->view, t, &area, &st, g->all);
            if (!sk_rect_empty(r))
                dirty[nd++] = sk_rect_make(r.x + a.x, r.y + a.y, r.w, r.h);
        }
        sk_term_cursor c;
        sk_term_cursor_of(t, &c);
        if (c.blink && !g->blink_timer) {
            g->blink_timer = sk_ui_timer(BLINK_MS, BLINK_MS, on_blink, g);
        } else if (!c.blink && g->blink_timer) {
            sk_ui_cancel(g->blink_timer);
            g->blink_timer = 0;
            g->cursor_on = true;
        }
        sk_term_unlock(t);
    }
    if (g->all) {
        sk_window_present(g->win, NULL, 0);
    } else if (nd) {
        sk_window_present(g->win, dirty, nd);
    }
    g->all = false;
}

/* ---- Input ---- */

static void send_active(gui *g, const sk_buf *b)
{
    snap_tab *t = active_tab(g);
    if (t && b->len)
        post_input(g, t->id, b->ptr, b->len);
}

static void copy_selection(gui *g, bool primary)
{
    sk_term *t = active_term(g);
    if (!t)
        return;
    char *text = sk_term_has_selection(t) ? sk_term_selection_text(t) : NULL;
    if (text && *text) {
        if (primary)
            sk_primary_set(text);
        else
            sk_clipboard_set(text);
    }
    free(text);
}

static void on_paste(void *ud, const char *utf8)
{
    gui *g = ud;
    sk_term *t = active_term(g);
    if (!t || !utf8 || !*utf8)
        return;
    sk_term_input_modes m;
    sk_term_input_modes_of(t, &m);
    sk_buf out = {0};
    sk_term_paste(&m, utf8, strlen(utf8), &out);
    sk_term_scroll_to_bottom(t);
    send_active(g, &out);
    sk_buf_free(&out);
}

/* The app's own chords, which the CLI never sees: decision 0023's on
 * Linux, with Cmd for Ctrl+Shift on macOS. */
static bool chord(gui *g, const sk_key_event *k)
{
    uint32_t m = k->mods & (SK_MOD_SHIFT | SK_MOD_CTRL | SK_MOD_ALT | SK_MOD_SUPER);
#ifdef __APPLE__
    const uint32_t app = SK_MOD_SUPER;
#else
    const uint32_t app = SK_MOD_CTRL | SK_MOD_SHIFT;
#endif
    snap_tab *t = active_tab(g);
    if (k->key == SK_KEY_TAB && (m == SK_MOD_CTRL || m == (SK_MOD_CTRL | SK_MOD_SHIFT))) {
        post_tab(g, do_step, m & SK_MOD_SHIFT ? -1 : 1);
        return true;
    }
    if (k->key == SK_KEY_INSERT && m == SK_MOD_SHIFT) {
        sk_clipboard_get(false, on_paste, g);
        return true;
    }
#ifdef __APPLE__
    if (m == (SK_MOD_SUPER | SK_MOD_SHIFT) && (k->base == ']' || k->base == '[')) {
        post_tab(g, do_step, k->base == ']' ? 1 : -1);
        return true;
    }
#endif
    if (m != app)
        return false;
    switch (k->base) {
    case 't':
        sk_loop_post(g->loop, do_new_tab, g);
        return true;
    case 'w':
        if (t)
            post_tab(g, do_close, t->id);
        return true;
    case 'c':
        copy_selection(g, false);
        return true;
    case 'v':
        sk_clipboard_get(false, on_paste, g);
        return true;
    default:
        return false;
    }
}

static void on_key(gui *g, const sk_key_event *k)
{
    if (chord(g, k))
        return;
    sk_term *t = active_term(g);
    if (!t)
        return;
    sk_term_input_modes m;
    sk_term_input_modes_of(t, &m);
    sk_buf out = {0};
    int rows = g->ui_rows;
    sk_term_key_result r = sk_term_key(k, &m, &out);
    switch (r) {
    case SK_TERM_KEY_SEND:
        /* xterm.js's scrollOnUserInput, and a selection ends. */
        sk_term_scroll_to_bottom(t);
        sk_term_select_clear(t);
        send_active(g, &out);
        g->all = true;
        sk_window_request_frame(g->win);
        break;
    case SK_TERM_KEY_PAGE_UP:
    case SK_TERM_KEY_PAGE_DOWN:
        sk_term_scroll(t, r == SK_TERM_KEY_PAGE_UP ? -(rows - 1) : rows - 1);
        g->all = true;
        sk_window_request_frame(g->win);
        break;
    default:
        break;
    }
    sk_buf_free(&out);
}

/* The pointer in points from the grid's top-left. */
static void grid_point(gui *g, double x, double y, double *gx, double *gy)
{
    *gx = x;
    *gy = y - (double)strip_height(g) / sk_window_scale(g->win);
}

static void grid_of(gui *g, sk_term_grid *grid)
{
    int cw, ch;
    sk_termview_cell(g->view, &cw, &ch);
    double s = sk_window_scale(g->win);
    grid->cols = g->ui_cols;
    grid->rows = g->ui_rows;
    grid->cell_w = cw / s;
    grid->cell_h = ch / s;
}

static void hit(gui *g, sk_term *t, double gx, double gy, int *col, int *row, int *edge)
{
    double s = sk_window_scale(g->win);
    sk_termview_hit(g->view, t, (int)floor(gx * s), (int)floor(gy * s), col, row, edge);
}

/* The OSC 8 link of a cell. The terminal's own size, which a resize on
 * its way may make the window's not yet. */
static uint32_t link_at(sk_term *t, int col, int row)
{
    sk_term_lock(t);
    int cols, rows;
    sk_term_size(t, &cols, &rows);
    uint32_t link = 0;
    if (col >= 0 && col < cols && row >= 0 && row < rows) {
        sk_term_cell *cells = sk_xcalloc((size_t)cols, sizeof *cells);
        sk_term_row(t, row, cells);
        link = cells[col].link;
        free(cells);
    }
    sk_term_unlock(t);
    return link;
}

static void open_link(gui *g, sk_term *t, uint32_t link)
{
    const char *uri = sk_term_link_uri(t, link);
    if (!uri)
        return;
    size_t n = strlen(uri);
    uri_msg *m = sk_xmalloc(sizeof *m + n + 1);
    m->g = g;
    memcpy(m->uri, uri, n + 1);
    sk_loop_post(g->loop, do_open, m);
}

static void strip_click(gui *g, const sk_event *ev)
{
    double s = sk_window_scale(g->win);
    int x = (int)floor(ev->mouse.x * s), y = (int)floor(ev->mouse.y * s);
    sk_image *f = sk_window_frame(g->win);
    for (size_t i = 0; i <= g->snap.n; i++) {
        sk_rect r, c;
        tab_geometry(g, i, f->w, s, &r, &c);
        if (x < r.x || x >= r.x + r.w || y < r.y || y >= r.y + r.h)
            continue;
        if (i == g->snap.n) {
            sk_loop_post(g->loop, do_new_tab, g);
        } else if (ev->mouse.button == 2 || (x >= c.x && ev->mouse.button == 1)) {
            post_tab(g, do_close, g->snap.tabs[i].id);
        } else if (ev->mouse.button == 1) {
            post_tab(g, do_activate, g->snap.tabs[i].id);
        }
        return;
    }
}

static void on_mouse(gui *g, const sk_event *ev)
{
    sk_term *t = active_term(g);
    double gx, gy;
    grid_point(g, ev->mouse.x, ev->mouse.y, &gx, &gy);
    if (ev->type == SK_EV_MOUSE_DOWN && gy < 0 && !g->selecting) {
        strip_click(g, ev);
        return;
    }
    if (!t)
        return;
    sk_term_input_modes m;
    sk_term_input_modes_of(t, &m);
    sk_term_grid grid;
    grid_of(g, &grid);
    sk_buf out = {0};
    int col, row, edge;
    hit(g, t, gx, gy, &col, &row, &edge);
    bool redraw = false;
#ifdef __APPLE__
    const uint32_t opener = SK_MOD_SUPER;
#else
    const uint32_t opener = SK_MOD_CTRL;
#endif
    switch (ev->type) {
    case SK_EV_MOUSE_DOWN:
        if (sk_term_mouse_down(&g->mouse, &m, &grid, gx, gy, ev->mouse.button, ev->mouse.mods, &out))
            break;
        if (ev->mouse.button != 1)
            break;
        if (ev->mouse.mods & opener) {
            uint32_t link = link_at(t, col, row);
            if (link) {
                open_link(g, t, link);
                break;
            }
        }
        if (ev->mouse.mods & SK_MOD_SHIFT && sk_term_has_selection(t)) {
            sk_term_select_extend(t, edge, row);
        } else {
            sk_select_kind kind = ev->mouse.clicks >= 3   ? SK_SELECT_LINE
                                  : ev->mouse.clicks == 2 ? SK_SELECT_WORD
                                  : ev->mouse.mods & SK_MOD_ALT ? SK_SELECT_COLUMN
                                                                : SK_SELECT_CHAR;
            sk_term_select_start(t, kind, kind == SK_SELECT_WORD || kind == SK_SELECT_LINE ? col : edge, row);
        }
        g->selecting = true;
        redraw = true;
        break;
    case SK_EV_MOUSE_MOVE:
        if (g->selecting) {
            if (gy < 0)
                sk_term_select_drag_scroll(t, -1);
            else if (gy * sk_window_scale(g->win) >= term_area(g).h)
                sk_term_select_drag_scroll(t, 1);
            else
                sk_term_select_move(t, edge, row);
            redraw = true;
            break;
        }
        sk_term_mouse_move(&g->mouse, &m, &grid, gx, gy, gy >= 0, ev->mouse.mods, &out);
        uint32_t link = gy >= 0 ? link_at(t, col, row) : 0;
        if (link != g->hover_link) {
            g->hover_link = link;
            sk_window_set_cursor(g->win, link ? SK_CURSOR_POINTER : SK_CURSOR_IBEAM);
            redraw = true;
        }
        break;
    case SK_EV_MOUSE_UP:
        sk_term_mouse_up(&g->mouse, &m, &grid, gx, gy, ev->mouse.button, ev->mouse.mods, &out);
        if (g->selecting && ev->mouse.button == 1) {
            g->selecting = false;
            /* The selection is X11's PRIMARY as it is made, as the app's
             * onSelectionChange wrote it. */
            copy_selection(g, true);
        }
        break;
    default:
        break;
    }
    send_active(g, &out);
    sk_buf_free(&out);
    if (redraw) {
        g->all = true;
        sk_window_request_frame(g->win);
    }
}

static void on_scroll(gui *g, const sk_event *ev)
{
    sk_term *t = active_term(g);
    if (!t)
        return;
    double gx, gy;
    grid_point(g, ev->scroll.x, ev->scroll.y, &gx, &gy);
    sk_term_input_modes m;
    sk_term_input_modes_of(t, &m);
    sk_term_grid grid;
    grid_of(g, &grid);
    sk_buf out = {0};
    if (sk_term_mouse_wheel(&g->mouse, &m, &grid, gx, gy, ev->scroll.dy, ev->scroll.precise, ev->scroll.mods,
                            &out)) {
        send_active(g, &out);
    } else {
        /* The view scrolls: by lines, or by points a cell's height to a
         * line, the rest kept for the next event. */
        g->wheel += ev->scroll.precise ? ev->scroll.dy / grid.cell_h : ev->scroll.dy;
        int lines = (int)g->wheel;
        g->wheel -= lines;
        if (lines) {
            sk_term_scroll(t, -lines);
            g->all = true;
            sk_window_request_frame(g->win);
        }
    }
    sk_buf_free(&out);
}

static void on_event(void *ud, const sk_event *ev)
{
    gui *g = ud;
    switch (ev->type) {
    case SK_EV_CLOSE:
        /* As closing the window does (decision 0016): the tabs hang up,
         * and the app ends with the last. */
        sk_loop_post(g->loop, do_hangup_all, g);
        break;
    case SK_EV_RESIZE: {
        double s = sk_window_scale(g->win);
        if (s != g->style.scale) {
            g->style.scale = s;
            sk_termview_set_style(g->view, &g->style);
        }
        fit(g);
        g->all = true;
        sk_window_request_frame(g->win);
        break;
    }
    case SK_EV_FRAME:
        draw(g);
        break;
    case SK_EV_FOCUS:
        g->focused = ev->focused;
        focus_term(g);
        g->all = true;
        sk_window_request_frame(g->win);
        break;
    case SK_EV_KEY_DOWN:
        on_key(g, &ev->key);
        break;
    case SK_EV_MOUSE_DOWN:
    case SK_EV_MOUSE_UP:
    case SK_EV_MOUSE_MOVE:
        on_mouse(g, ev);
        break;
    case SK_EV_MOUSE_LEAVE:
        if (g->hover_link) {
            g->hover_link = 0;
            g->all = true;
            sk_window_request_frame(g->win);
        }
        break;
    case SK_EV_SCROLL:
        on_scroll(g, ev);
        break;
    default:
        break;
    }
}

/* ---- The control socket of the live checks ---- */

typedef struct {
    gui *g;
    sk_control_req *req;
    sk_buf msg;  /* the request, as JSON */
    sk_buf core; /* what the core added: the tabs' members */
} ctl_msg;

static const struct {
    const char *name;
    sk_key key;
} KEYS[] = {
    {"Enter", SK_KEY_ENTER},   {"Tab", SK_KEY_TAB},         {"Backspace", SK_KEY_BACKSPACE},
    {"Escape", SK_KEY_ESCAPE}, {"Up", SK_KEY_UP},           {"Down", SK_KEY_DOWN},
    {"Left", SK_KEY_LEFT},     {"Right", SK_KEY_RIGHT},     {"Home", SK_KEY_HOME},
    {"End", SK_KEY_END},       {"PageUp", SK_KEY_PAGE_UP},  {"PageDown", SK_KEY_PAGE_DOWN},
    {"Insert", SK_KEY_INSERT}, {"Delete", SK_KEY_DELETE},
};

static uint32_t mods_of(const sk_json *v)
{
    uint32_t m = 0;
    for (size_t i = 0; v && i < (v->type == SK_JSON_ARRAY ? v->array.len : 0); i++) {
        sk_str s = {0};
        sk_json_as_string(sk_json_at(v, i), &s);
        m |= sk_str_eq_c(s, "shift")   ? SK_MOD_SHIFT
             : sk_str_eq_c(s, "ctrl")  ? SK_MOD_CTRL
             : sk_str_eq_c(s, "alt")   ? SK_MOD_ALT
             : sk_str_eq_c(s, "super") ? SK_MOD_SUPER
                                       : 0;
    }
    return m;
}

static double num(const sk_json *msg, const char *key, double dflt)
{
    double d;
    return sk_json_as_number(sk_json_get(msg, key), &d) ? d : dflt;
}

/* A key as the platform would give it: a named key, or a character
 * typed with its text and its base. */
static bool key_of(const sk_json *msg, sk_key_event *k)
{
    memset(k, 0, sizeof *k);
    k->mods = mods_of(sk_json_get(msg, "mods"));
    sk_str name = {0}, text = {0};
    sk_json_as_string(sk_json_get(msg, "key"), &name);
    sk_json_as_string(sk_json_get(msg, "text"), &text);
    for (size_t i = 0; i < SK_LEN(KEYS); i++)
        if (sk_str_eq_c(name, KEYS[i].name))
            k->key = KEYS[i].key;
    if (!k->key && name.len > 1 && name.ptr[0] == 'F') {
        int n = atoi(name.ptr + 1);
        if (n >= 1 && n <= 24)
            k->key = (sk_key)(SK_KEY_F1 + n - 1);
    }
    if (k->key)
        return true;
    sk_str ch = text.len ? text : name;
    if (!ch.len || ch.len >= sizeof k->text)
        return false;
    memcpy(k->text, ch.ptr, ch.len);
    uint32_t cp = 0;
    sk_utf8_decode(ch.ptr, ch.len, &cp, NULL);
    k->base = cp >= 'A' && cp <= 'Z' ? cp + 32 : cp;
    if (name.len)
        sk_utf8_decode(name.ptr, name.len, &k->base, NULL);
    /* Ctrl and Super chords type nothing of their own. */
    if (k->mods & (SK_MOD_CTRL | SK_MOD_SUPER) && !text.len)
        k->text[0] = 0;
    return true;
}

static void probe(gui *g, ctl_msg *m)
{
    sk_buf b = {0};
    sk_jw w;
    sk_jw_init(&w, &b, 0);
    sk_jw_object(&w);
    sk_jw_key(&w, SK_STR("ok"));
    sk_jw_bool(&w, true);
    /* The core's members, comma first, after the writer's last value. */
    sk_buf_append(&b, m->core.ptr, m->core.len);
    int pw, ph;
    sk_window_size(g->win, &pw, &ph);
    sk_jw_key(&w, SK_STR("window"));
    sk_jw_object(&w);
    sk_jw_key(&w, SK_STR("w"));
    sk_jw_int(&w, pw);
    sk_jw_key(&w, SK_STR("h"));
    sk_jw_int(&w, ph);
    sk_jw_key(&w, SK_STR("scale"));
    sk_jw_number(&w, sk_window_scale(g->win));
    sk_jw_key(&w, SK_STR("cols"));
    sk_jw_int(&w, g->ui_cols);
    sk_jw_key(&w, SK_STR("rows"));
    sk_jw_int(&w, g->ui_rows);
    sk_jw_key(&w, SK_STR("strip"));
    sk_jw_bool(&w, strip_height(g) > 0);
    sk_jw_object_end(&w);
    sk_term *t = active_term(g);
    sk_jw_key(&w, SK_STR("screen"));
    if (t) {
        sk_term_lock(t);
        char *text = sk_term_screen_text(t);
        sk_jw_string(&w, sk_str_c(text));
        free(text);
        char *sel = sk_term_has_selection(t) ? sk_term_selection_text(t) : NULL;
        sk_jw_key(&w, SK_STR("selection"));
        if (sel)
            sk_jw_string(&w, sk_str_c(sel));
        else
            sk_jw_null(&w);
        free(sel);
        sk_term_cursor c;
        sk_term_cursor_of(t, &c);
        sk_jw_key(&w, SK_STR("cursor"));
        sk_jw_array(&w);
        sk_jw_int(&w, c.x);
        sk_jw_int(&w, c.y);
        sk_jw_array_end(&w);
        sk_term_unlock(t);
    } else {
        sk_jw_null(&w);
    }
    sk_jw_object_end(&w);
    sk_control_answer(m->req, b.ptr, b.len);
    sk_buf_free(&b);
}

/* On the UI thread: each request goes the way the platform's events go. */
static void ui_control(void *ud)
{
    ctl_msg *m = ud;
    gui *g = m->g;
    sk_arena a;
    sk_arena_init(&a, 0);
    const sk_json *msg = sk_json_parse(&a, m->msg.ptr, m->msg.len, NULL);
    sk_str op = {0};
    sk_json_as_string(sk_json_get(msg, "op"), &op);
    sk_event ev;
    memset(&ev, 0, sizeof ev);
    ev.win = g->win;
    const char *why = NULL;
    if (sk_str_eq_c(op, "probe")) {
        probe(g, m);
        m->req = NULL;
    } else if (sk_str_eq_c(op, "key")) {
        ev.type = SK_EV_KEY_DOWN;
        if (key_of(msg, &ev.key))
            on_event(g, &ev);
        else
            why = "no such key";
    } else if (sk_str_eq_c(op, "type")) {
        sk_str text = {0};
        sk_json_as_string(sk_json_get(msg, "text"), &text);
        for (size_t i = 0; i < text.len;) {
            uint32_t cp = 0;
            size_t n = sk_utf8_decode(text.ptr + i, text.len - i, &cp, NULL);
            n = n ? n : 1;
            memset(&ev.key, 0, sizeof ev.key);
            ev.type = SK_EV_KEY_DOWN;
            if (cp == '\n' || cp == '\r') {
                ev.key.key = SK_KEY_ENTER;
            } else if (n < sizeof ev.key.text) {
                memcpy(ev.key.text, text.ptr + i, n);
                ev.key.base = cp >= 'A' && cp <= 'Z' ? cp + 32 : cp;
                ev.key.mods = cp >= 'A' && cp <= 'Z' ? SK_MOD_SHIFT : 0;
            }
            on_event(g, &ev);
            i += n;
        }
    } else if (sk_str_eq_c(op, "mouse")) {
        sk_str kind = {0};
        sk_json_as_string(sk_json_get(msg, "kind"), &kind);
        ev.type = sk_str_eq_c(kind, "down") ? SK_EV_MOUSE_DOWN : sk_str_eq_c(kind, "up") ? SK_EV_MOUSE_UP
                                                                                         : SK_EV_MOUSE_MOVE;
        ev.mouse.x = num(msg, "x", 0);
        ev.mouse.y = num(msg, "y", 0);
        ev.mouse.button = (int)num(msg, "button", ev.type == SK_EV_MOUSE_MOVE ? 0 : 1);
        ev.mouse.clicks = (int)num(msg, "clicks", 1);
        ev.mouse.mods = mods_of(sk_json_get(msg, "mods"));
        on_event(g, &ev);
    } else if (sk_str_eq_c(op, "scroll")) {
        ev.type = SK_EV_SCROLL;
        ev.scroll.x = num(msg, "x", 0);
        ev.scroll.y = num(msg, "y", 0);
        ev.scroll.dy = num(msg, "dy", 0);
        bool precise = false;
        sk_json_as_bool(sk_json_get(msg, "precise"), &precise);
        ev.scroll.precise = precise;
        ev.scroll.mods = mods_of(sk_json_get(msg, "mods"));
        on_event(g, &ev);
    } else if (sk_str_eq_c(op, "focus")) {
        ev.type = SK_EV_FOCUS;
        ev.focused = true;
        sk_json_as_bool(sk_json_get(msg, "focused"), &ev.focused);
        on_event(g, &ev);
    } else if (sk_str_eq_c(op, "close")) {
        ev.type = SK_EV_CLOSE;
        on_event(g, &ev);
    } else if (sk_str_eq_c(op, "shot")) {
        sk_str path = {0};
        sk_json_as_string(sk_json_get(msg, "path"), &path);
        sk_image im;
        size_t n = 0;
        uint8_t *q = NULL;
        FILE *f = NULL;
        if (!path.len || !sk_window_readback(g->win, &im)) {
            why = "cannot read the window back";
        } else {
            q = sk_qoi_encode(&im, &n);
            f = q ? fopen(path.ptr, "wb") : NULL;
            if (!f || fwrite(q, 1, n, f) != n)
                why = "cannot write the image";
            if (f)
                fclose(f);
            free(q);
            sk_image_free(&im);
        }
    } else {
        why = "no such op";
    }
    if (m->req)
        sk_control_ok(m->req, why);
    sk_arena_free(&a);
    sk_buf_free(&m->msg);
    sk_buf_free(&m->core);
    free(m);
}

/* On the core thread: what only the core knows goes in first. */
static void on_control(void *ud, sk_control_req *req, const sk_json *msg)
{
    gui *g = ud;
    sk_str op = {0};
    sk_json_as_string(sk_json_get(msg, "op"), &op);
    if (sk_str_eq_c(op, "hangup")) {
        sk_tabs_hangup_all(g->tabs);
        sk_control_ok(req, NULL);
        return;
    }
    ctl_msg *m = sk_xcalloc(1, sizeof *m);
    m->g = g;
    m->req = req;
    sk_jw w;
    sk_jw_init(&w, &m->msg, 0);
    sk_jw_value(&w, msg);
    if (sk_str_eq_c(op, "probe")) {
        /* The tabs' members of the probe, in place of skinnedProbe's. */
        sk_jw_init(&w, &m->core, 0);
        sk_jw_object(&w);
        sk_tab *active = sk_tabs_active(g->tabs);
        sk_jw_key(&w, SK_STR("active"));
        sk_jw_int(&w, active ? active->id : 0);
        sk_jw_key(&w, SK_STR("tabs"));
        sk_jw_array(&w);
        for (size_t i = 0; i < sk_tabs_count(g->tabs); i++) {
            sk_tab *t = sk_tabs_at(g->tabs, i);
            sk_session *s = sk_clitab_session(t);
            sk_jw_object(&w);
            sk_jw_key(&w, SK_STR("id"));
            sk_jw_int(&w, t->id);
            sk_jw_key(&w, SK_STR("pid"));
            sk_jw_int(&w, s ? sk_session_pid(s) : 0);
            sk_jw_key(&w, SK_STR("active"));
            sk_jw_bool(&w, t == active);
            sk_jw_key(&w, SK_STR("unseen"));
            sk_jw_bool(&w, t->unseen);
            sk_jw_key(&w, SK_STR("label"));
            sk_jw_string(&w, sk_str_c(t->label));
            sk_jw_key(&w, SK_STR("name"));
            sk_jw_string(&w, sk_str_c(sk_tab_name(t)));
            sk_jw_key(&w, SK_STR("cwd"));
            sk_jw_string(&w, sk_str_c(t->cwd));
            sk_jw_key(&w, SK_STR("argv"));
            sk_jw_array(&w);
            for (size_t k = 0; k < t->cmd.len; k++)
                sk_jw_string(&w, sk_str_c(t->cmd.ptr[k]));
            sk_jw_array_end(&w);
            sk_jw_object_end(&w);
        }
        sk_jw_array_end(&w);
        sk_jw_object_end(&w);
        /* The object's members alone, for the UI to wrap. */
        sk_buf inner = {0};
        if (m->core.len > 2)
            sk_buf_append(&inner, m->core.ptr + 1, m->core.len - 2);
        sk_buf_clear(&m->core);
        sk_buf_append_c(&m->core, ",");
        sk_buf_append(&m->core, inner.ptr ? inner.ptr : "", inner.len);
        sk_buf_free(&inner);
    }
    sk_ui_post(ui_control, m);
}

/* ---- The run ---- */

int sk_gui_run(const sk_options *o, const sk_envp *env, const char *self)
{
    sk_launch l;
    int status = sk_launch_open(&l, o, env, self, "skinned");
    if (status)
        return status;
    status = SK_EXIT_APP;
    gui *g = sk_xcalloc(1, sizeof *g);
    g->o = o;
    g->env = env;
    g->l = &l;
    g->status = SK_EXIT_APP;
    g->cursor_on = true;
    g->focused = true;
    atomic_init(&g->frame_posted, false);
    atomic_init(&g->core_done, false);
    char *err = NULL;
    if (!sk_ui_open("skinned", on_event, g, &err)) {
        sk_tell("cannot open a window: %s", err ? err : "unknown");
        free(err);
        free(g);
        sk_launch_close(&l);
        return status;
    }
    g->fonts = sk_fonts_scan(NULL);
    g->mono = sk_fontset_new(g->fonts, NULL, true);
    g->sans = sk_fontset_new(g->fonts, NULL, false);
    g->cache = sk_glyph_cache_new((size_t)32 << 20);
    sk_window_opts wo = {
        .title = "skinned", .w = WINDOW_W, .h = WINDOW_H, .min_w = WINDOW_MIN_W, .min_h = WINDOW_MIN_H, .ud = g};
    /* For the live checks: a window off the screen. */
    const char *hidden = sk_envp_get(env, "SKINNED_HIDDEN");
    wo.hidden = hidden && *hidden == '1';
    g->win = sk_window_new(&wo, &err);
    if (!g->win) {
        sk_tell("cannot open a window: %s", err ? err : "unknown");
        free(err);
        goto out;
    }
    sk_window_set_cursor(g->win, SK_CURSOR_IBEAM);
    sk_termview_default_style(&g->style);
    g->style.fonts = g->mono;
    g->style.cache = g->cache;
    g->style.font_px = 14;
    g->style.scale = sk_window_scale(g->win);
    g->view = sk_termview_new(&g->style);
    sk_term_mouse_init(&g->mouse);
    sk_rect a = term_area(g);
    sk_termview_fit(g->view, a.w, a.h, &g->ui_cols, &g->ui_rows);
    g->cols = g->ui_cols;
    g->rows = g->ui_rows;

    g->loop = sk_loop_new();
    g->cli.loop = g->loop;
    g->cli.run_dir = l.run_dir;
    g->cli.root = l.root;
    g->cli.store = l.store;
    g->cli.relay = l.relay;
    g->cli.statusline = o->statusline;
    g->cli.spawned = on_spawn;
    g->cli.output = on_output;
    g->cli.log = log_line;
    g->cli.tell = tell_line;
    g->cli.ud = g;
    sk_tabs_config tc;
    memset(&tc, 0, sizeof tc);
    tc.make = make_tab;
    tc.follow_background = true;
    tc.tell = tell_line;
    tc.log = log_line;
    tc.change = on_change;
    tc.active = on_active;
    tc.gone = on_gone;
    tc.ended = on_ended;
    tc.ud = g;
    tc.cmd = o->cmd;
    tc.file = l.file;
    tc.env = env;
    tc.home = sk_envp_get(env, "HOME");
    g->tabs = sk_tabs_new(&tc);
    g->cli.tabs = g->tabs;
    sk_loop_signal(g->loop, SIGINT, on_stop, g);
    sk_loop_signal(g->loop, SIGTERM, on_stop, g);
    sk_loop_signal(g->loop, SIGHUP, on_stop, g);
    sk_control *ctl = NULL;
    const char *control = sk_envp_get(env, "SKINNED_CONTROL");
    if (control && *control && !(ctl = sk_control_open(g->loop, control, on_control, g, &err))) {
        sk_tell("%s", err);
        free(err);
        err = NULL;
    }

    /* The first tab runs the command as typed (decision 0003). Until
     * the core thread starts, this thread is the core's. */
    sk_tab_spec spec;
    memset(&spec, 0, sizeof spec);
    spec.kind = SK_TAB_CLI;
    spec.cmd = o->cmd;
    spec.cwd = l.cwd;
    if (!sk_tabs_add(g->tabs, &spec, -1, &err)) {
        sk_tell("cannot open the tab: %s", err ? err : "unknown");
        free(err);
    } else {
        sk_log("window: %s, relay %s, %dx%d", l.file, l.relay ? l.relay : "(none)", g->cols, g->rows);
        post_snapshot(g);
        pthread_t core;
        if (pthread_create(&core, NULL, core_main, g) != 0) {
            sk_tell("cannot start the core thread: %s", strerror(errno));
        } else {
            sk_window_focus(g->win);
            if (!sk_ui_run())
                sk_log("the display went away");
            /* The window's end is the tabs' too: the core ends with the
             * last of them. */
            if (!atomic_load(&g->core_done))
                sk_loop_post(g->loop, do_hangup_all, g);
            pthread_join(core, NULL);
            status = sk_tabs_done(g->tabs) ? sk_tabs_exit_status(g->tabs) : g->status;
        }
    }
    sk_tabs_free(g->tabs);
    for (size_t i = 0; i < g->ctabs.len; i++)
        st_unref(g->ctabs.ptr[i].st);
    sk_vec_free(&g->ctabs);
    sk_control_close(ctl);
    sk_loop_free(g->loop);

out:
    free_snapshot(&g->snap);
    if (g->view)
        sk_termview_free(g->view);
    if (g->win)
        sk_window_free(g->win);
    /* The cache keys glyphs by face, so it goes before the faces do. */
    sk_glyph_cache_free(g->cache);
    sk_fontset_free(g->mono);
    sk_fontset_free(g->sans);
    sk_fonts_free(g->fonts);
    sk_ui_close();
    free(g);
    sk_launch_close(&l);
    return status;
}
