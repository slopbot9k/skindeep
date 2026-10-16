#include "gui.h"

#include "../base/log.h"
#include "../base/utf8.h"
#include "../core/command.h"
#include "../core/loop.h"
#include "../platform/procinfo.h"
#include "../platform/ui.h"
#include "../term/keys.h"
#include "../term/links.h"
#include "../ui/termview.h"
#include "appsettings.h"
#include "clitab.h"
#include "control.h"
#include "dashboard.h"
#include "dashview.h"
#include "foreign.h"
#include "identity.h"
#include "lastskin.h"
#include "launch.h"
#include "menu.h"
#include "monitor.h"
#include "runtime.h"

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
/* app.css's chrome, in points. */
#define IDENTITY_H 22   /* #strip */
#define TABS_H 24       /* #tabs, from the second tab on */
#define TERM_INSET 4    /* #term inside #stage */
#define TAB_MIN_W 60
#define TAB_MAX_W 240
#define NEW_W 26        /* the + */
#define CLOSE_W 16
#define MARK_W 7
#define CHROME_PX 12    /* the strips' font size */
#define DRAG_PX 5       /* points a press moves before it is a drag (tabs.js) */
#define BLINK_MS 600
#define SYNC_HOLD_MS 1000
#define POLL_MS 2000    /* session.js's pollMs for the strip */
#define SESSIONS_MS 2000 /* menu.js's refresh of the agent sessions */
#define FONT_PX 14      /* the terminal's, with no skin and no setting */

/* menu.js's chords as each entry shows them. */
#ifdef __APPLE__
#define KEYS_NEW "Cmd+T"
#define KEYS_CLOSE "Cmd+W"
#else
#define KEYS_NEW "Ctrl+Shift+T"
#define KEYS_CLOSE "Ctrl+Shift+W"
#endif

/* What a row of the drop-down does (menu.js's handlers). */
enum {
    ACT_NONE,
    ACT_NEW_TAB,
    ACT_CHOOSER,
    ACT_CLOSE_TAB,
    ACT_REOPEN,
    ACT_DUPLICATE,
    ACT_NEXT_TAB,
    ACT_PREV_TAB,
    ACT_SETTING,
    ACT_RESET_PANELS,
    ACT_FORGET_SKIN,
    ACT_OPEN_LOGS,
    ACT_CLEAR_LOGS,
    ACT_SESSION,
    ACT_DASHBOARD,
    ACT_OPEN_SKIN,
    ACT_SELECT_SKIN,
    ACT_TOGGLE_VIZ,
    ACT_VIZ_MODE,
    ACT_VIZ_INPUT,
    ACT_TOGGLE_SOUND,
};

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

/* The grid of a window, as its UI last reported it to the core. */
typedef struct {
    int id, cols, rows;
} cwin;

/* What the UI shows of the tabs: the core posts a new one on each
 * change, every window's tabs in it. Each tab's terminal is held by
 * the snapshot. */
typedef struct {
    int id;
    sk_tab_kind kind;
    char *name;
    char *folder; /* the strip's folder, when another tab shows the name */
    bool unseen;
    shared_term *st;
} snap_tab;

/* The identity strip of decision 0004 for the active tab, as
 * session.js's sendStrip fills it. */
typedef struct {
    char *label, *cmdline, *cwd, *notice;
} strip_text;

typedef struct {
    int id;
    snap_tab *tabs;
    size_t n;
    int active;
    char *title;
    strip_text strip;
} snap_win;

typedef struct {
    snap_win *wins;
    size_t n;
} snapshot;

/* A window on the UI thread: the tabs' window win, as the snapshot
 * gave it, and everything its pointer and keys are doing. */
typedef struct {
    gui *g;
    sk_window *win;
    sk_termview *view;
    sk_termview_style style;
    snap_win snap;
    bool all;
    bool focused, cursor_on;
    int blink_timer, sync_timer;
    uint64_t sync_since;
    sk_term_mouse mouse;
    bool selecting;
    double wheel;
    uint32_t hover_link;
    /* A web link under the pointer: viewport rows and columns, end
     * exclusive; link_r0 < 0 for none. */
    int link_r0, link_c0, link_r1, link_c1;
    char *link_uri;
    /* An input method's composition, and where its candidates go. */
    char *preedit;
    int ime_x, ime_y;
    int ui_cols, ui_rows;
    /* A tab pressed in the strip, and whether the press became a drag. */
    /* The tab under the pointer (n for the +, -1 for none), and whether
     * it is the tab's close box. */
    int hover_tab;
    bool hover_close;
    int drag_tab;
    double drag_x0;
    bool dragging;
    /* The drop-down of decision 0022, the skin's button it opens from
     * (in points), and the agent sessions' refresh while it shows them. */
    sk_menu *menu;
    double button[4];
    int sessions_timer;
} gwin;

/* A problem of this launch, for Housekeeping (decision 0022). */
typedef struct {
    char *title, *detail;
} problem;

struct gui {
    const sk_options *o;
    const sk_envp *env;
    sk_launch *l;
    sk_loop *loop;

    /* The core thread's. */
    sk_tabs *tabs;
    sk_clitab_config cli;
    SK_VEC(ctab) ctabs;
    SK_VEC(cwin) cwins;
    char *strip_key; /* the strips last posted, to post a change alone */
    int status;
    sk_env paths;
    sk_foreign *foreign;
    sk_monitor_config mon;
    sk_dashboard_config dash;
    sk_boards *boards;
    char *profiles_dir, *settings_file, *last_dir;
    sk_appsettings settings;
    SK_VEC(problem) problems;
    size_t problems_seen;

    /* Between the threads. */
    atomic_bool frame_posted;
    atomic_bool core_done;

    /* The UI thread's. */
    bool hidden;
    sk_fonts *fonts;
    sk_fontset *mono, *sans;
    sk_glyph_cache *cache;
    double font_px; /* the terminal's, from the settings */
    SK_VEC(gwin *) wins;
    /* Each dashboard tab's pane, by tab id. */
    SK_VEC(struct dview) dviews;
};

struct dview {
    int id;
    sk_dashview *v;
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

/* The grid of window win: its own once its UI has told it, else the
 * first window's, else 80 by 24. */
static void grid_of_win(gui *g, int win, int *cols, int *rows)
{
    *cols = 80;
    *rows = 24;
    for (size_t i = 0; i < g->cwins.len; i++) {
        if (g->cwins.ptr[i].id == win || i == 0) {
            *cols = g->cwins.ptr[i].cols;
            *rows = g->cwins.ptr[i].rows;
        }
        if (g->cwins.ptr[i].id == win)
            return;
    }
}

static void ui_frame_asked(void *ud);

/* Any thread: one round of frames on its way, however often asked. */
static void ask_frame(gui *g)
{
    if (!atomic_exchange(&g->frame_posted, true))
        sk_ui_post(ui_frame_asked, g);
}

static void free_strip(strip_text *s)
{
    free(s->label);
    free(s->cmdline);
    free(s->cwd);
    free(s->notice);
    memset(s, 0, sizeof *s);
}

static void free_snap_win(snap_win *w)
{
    for (size_t i = 0; i < w->n; i++) {
        free(w->tabs[i].name);
        free(w->tabs[i].folder);
        st_unref(w->tabs[i].st);
    }
    free(w->tabs);
    free(w->title);
    free_strip(&w->strip);
    memset(w, 0, sizeof *w);
}

/* The strip of tab as session.js's sendStrip and pollStrip make it: the
 * label, the live command line (after any exec in a wrapper) with the
 * home as ~, the folder it started in, and the adapter's notice with
 * the tab's note. */
static void strip_of(gui *g, sk_tab *tab, strip_text *out)
{
    /* A monitor and the dashboard say their own. */
    sk_tab_strip ms;
    if (sk_monitor_strip(tab, &ms) || sk_dashboard_strip(tab, &ms)) {
        out->label = sk_xstrdup(ms.label ? ms.label : "");
        out->cmdline = sk_xstrdup(ms.cmdline ? ms.cmdline : "");
        out->cwd = sk_xstrdup(ms.cwd ? ms.cwd : "");
        out->notice = ms.notice && *ms.notice ? sk_xstrdup(ms.notice) : NULL;
        return;
    }
    const char *home = sk_envp_get(g->env, "HOME");
    sk_buf b = {0}, live = {0};
    out->label = sk_xstrdup(tab->label);
    sk_session *s = sk_clitab_session(tab);
    pid_t pid = s ? sk_session_pid(s) : 0;
    if (!(pid > 0 && sk_identity_live_cmdline(pid, &live)))
        sk_identity_quote_argv(&live, tab->cmd.ptr);
    sk_identity_shown_cmdline(&b, sk_buf_cstr(&live), home);
    out->cmdline = sk_xstrdup(sk_buf_cstr(&b));
    sk_buf_clear(&b);
    sk_identity_tildify(&b, tab->cwd, home);
    out->cwd = sk_xstrdup(sk_buf_cstr(&b));
    sk_buf_clear(&b);
    sk_agent *a = sk_clitab_agent(tab);
    sk_report r = {0};
    if (a)
        sk_agent_report(a, &r);
    const char *note = sk_clitab_note(tab);
    if (r.notice && *r.notice)
        sk_buf_append_c(&b, r.notice);
    if (note && *note) {
        if (b.len)
            sk_buf_append_c(&b, "  ");
        sk_buf_append_c(&b, note);
    }
    out->notice = b.len ? sk_xstrdup(b.ptr) : NULL;
    sk_buf_free(&b);
    sk_buf_free(&live);
}

static void ui_snapshot(void *ud);

/* Every window's tabs as the UI shows them, posted to it. */
static void post_snapshot(gui *g)
{
    snapshot *s = sk_xcalloc(1, sizeof *s);
    int ids[64];
    size_t nw = sk_tabs_wins(g->tabs, ids, SK_LEN(ids));
    nw = SK_MIN(nw, SK_LEN(ids));
    s->wins = sk_xcalloc(nw ? nw : 1, sizeof *s->wins);
    sk_tab_list list = {0};
    for (size_t k = 0; k < nw; k++) {
        snap_win *w = &s->wins[s->n++];
        w->id = ids[k];
        sk_tabs_in_win(g->tabs, ids[k], &list);
        w->tabs = sk_xcalloc(list.len ? list.len : 1, sizeof *w->tabs);
        for (size_t i = 0; i < list.len; i++) {
            ctab *ct = find(g, list.ptr[i]->id);
            if (!ct)
                continue;
            snap_tab *t = &w->tabs[w->n++];
            t->id = list.ptr[i]->id;
            t->kind = list.ptr[i]->kind;
            t->name = sk_xstrdup(sk_tab_name(list.ptr[i]));
            sk_buf folder = {0};
            if (sk_tabs_folder(g->tabs, list.ptr[i], &folder))
                t->folder = sk_xstrdup(sk_buf_cstr(&folder));
            sk_buf_free(&folder);
            t->unseen = list.ptr[i]->unseen;
            t->st = st_ref(ct->st);
        }
        sk_tab *active = sk_tabs_active_of(g->tabs, ids[k]);
        w->active = active ? active->id : 0;
        sk_buf title = {0};
        sk_identity_title(&title, active ? sk_tab_name(active) : "skinned");
        w->title = title.ptr;
        if (active)
            strip_of(g, active, &w->strip);
    }
    sk_vec_free(&list);
    void **pair = sk_xmalloc(2 * sizeof *pair);
    pair[0] = g;
    pair[1] = s;
    sk_ui_post(ui_snapshot, pair);
}

/* pollStrip, every POLL_MS: the live command line follows an exec in a
 * wrapper and the notice the adapter's report; a change goes to the UI
 * with the tabs. */
static void on_strip_poll(sk_loop *loop, void *ud)
{
    (void)loop;
    gui *g = ud;
    sk_buf key = {0};
    int ids[64];
    size_t nw = SK_MIN(sk_tabs_wins(g->tabs, ids, SK_LEN(ids)), SK_LEN(ids));
    for (size_t k = 0; k < nw; k++) {
        sk_tab *a = sk_tabs_active_of(g->tabs, ids[k]);
        if (!a)
            continue;
        strip_text s = {0};
        strip_of(g, a, &s);
        sk_buf_printf(&key, "%d\x1f%s\x1f%s\x1f%s\x1f%s\x1e", ids[k], s.label, s.cmdline, s.cwd,
                      s.notice ? s.notice : "");
        free_strip(&s);
    }
    if (!g->strip_key || strcmp(g->strip_key, sk_buf_cstr(&key))) {
        free(g->strip_key);
        g->strip_key = sk_xstrdup(sk_buf_cstr(&key));
        post_snapshot(g);
    }
    sk_buf_free(&key);
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
    if (tab->kind != SK_TAB_CLI) {
        /* A pane's text marks nothing unseen. */
        ask_frame(g);
        return;
    }
    if (tab != sk_tabs_active_of(g->tabs, tab->win)) {
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
    int id, win;
} tab_msg;

/* A new tab's CLI starts once the tabs have it, at its window's size; a
 * monitor starts watching. */
static void do_start(void *ud)
{
    tab_msg *m = ud;
    sk_tab *tab = sk_tabs_get(m->g->tabs, m->id);
    if (tab && tab->kind == SK_TAB_MONITOR) {
        sk_monitor_start(tab);
        free(m);
        return;
    }
    if (tab && tab->kind == SK_TAB_DASHBOARD) {
        sk_dashboard_start(tab);
        free(m);
        return;
    }
    sk_session *s = sk_clitab_session(tab);
    if (s && sk_session_get_state(s) == SK_SESSION_IDLE) {
        int cols, rows;
        grid_of_win(m->g, tab->win, &cols, &rows);
        sk_session_start(s, cols, rows);
    }
    free(m);
}

static void post_tab(gui *g, sk_task_cb fn, int id, int win)
{
    tab_msg *m = sk_xmalloc(sizeof *m);
    m->g = g;
    m->id = id;
    m->win = win;
    sk_loop_post(g->loop, fn, m);
}

/* A problem for Housekeeping, logged as main.js logs it. */
static void add_problem(gui *g, const char *title, const char *detail)
{
    problem p = {sk_xstrdup(title), sk_xstrdup(detail ? detail : "")};
    sk_vec_push(&g->problems, p);
    sk_log("%s: %s", title, p.detail);
}

/* A monitor's watch agent, its strip and its pane (decision 0027). */
static sk_agent *mon_watcher(void *ud, const sk_foreign_session *w, sk_bus *bus)
{
    return sk_foreign_watch(((gui *)ud)->foreign, w, bus);
}

static void mon_strip(void *ud, sk_tab *tab, const sk_tab_strip *strip)
{
    (void)tab, (void)strip;
    post_snapshot(ud);
}

static void mon_pane(void *ud, sk_tab *tab, const char *text)
{
    on_output(ud, tab, text, strlen(text));
}

/* The dashboard's sessions, boards and view (decision 0028). */
static void dash_list(void *ud, sk_foreign_entries *out)
{
    sk_foreign_sessions(((gui *)ud)->foreign, out);
}

static void dash_board(void *ud, const char *folder, sk_board *out)
{
    sk_boards_get(((gui *)ud)->boards, folder, out);
}

typedef struct {
    gui *g;
    int id;
    char view[];
} view_msg;

static void ui_dash_view(void *ud);

static void dash_send(void *ud, sk_tab *tab, const char *view)
{
    gui *g = ud;
    size_t n = strlen(view);
    view_msg *m = sk_xmalloc(sizeof *m + n + 1);
    m->g = g;
    m->id = tab->id;
    memcpy(m->view, view, n + 1);
    sk_ui_post(ui_dash_view, m);
}

static int make_tab(void *ud, sk_tab *tab, const sk_tab_spec *spec, char **err)
{
    gui *g = ud;
    if (spec->kind == SK_TAB_MONITOR) {
        if (sk_monitor_make(&g->mon, tab, spec, err) != 0)
            return -1;
    } else if (spec->kind == SK_TAB_DASHBOARD) {
        if (sk_dashboard_make(&g->dash, tab, spec, err) != 0)
            return -1;
    } else if (sk_clitab_make(&g->cli, tab, spec, err) != 0) {
        return -1;
    }
    int cols, rows;
    grid_of_win(g, tab->win, &cols, &rows);
    ctab ct = {tab->id, st_new(g, tab->id, cols, rows)};
    sk_vec_push(&g->ctabs, ct);
    post_tab(g, do_start, tab->id, tab->win);
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

static void ui_quit(void *ud);

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

/* New tab (decision 0023): in window win, in the live folder of its
 * active tab's CLI. */
static void do_new_tab(void *ud)
{
    tab_msg *m = ud;
    gui *g = m->g;
    sk_tabs_touch(g->tabs, m->win);
    sk_tab *from = sk_tabs_active_of(g->tabs, m->win);
    sk_session *s = sk_clitab_session(from);
    char *cwd = s && sk_session_pid(s) > 0 ? sk_procinfo_cwd(sk_session_pid(s)) : NULL;
    char *err = NULL;
    if (!sk_tabs_new_tab(g->tabs, cwd ? cwd : g->l->cwd, &err) && err)
        add_problem(g, "New tab failed", err);
    free(err);
    free(cwd);
    free(m);
}

static void do_close(void *ud)
{
    tab_msg *m = ud;
    sk_tabs_close(m->g->tabs, m->id);
    free(m);
}

/* Closing a window closes its tabs (decision 0016). */
static void do_close_win(void *ud)
{
    tab_msg *m = ud;
    sk_tabs_close_win(m->g->tabs, m->win);
    free(m);
}

static void do_activate(void *ud)
{
    tab_msg *m = ud;
    sk_tabs_activate(m->g->tabs, m->id);
    free(m);
}

/* m->id is the direction here, 1 or -1. */
static void do_step(void *ud)
{
    tab_msg *m = ud;
    sk_tabs_step(m->g->tabs, m->id, m->win);
    free(m);
}

static void do_touch(void *ud)
{
    tab_msg *m = ud;
    sk_tabs_touch(m->g->tabs, m->win);
    free(m);
}

/* A tab dragged within its strip: m->win is the index here. */
static void do_move(void *ud)
{
    tab_msg *m = ud;
    sk_tabs_move(m->g->tabs, m->id, m->win);
    free(m);
}

/* A tab dropped outside its window leaves for a new one (decision
 * 0023). Its terminal is the same object in the new window, so nothing
 * is serialized: the move is done at once. */
static void do_pop(void *ud)
{
    tab_msg *m = ud;
    if (sk_tabs_pop_begin(m->g->tabs, m->id, m->win) && sk_tabs_pop_end(m->g->tabs, m->id) >= 0)
        sk_tabs_pop_done(m->g->tabs, m->id);
    free(m);
}

typedef struct {
    gui *g;
    int win, cols, rows;
} size_msg;

/* A window's tabs follow its size: each terminal reflows, each pty
 * resizes. */
static void do_resize(void *ud)
{
    size_msg *m = ud;
    gui *g = m->g;
    cwin *w = NULL;
    for (size_t i = 0; i < g->cwins.len; i++)
        if (g->cwins.ptr[i].id == m->win)
            w = &g->cwins.ptr[i];
    if (!w) {
        cwin c = {m->win, m->cols, m->rows};
        sk_vec_push(&g->cwins, c);
    } else {
        w->cols = m->cols;
        w->rows = m->rows;
    }
    for (size_t i = 0; i < g->ctabs.len; i++) {
        sk_tab *tab = sk_tabs_get(g->tabs, g->ctabs.ptr[i].id);
        if (!tab || tab->win != m->win)
            continue;
        sk_term_resize(g->ctabs.ptr[i].st->term, m->cols, m->rows);
        sk_session *s = sk_clitab_session(tab);
        if (s)
            sk_session_resize(s, m->cols, m->rows);
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

/* The system's opener on a link or a folder. */
static void run_opener(gui *g, const char *what)
{
#ifdef __APPLE__
    char *argv[] = {"open", (char *)what, NULL};
#else
    char *argv[] = {"xdg-open", (char *)what, NULL};
#endif
    pid_t pid;
    if (posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ) == 0)
        sk_loop_child(g->loop, pid, reaped, NULL);
    else
        sk_log("cannot run %s for %s", argv[0], what);
}

/* A link the user opened, as Electron's shell.openExternal: http and
 * https alone. */
static void do_open(void *ud)
{
    uri_msg *m = ud;
    if (!strncmp(m->uri, "http://", 7) || !strncmp(m->uri, "https://", 8))
        run_opener(m->g, m->uri);
    free(m);
}

/* ---- The drop-down's side on the core thread ---- */

typedef SK_VEC(sk_menu_row) row_vec;

static sk_menu_row *row_add(row_vec *v, sk_row_kind kind, const char *text)
{
    sk_menu_row r;
    memset(&r, 0, sizeof r);
    r.kind = kind;
    r.text = text ? sk_xstrdup(text) : NULL;
    sk_vec_push(v, r);
    return &v->ptr[v->len - 1];
}

static sk_menu_row *row_item(row_vec *v, const char *text, int action, sk_row_then then, const char *keys)
{
    sk_menu_row *r = row_add(v, SK_ROW_ITEM, text);
    r->action = action;
    r->then = then;
    r->keys = keys ? sk_xstrdup(keys) : NULL;
    return r;
}

/* menu.js's age: seconds, minutes, hours or days. */
static char *age_text(double ms)
{
    long s = ms > 0 ? (long)(ms / 1000) : 0;
    sk_buf b = {0};
    if (s < 60)
        sk_buf_printf(&b, "%ld s", s);
    else if (s < 3600)
        sk_buf_printf(&b, "%ld m", s / 60);
    else if (s < 86400)
        sk_buf_printf(&b, "%ld h", s / 3600);
    else
        sk_buf_printf(&b, "%ld d", s / 86400);
    return b.ptr;
}

/* The agent sessions now, as the menu's sessions:list gives them. */
static void sessions_now(gui *g, sk_menu_session **out, size_t *n)
{
    sk_foreign_entries l = {0};
    sk_foreign_sessions(g->foreign, &l);
    *n = l.len;
    *out = sk_xcalloc(l.len ? l.len : 1, sizeof **out);
    for (size_t i = 0; i < l.len; i++) {
        const sk_foreign_entry *e = &l.ptr[i];
        sk_menu_session *s = &(*out)[i];
        s->key = sk_xstrdup(e->key);
        s->label = sk_xstrdup(e->label ? e->label : "");
        s->folder = sk_xstrdup(e->folder ? e->folder : "");
        s->state = sk_xstrdup(e->state ? e->state : "unknown");
        s->age = age_text(e->age);
        s->where = sk_xstrdup(e->where ? e->where : "");
        s->in_tab = e->tab_id != 0;
    }
    sk_foreign_entries_free(&l);
}

/* File: the tab entries of decisions 0023 and 0026, Settings and
 * Housekeeping. Showing it marks this launch's problems seen. */
static void file_rows(gui *g, row_vec *v)
{
    sk_tabs_menu tm;
    sk_tabs_menu_state(g->tabs, &tm);
    row_item(v, "New tab", ACT_NEW_TAB, SK_ROW_CLOSES, KEYS_NEW);
    /* The chooser of decision 0025 is not in this build yet. */
    row_item(v, "New tab...", ACT_CHOOSER, SK_ROW_CLOSES, NULL)->disabled = true;
    row_item(v, "Close tab", ACT_CLOSE_TAB, SK_ROW_CLOSES, KEYS_CLOSE);
    /* Reopen and Duplicate resume or fork the session, else start a new
     * one. */
    sk_buf b = {0};
    sk_buf_printf(&b, "Reopen closed tab%s", tm.reopen && tm.reopen_fresh ? " (new session)" : "");
    row_item(v, b.ptr, ACT_REOPEN, SK_ROW_CLOSES, NULL)->disabled = !tm.reopen;
    sk_buf_clear(&b);
    sk_buf_printf(&b, "Duplicate tab%s", tm.duplicate && tm.duplicate_fresh ? " (new session)" : "");
    row_item(v, b.ptr, ACT_DUPLICATE, SK_ROW_CLOSES, NULL)->disabled = !tm.duplicate;
    row_item(v, "Next tab", ACT_NEXT_TAB, SK_ROW_CLOSES, "Ctrl+Tab");
    row_item(v, "Previous tab", ACT_PREV_TAB, SK_ROW_CLOSES, "Ctrl+Shift+Tab");
    row_add(v, SK_ROW_RULE, NULL);
    row_add(v, SK_ROW_HEADING, "Settings");
    sk_setting_row rows[SK_SETTINGS_COUNT];
    sk_appsettings_menu(&g->settings, NULL, rows);
    for (size_t i = 0; i < SK_SETTINGS_COUNT; i++) {
        size_t at = v->len;
        row_add(v, SK_ROW_FLYOUT, rows[i].setting->label);
        sk_menu_row *f = &v->ptr[at];
        f->nsub = rows[i].choices.len;
        f->sub = sk_xcalloc(f->nsub ? f->nsub : 1, sizeof *f->sub);
        for (size_t k = 0; k < f->nsub; k++) {
            const sk_setting_choice *c = &rows[i].choices.ptr[k];
            sk_menu_row *s = &f->sub[k];
            sk_buf_clear(&b);
            sk_buf_printf(&b, "%s%s", c->current ? "* " : "  ", c->label);
            s->kind = SK_ROW_ITEM;
            s->text = sk_xstrdup(b.ptr);
            s->current = c->current;
            s->action = ACT_SETTING;
            s->then = SK_ROW_WAITS;
            /* The setting's id and its value as JSON, null for the
             * skin's own. */
            sk_buf_clear(&b);
            sk_buf_printf(&b, "%s\x1f", rows[i].setting->id);
            if (c->is_default) {
                sk_buf_append_c(&b, "null");
            } else {
                sk_jw w;
                sk_jw_init(&w, &b, 0);
                sk_jw_number(&w, c->value);
            }
            s->arg = sk_xstrdup(b.ptr);
        }
    }
    sk_appsettings_menu_free(rows);
    /* The skin's splits back to its own sizes (decision 0035): there is
     * no skin to have any yet. */
    row_item(v, "Reset panel sizes", ACT_RESET_PANELS, SK_ROW_WAITS, NULL)->disabled = true;
    row_add(v, SK_ROW_RULE, NULL);
    row_add(v, SK_ROW_HEADING, "Housekeeping");
    for (size_t i = 0; i < g->problems.len; i++) {
        sk_menu_row *p = row_item(v, g->problems.ptr[i].title, ACT_NONE, SK_ROW_SAYS, NULL);
        p->problem = true;
        p->arg = sk_xstrdup(g->problems.ptr[i].detail);
    }
    row_item(v, "Forget remembered skin", ACT_FORGET_SKIN, SK_ROW_WAITS, NULL);
    row_item(v, "Open log folder", ACT_OPEN_LOGS, SK_ROW_CLOSES, NULL);
    row_item(v, "Clear logs", ACT_CLEAR_LOGS, SK_ROW_WAITS, NULL);
    g->problems_seen = g->problems.len;
    sk_buf_free(&b);
}

/* Agents: the agent sessions of decision 0027 and the dashboard of
 * decision 0028. */
static void agents_rows(gui *g, row_vec *v)
{
    row_add(v, SK_ROW_HEADING, "Agent sessions");
    sk_menu_row *box = row_add(v, SK_ROW_SESSIONS, NULL);
    sessions_now(g, &box->sessions, &box->nsessions);
    row_item(v, "Sessions dashboard", ACT_DASHBOARD, SK_ROW_CLOSES, NULL);
}

/* Skins and Visualization, whose subsystems are not in this build yet:
 * their entries, none of them live. */
static void skins_rows(row_vec *v)
{
    row_item(v, "Open skin...", ACT_OPEN_SKIN, SK_ROW_WAITS, NULL)->disabled = true;
    row_add(v, SK_ROW_RULE, NULL);
    row_add(v, SK_ROW_HEADING, "Skins");
    static const char *const shipped[] = {"coffee", "deck", "library", "phosphor"};
    for (size_t i = 0; i < SK_LEN(shipped); i++) {
        char text[32];
        snprintf(text, sizeof text, "  %s", shipped[i]);
        row_item(v, text, ACT_SELECT_SKIN, SK_ROW_WAITS, NULL)->disabled = true;
    }
}

static void viz_rows(row_vec *v)
{
    row_item(v, "Show visualization", ACT_TOGGLE_VIZ, SK_ROW_CLOSES, NULL)->disabled = true;
    row_add(v, SK_ROW_FLYOUT, "Visualization mode");
    row_add(v, SK_ROW_FLYOUT, "Visualization input");
    row_item(v, "Mute skin sounds", ACT_TOGGLE_SOUND, SK_ROW_CLOSES, NULL)->disabled = true;
}

typedef struct {
    gui *g;
    int win, menu;
    unsigned ask;
    sk_menu_row *rows;
    size_t n;
    sk_menu_session *sessions;
} menu_msg;

static void ui_menu(void *ud);
static void ui_sessions(void *ud);

/* A window asked for a menu: its rows, back to the window. */
static void do_menu(void *ud)
{
    menu_msg *m = ud;
    gui *g = m->g;
    sk_tabs_touch(g->tabs, m->win);
    row_vec v = {0};
    switch (m->menu) {
    case SK_MENU_FILE:
        file_rows(g, &v);
        break;
    case SK_MENU_AGENTS:
        agents_rows(g, &v);
        break;
    case SK_MENU_SKINS:
        skins_rows(&v);
        break;
    default:
        viz_rows(&v);
        break;
    }
    m->rows = v.ptr;
    m->n = v.len;
    sk_ui_post(ui_menu, m);
}

/* The agent sessions again, for the box that shows them. */
static void do_sessions(void *ud)
{
    menu_msg *m = ud;
    sessions_now(m->g, &m->sessions, &m->n);
    sk_ui_post(ui_sessions, m);
}

typedef struct {
    gui *g;
    int win, action;
    unsigned press;
    bool waits;
    char *error, *note;
    double font_px;
    char arg[];
} act_msg;

static void ui_answer(void *ud);
static void ui_settings(void *ud);

/* The terminal's size the settings give, or the default without a skin
 * to give one. */
static double font_of(const sk_appsettings *s)
{
    return s->set[SK_SETTING_FONT_SIZE] ? s->value[SK_SETTING_FONT_SIZE] : FONT_PX;
}

/* settings:set: a pick saved and applied to every window. */
static char *set_setting(gui *g, const char *arg)
{
    const char *sep = strchr(arg, '\x1f');
    if (!sep)
        return sk_xstrdup("no such setting");
    char *id = sk_xstrndup(arg, (size_t)(sep - arg));
    sk_arena a;
    sk_arena_init(&a, 0);
    const sk_json *v = sk_json_parse(&a, sep + 1, strlen(sep + 1), NULL);
    char *err = NULL;
    if (!v || !sk_appsettings_valid(id, v) || !sk_appsettings_set(&g->settings, id, v)) {
        sk_buf b = {0};
        sk_buf_printf(&b, "no setting %s of %s", id, sep + 1);
        err = b.ptr;
    } else {
        if (g->settings_file && sk_appsettings_save(g->settings_file, &g->settings) != 0)
            sk_log("settings: %s", strerror(errno));
        act_msg *s = sk_xcalloc(1, sizeof *s);
        s->g = g;
        s->font_px = font_of(&g->settings);
        sk_ui_post(ui_settings, s);
    }
    sk_arena_free(&a);
    free(id);
    return err;
}

/* A row of the drop-down pressed in window m->win: what main.js's
 * handler does, its error or note back to the menu when it waits. */
static void do_action(void *ud)
{
    act_msg *m = ud;
    gui *g = m->g;
    sk_tabs_touch(g->tabs, m->win);
    char *err = NULL, *note = NULL;
    switch (m->action) {
    case ACT_NEW_TAB: {
        tab_msg *t = sk_xmalloc(sizeof *t);
        *t = (tab_msg){g, 0, m->win};
        do_new_tab(t);
        break;
    }
    case ACT_CLOSE_TAB: {
        sk_tab *a = sk_tabs_active_of(g->tabs, m->win);
        if (a)
            sk_tabs_close(g->tabs, a->id);
        break;
    }
    case ACT_REOPEN:
        if (!sk_tabs_reopen(g->tabs, &err) && err) {
            add_problem(g, "Reopen closed tab failed", err);
            free(err);
            err = NULL;
        }
        break;
    case ACT_DUPLICATE:
        if (!sk_tabs_duplicate(g->tabs, &err) && err) {
            add_problem(g, "Duplicate tab failed", err);
            free(err);
            err = NULL;
        }
        break;
    case ACT_NEXT_TAB:
    case ACT_PREV_TAB:
        sk_tabs_step(g->tabs, m->action == ACT_NEXT_TAB ? 1 : -1, m->win);
        break;
    case ACT_SETTING:
        err = set_setting(g, m->arg);
        break;
    case ACT_FORGET_SKIN:
        if (!g->last_dir || sk_last_forget(g->last_dir) != 0) {
            sk_buf b = {0};
            sk_buf_printf(&b, "forget: %s", g->last_dir ? strerror(errno) : "no state folder");
            err = b.ptr;
        } else {
            note = sk_xstrdup("Forgotten. The next launch opens the default skin, unless a window records another.");
        }
        break;
    case ACT_OPEN_LOGS: {
        char *dir = sk_runtime_log_dir(&g->paths);
        if (dir) {
            sk_mkdirs(dir, 0700);
            run_opener(g, dir);
        }
        free(dir);
        break;
    }
    case ACT_CLEAR_LOGS: {
        int n = sk_runtime_clear_logs(&g->paths);
        sk_buf b = {0};
        if (n < 0)
            sk_buf_printf(&b, "clear logs: %s", strerror(errno));
        else
            sk_buf_printf(&b, "Removed %d log%s of ended windows.", n, n == 1 ? "" : "s");
        if (n < 0)
            err = b.ptr;
        else
            note = b.ptr;
        break;
    }
    case ACT_SESSION:
        sk_foreign_open(g->foreign, m->arg, &err);
        break;
    case ACT_DASHBOARD:
        if (!sk_dashboard_open(g->tabs, g->l->cwd, &err) && err) {
            add_problem(g, "Sessions dashboard failed", err);
            free(err);
            err = NULL;
        }
        break;
    default:
        err = sk_xstrdup("not in this build yet");
        break;
    }
    if (m->waits) {
        m->error = err;
        m->note = note;
        sk_ui_post(ui_answer, m);
        return;
    }
    free(err);
    free(note);
    free(m);
}

static void *core_main(void *ud)
{
    gui *g = ud;
    sk_loop_run(g->loop);
    atomic_store(&g->core_done, true);
    return NULL;
}

/* ---- The UI thread: windows ---- */

static void on_event(void *ud, const sk_event *ev);

static snap_tab *active_tab(gwin *w)
{
    for (size_t i = 0; i < w->snap.n; i++)
        if (w->snap.tabs[i].id == w->snap.active)
            return &w->snap.tabs[i];
    return NULL;
}

static sk_term *active_term(gwin *w)
{
    snap_tab *t = active_tab(w);
    return t ? t->st->term : NULL;
}

static int px(gwin *w, double points)
{
    return (int)lround(points * sk_window_scale(w->win));
}

/* app.css: the identity strip, then the tab strip from the second tab
 * on, then the stage. In points. */
static bool tabs_shown(gwin *w)
{
    return w->snap.n > 1;
}

static double top_points(gwin *w)
{
    return IDENTITY_H + (tabs_shown(w) ? TABS_H : 0);
}

/* The stage below the strips, in pixels. */
static sk_rect stage_area(gwin *w)
{
    sk_image *f = sk_window_frame(w->win);
    int top = px(w, top_points(w));
    return sk_rect_make(0, top, f->w, SK_MAX(0, f->h - top));
}

/* The terminal's part of the frame, in pixels: #term's 4 points in. */
static sk_rect term_area(gwin *w)
{
    sk_rect s = stage_area(w);
    int in = px(w, TERM_INSET);
    return sk_rect_make(s.x + in, s.y + in, SK_MAX(0, s.w - 2 * in), SK_MAX(0, s.h - 2 * in));
}

static void redraw(gwin *w)
{
    w->all = true;
    sk_window_request_frame(w->win);
}

/* The grid that fits, sent to the core when it changed. */
static void fit(gwin *w)
{
    sk_rect a = term_area(w);
    int cols, rows;
    sk_termview_fit(w->view, a.w, a.h, &cols, &rows);
    if (cols == w->ui_cols && rows == w->ui_rows)
        return;
    w->ui_cols = cols;
    w->ui_rows = rows;
    size_msg *m = sk_xmalloc(sizeof *m);
    m->g = w->g;
    m->win = w->snap.id;
    m->cols = cols;
    m->rows = rows;
    sk_loop_post(w->g->loop, do_resize, m);
}

static void focus_term(gwin *w)
{
    sk_term *t = active_term(w);
    if (t)
        sk_term_set_focus(t, w->focused);
}

static void clear_hover(gwin *w)
{
    w->hover_link = 0;
    free(w->link_uri);
    w->link_uri = NULL;
    w->link_r0 = -1;
}

static gwin *gwin_of(gui *g, int id)
{
    for (size_t i = 0; i < g->wins.len; i++)
        if (g->wins.ptr[i]->snap.id == id)
            return g->wins.ptr[i];
    return NULL;
}

static gwin *gwin_new(gui *g, int id)
{
    char *err = NULL;
    gwin *w = sk_xcalloc(1, sizeof *w);
    w->g = g;
    w->snap.id = id;
    w->focused = true;
    w->cursor_on = true;
    w->link_r0 = -1;
    w->ime_x = w->ime_y = -1;
    w->hover_tab = -1;
    w->drag_tab = 0;
    sk_window_opts wo = {.title = "skinned",
                         .w = WINDOW_W,
                         .h = WINDOW_H,
                         .min_w = WINDOW_MIN_W,
                         .min_h = WINDOW_MIN_H,
                         .ud = w,
                         .hidden = g->hidden};
    w->win = sk_window_new(&wo, &err);
    if (!w->win) {
        sk_tell("cannot open a window: %s", err ? err : "unknown");
        free(err);
        free(w);
        return NULL;
    }
    sk_window_set_cursor(w->win, SK_CURSOR_IBEAM);
    sk_termview_default_style(&w->style);
    w->style.fonts = g->mono;
    w->style.cache = g->cache;
    w->style.font_px = g->font_px;
    w->style.scale = sk_window_scale(w->win);
    w->view = sk_termview_new(&w->style);
    sk_term_mouse_init(&w->mouse);
    w->menu = sk_menu_new();
    sk_vec_push(&g->wins, w);
    return w;
}

static void gwin_free(gwin *w)
{
    if (w->blink_timer)
        sk_ui_cancel(w->blink_timer);
    if (w->sync_timer)
        sk_ui_cancel(w->sync_timer);
    if (w->sessions_timer)
        sk_ui_cancel(w->sessions_timer);
    sk_menu_free(w->menu);
    free_snap_win(&w->snap);
    free(w->link_uri);
    free(w->preedit);
    sk_termview_free(w->view);
    sk_window_free(w->win);
    free(w);
}

static void drop_dviews(gui *g);

/* Each window as the snapshot has it: new windows open, windows with no
 * tabs left close. */
static void ui_snapshot(void *ud)
{
    void **pair = ud;
    gui *g = pair[0];
    snapshot *s = pair[1];
    free(pair);
    /* No tab left is the app's end, which quits on its own: the windows
     * stay as they were until then. */
    if (!s->n) {
        free(s->wins);
        free(s);
        return;
    }
    for (size_t i = 0; i < g->wins.len;) {
        gwin *w = g->wins.ptr[i];
        bool kept = false;
        for (size_t k = 0; k < s->n && !kept; k++)
            kept = s->wins[k].id == w->snap.id;
        if (kept) {
            i++;
            continue;
        }
        gwin_free(w);
        g->wins.ptr[i] = g->wins.ptr[--g->wins.len];
    }
    for (size_t k = 0; k < s->n; k++) {
        snap_win *sw = &s->wins[k];
        gwin *w = gwin_of(g, sw->id);
        bool fresh = !w;
        if (fresh && !(w = gwin_new(g, sw->id))) {
            free_snap_win(sw);
            continue;
        }
        bool switched = sw->active != w->snap.active;
        bool strip = (sw->n > 1) != (w->snap.n > 1);
        free_snap_win(&w->snap);
        w->snap = *sw;
        sk_window_set_title(w->win, w->snap.title);
        if (strip || fresh)
            fit(w);
        if (switched) {
            sk_term_mouse_reset(&w->mouse);
            w->selecting = false;
            clear_hover(w);
            focus_term(w);
        }
        if (fresh)
            sk_window_focus(w->win);
        redraw(w);
    }
    drop_dviews(g);
    free(s->wins);
    free(s);
}

static void ui_frame_asked(void *ud)
{
    gui *g = ud;
    atomic_store(&g->frame_posted, false);
    for (size_t i = 0; i < g->wins.len; i++)
        sk_window_request_frame(g->wins.ptr[i]->win);
}

/* For tests: SKINNED_SHOT=FILE keeps what the first window shows when
 * the app ends, read back from the system, as a QOI image. */
static bool shot(gwin *w, const char *path)
{
    sk_image im;
    if (!path || !*path || !w || !sk_window_readback(w->win, &im))
        return false;
    size_t n = 0;
    uint8_t *q = sk_qoi_encode(&im, &n);
    FILE *f = q ? fopen(path, "wb") : NULL;
    bool ok = f && fwrite(q, 1, n, f) == n;
    if (f)
        ok = fclose(f) == 0 && ok;
    free(q);
    sk_image_free(&im);
    return ok;
}

static void ui_quit(void *ud)
{
    gui *g = ud;
    const char *path = getenv("SKINNED_SHOT");
    if (path && *path && g->wins.len && !shot(g->wins.ptr[0], path))
        sk_log("cannot write %s", path);
    sk_ui_quit();
}

/* ---- The drop-down ---- */

static sk_menu_style menu_style(gwin *w)
{
    return (sk_menu_style){w->g->sans, w->g->mono, w->g->cache, sk_window_scale(w->win)};
}

/* menu.js's show: the menu asked of the core, the newest ask winning. */
static void menu_ask(gwin *w, int menu)
{
    menu_msg *m = sk_xcalloc(1, sizeof *m);
    m->g = w->g;
    m->win = w->snap.id;
    m->menu = menu;
    m->ask = sk_menu_ask(w->menu, menu);
    sk_loop_post(w->g->loop, do_menu, m);
}

static void on_sessions_tick(void *ud)
{
    gwin *w = ud;
    if (!sk_menu_has_sessions(w->menu)) {
        sk_ui_cancel(w->sessions_timer);
        w->sessions_timer = 0;
        return;
    }
    menu_msg *m = sk_xcalloc(1, sizeof *m);
    m->g = w->g;
    m->win = w->snap.id;
    sk_loop_post(w->g->loop, do_sessions, m);
}

/* The sessions refresh every 2 s while the menu shows them, and no
 * longer. */
static void menu_changed(gwin *w)
{
    bool want = sk_menu_has_sessions(w->menu);
    if (want && !w->sessions_timer)
        w->sessions_timer = sk_ui_timer(SESSIONS_MS, SESSIONS_MS, on_sessions_tick, w);
    else if (!want && w->sessions_timer) {
        sk_ui_cancel(w->sessions_timer);
        w->sessions_timer = 0;
    }
    redraw(w);
}

static void ui_menu(void *ud)
{
    menu_msg *m = ud;
    gwin *w = gwin_of(m->g, m->win);
    if (!w) {
        sk_menu_rows_free(m->rows, m->n);
    } else {
        int pw, ph;
        sk_window_size(w->win, &pw, &ph);
        sk_menu_style st = menu_style(w);
        if (sk_menu_show(w->menu, m->ask, m->menu, m->rows, m->n, &st, w->button[0], w->button[1], w->button[2],
                         w->button[3], pw, ph, top_points(w)))
            menu_changed(w);
    }
    free(m);
}

static void ui_sessions(void *ud)
{
    menu_msg *m = ud;
    gwin *w = gwin_of(m->g, m->win);
    if (w && sk_menu_has_sessions(w->menu)) {
        sk_menu_sessions(w->menu, m->sessions, m->n);
        redraw(w);
    } else {
        sk_menu_sessions_free(m->sessions, m->n);
    }
    free(m);
}

static void ui_answer(void *ud)
{
    act_msg *m = ud;
    gwin *w = gwin_of(m->g, m->win);
    if (w) {
        sk_menu_answer(w->menu, m->press, m->error, m->note);
        menu_changed(w);
    }
    free(m->error);
    free(m->note);
    free(m);
}

/* A setting picked: every window's terminal at its new size. */
static void ui_settings(void *ud)
{
    act_msg *m = ud;
    gui *g = m->g;
    g->font_px = m->font_px;
    for (size_t i = 0; i < g->wins.len; i++) {
        gwin *w = g->wins.ptr[i];
        w->style.font_px = g->font_px;
        sk_termview_set_style(w->view, &w->style);
        fit(w);
        redraw(w);
    }
    free(m);
}

/* A press on the drop-down: what it asks for, run on the core. */
static void menu_press(gwin *w, const sk_menu_press *p)
{
    if (p->kind == SK_MENU_PRESS_SHOW) {
        menu_ask(w, p->menu);
    } else if (p->kind == SK_MENU_PRESS_ACTION || p->kind == SK_MENU_PRESS_SESSION) {
        size_t n = p->arg ? strlen(p->arg) : 0;
        act_msg *m = sk_xcalloc(1, sizeof *m + n + 1);
        m->g = w->g;
        m->win = w->snap.id;
        m->action = p->kind == SK_MENU_PRESS_SESSION ? ACT_SESSION : p->action;
        m->press = p->press;
        m->waits = p->waits;
        if (n)
            memcpy(m->arg, p->arg, n);
        sk_loop_post(w->g->loop, do_action, m);
    }
    menu_changed(w);
}

/* ---- The dashboard's pane ---- */

static sk_dashview *dview_of(gui *g, int id, bool make)
{
    for (size_t i = 0; i < g->dviews.len; i++)
        if (g->dviews.ptr[i].id == id)
            return g->dviews.ptr[i].v;
    if (!make)
        return NULL;
    struct dview d = {id, sk_dashview_new()};
    sk_vec_push(&g->dviews, d);
    return d.v;
}

/* The pane of w's active tab when it is the dashboard's. */
static sk_dashview *active_dview(gwin *w)
{
    snap_tab *t = active_tab(w);
    return t && t->kind == SK_TAB_DASHBOARD ? dview_of(w->g, t->id, false) : NULL;
}

static void ui_dash_view(void *ud)
{
    view_msg *m = ud;
    gui *g = m->g;
    sk_dashview_update(dview_of(g, m->id, true), m->view, strlen(m->view));
    for (size_t i = 0; i < g->wins.len; i++)
        if (g->wins.ptr[i]->snap.active == m->id)
            redraw(g->wins.ptr[i]);
    free(m);
}

/* Panes whose tab no window has any more. */
static void drop_dviews(gui *g)
{
    for (size_t i = 0; i < g->dviews.len;) {
        bool kept = false;
        for (size_t k = 0; k < g->wins.len && !kept; k++)
            for (size_t j = 0; j < g->wins.ptr[k]->snap.n && !kept; j++)
                kept = g->wins.ptr[k]->snap.tabs[j].id == g->dviews.ptr[i].id;
        if (kept) {
            i++;
            continue;
        }
        sk_dashview_free(g->dviews.ptr[i].v);
        g->dviews.ptr[i] = g->dviews.ptr[--g->dviews.len];
    }
}

/* The pointer on the dashboard's pane: the live row under it lit, a
 * press opening its session, the wheel scrolling it. True when the
 * event was the pane's. */
static bool dash_mouse(gwin *w, const sk_event *ev)
{
    snap_tab *at = active_tab(w);
    if (!at || at->kind != SK_TAB_DASHBOARD)
        return false;
    sk_dashview *v = active_dview(w);
    if (!v)
        return true;
    double x = ev->mouse.x - TERM_INSET, y = ev->mouse.y - top_points(w) - TERM_INSET;
    if (ev->type == SK_EV_MOUSE_MOVE && sk_dashview_hover(v, x, y))
        redraw(w);
    if (ev->type == SK_EV_MOUSE_DOWN && ev->mouse.button == 1) {
        const char *key = sk_dashview_hit(v, x, y);
        if (key) {
            size_t n = strlen(key);
            act_msg *m = sk_xcalloc(1, sizeof *m + n + 1);
            m->g = w->g;
            m->win = w->snap.id;
            m->action = ACT_SESSION;
            memcpy(m->arg, key, n);
            sk_loop_post(w->g->loop, do_action, m);
        }
    }
    return true;
}

/* ---- Drawing ---- */

/* The baseline of a line of the chrome's mono text at size, its line box
 * lh high from top: the font's ascent and descent centred, as CSS sets
 * a line. */
static float baseline_in(gui *g, int style, float size, float top, float lh)
{
    sk_font_vmetrics m;
    sk_font_vmetrics_at(sk_fontset_primary(g->mono, style), size, &m);
    return top + (lh - (m.ascent + m.descent)) / 2 + m.ascent;
}

/* text drawn at most maxw wide, cut with an ellipsis as text-overflow:
 * ellipsis cuts it. Returns the width drawn. */
static float draw_fit(gui *g, sk_image *f, int style, float size, float x, float base, float maxw, const char *s,
                      sk_color c)
{
    size_t n = strlen(s);
    float w = sk_measure_text(g->mono, style, size, s, n);
    /* Half a pixel of slack: a box sized to its text is rounded. */
    if (w <= maxw + 0.5f) {
        sk_draw_text(f, g->cache, g->mono, style, size, x, base, s, n, c, NULL);
        return w;
    }
    const char *ell = "\xe2\x80\xa6";
    float ew = sk_measure_text(g->mono, style, size, ell, 3);
    size_t keep = 0;
    float kw = 0;
    for (size_t i = 0; i < n;) {
        uint32_t cp;
        size_t k = sk_utf8_decode(s + i, n - i, &cp, NULL);
        k = k ? k : 1;
        float cw = sk_measure_text(g->mono, style, size, s, i + k);
        if (cw + ew > maxw)
            break;
        keep = i + k;
        kw = cw;
        i += k;
    }
    if (ew > maxw)
        return 0;
    sk_draw_text(f, g->cache, g->mono, style, size, x, base, s, keep, c, NULL);
    sk_draw_text(f, g->cache, g->mono, style, size, x + kw, base, ell, 3, c, NULL);
    return kw + ew;
}

/* A tab's parts across, in points: padding 8, the mark, gaps of 6, the
 * name and folder, the close box, padding 4, a 1-point border. */
static double tab_fixed(void)
{
    return 8 + MARK_W + 6 + 6 + CLOSE_W + 4 + 1;
}

/* The tabs as #tabs's flexbox lays them out, in pixels: each its content
 * wide within 60 to 240 points, all shrunk alike when they would pass
 * the +, none under 60. rects[n] is the +; close[i] each close box. */
static void tab_layout(gwin *w, int width, sk_rect *rects, sk_rect *close)
{
    gui *g = w->g;
    double s = sk_window_scale(w->win);
    float size = (float)(CHROME_PX * s);
    int y = px(w, IDENTITY_H), h = px(w, TABS_H) - px(w, 1);
    double *want = sk_xcalloc(w->snap.n ? w->snap.n : 1, sizeof *want), sum = 0;
    for (size_t i = 0; i < w->snap.n; i++) {
        snap_tab *t = &w->snap.tabs[i];
        double pts = tab_fixed() + sk_measure_text(g->mono, 0, size, t->name, strlen(t->name)) / s;
        if (t->folder)
            pts += 6 + sk_measure_text(g->mono, 0, size, t->folder, strlen(t->folder)) / s;
        want[i] = SK_MIN(SK_MAX(pts, (double)TAB_MIN_W), (double)TAB_MAX_W) * s;
        sum += want[i];
    }
    double avail = width - NEW_W * s;
    double f = sum > avail && sum > 0 ? avail / sum : 1;
    int x = 0;
    for (size_t i = 0; i < w->snap.n; i++) {
        int tw = (int)ceil(SK_MAX(want[i] * f, TAB_MIN_W * s));
        rects[i] = sk_rect_make(x, y, tw, h);
        close[i] = sk_rect_make(x + tw - px(w, 4 + 1 + CLOSE_W), y + (h - px(w, CLOSE_W)) / 2, px(w, CLOSE_W),
                                px(w, CLOSE_W));
        x += tw;
    }
    rects[w->snap.n] = sk_rect_make(x, y, px(w, NEW_W), h);
    close[w->snap.n] = sk_rect_make(0, 0, 0, 0);
    free(want);
}

static void fill_circle(sk_image *f, float cx, float cy, float r, sk_color c)
{
    sk_path *p = sk_path_new();
    sk_path_ellipse(p, cx - r, cy - r, 2 * r, 2 * r);
    sk_fill_path(f, p, NULL, SK_FILL_NONZERO, c, NULL);
    sk_path_free(p);
}

/* The identity strip (#strip) and, from the second tab on, the tab strip
 * (#tabs), as app.css draws them. */
static void draw_chrome(gwin *w, sk_image *f)
{
    gui *g = w->g;
    double s = sk_window_scale(w->win);
    float size = (float)(CHROME_PX * s);
    int line = px(w, 1);
    /* #strip: the label, the command line taking what is left, the
     * notice and the folder at the right, 14 points apart. */
    int ih = px(w, IDENTITY_H);
    sk_fill_rect(f, sk_rect_make(0, 0, f->w, ih), SK_RGB(0x10, 0x10, 0x10), SK_BLEND_SRC, NULL);
    sk_fill_rect(f, sk_rect_make(0, ih - line, f->w, line), SK_RGB(0x33, 0x33, 0x33), SK_BLEND_SRC, NULL);
    /* A flexbox left to right, four items 14 points apart, the empty
     * ones too: the command line alone grows and shrinks, to nothing at
     * least, and what passes the right edge is cut off there. */
    strip_text *st = &w->snap.strip;
    float pad = (float)(8 * s), gap = (float)(14 * s);
    float base = baseline_in(g, 0, size, 0, (float)ih);
    const char *label = st->label ? st->label : "", *cmd = st->cmdline ? st->cmdline : "";
    const char *notice = st->notice ? st->notice : "", *cwd = st->cwd ? st->cwd : "";
    float lw = sk_measure_text(g->mono, SK_STYLE_BOLD, size, label, strlen(label));
    float nw = sk_measure_text(g->mono, 0, size, notice, strlen(notice));
    float cw = sk_measure_text(g->mono, 0, size, cwd, strlen(cwd));
    float cmd_x = pad + lw + gap;
    float cmd_w = SK_MAX(0.0f, (float)f->w - pad - cw - gap - nw - gap - cmd_x);
    sk_draw_text(f, g->cache, g->mono, SK_STYLE_BOLD, size, pad, base, label, strlen(label), SK_RGB(0xff, 0xff, 0xff),
                 NULL);
    draw_fit(g, f, 0, size, cmd_x, base, cmd_w, cmd, SK_RGB(0x9f, 0xb3, 0xc8));
    float nx = cmd_x + cmd_w + gap;
    sk_draw_text(f, g->cache, g->mono, 0, size, nx, base, notice, strlen(notice), SK_RGB(0xe0, 0xb0, 0x50), NULL);
    sk_draw_text(f, g->cache, g->mono, 0, size, nx + nw + gap, base, cwd, strlen(cwd), SK_RGB(0x88, 0x88, 0x88), NULL);
    if (!tabs_shown(w))
        return;
    /* #tabs: each tab its mark, name, folder and close box. */
    int ty = ih, th = px(w, TABS_H);
    sk_fill_rect(f, sk_rect_make(0, ty, f->w, th), SK_RGB(0x0b, 0x0b, 0x0b), SK_BLEND_SRC, NULL);
    sk_fill_rect(f, sk_rect_make(0, ty + th - line, f->w, line), SK_RGB(0x33, 0x33, 0x33), SK_BLEND_SRC, NULL);
    sk_rect *rects = sk_xcalloc(w->snap.n + 1, sizeof *rects), *closes = sk_xcalloc(w->snap.n + 1, sizeof *closes);
    tab_layout(w, f->w, rects, closes);
    float tbase = baseline_in(g, 0, size, (float)ty, (float)px(w, 23));
    for (size_t i = 0; i < w->snap.n; i++) {
        snap_tab *t = &w->snap.tabs[i];
        sk_rect r = rects[i], c = closes[i];
        bool active = t->id == w->snap.active, hover = (int)i == w->hover_tab;
        if (active || hover)
            sk_fill_rect(f, r, active ? SK_RGB(0x1e, 0x1e, 0x1e) : SK_RGB(0x16, 0x16, 0x16), SK_BLEND_SRC, NULL);
        sk_fill_rect(f, sk_rect_make(r.x + r.w - line, r.y, line, r.h), SK_RGB(0x26, 0x26, 0x26), SK_BLEND_SRC,
                     NULL);
        sk_color text = active ? SK_RGB(0xff, 0xff, 0xff) : SK_RGB(0x9a, 0x9a, 0x9a);
        float tx = (float)(r.x + px(w, 8));
        if (!active && t->unseen)
            fill_circle(f, tx + (float)(MARK_W * s / 2), (float)(r.y + r.h / 2.0), (float)(MARK_W * s / 2),
                        SK_RGB(0x6f, 0xa8, 0xdc));
        tx += (float)((MARK_W + 6) * s);
        float room = (float)(c.x - px(w, 6)) - tx;
        float fw = t->folder ? sk_measure_text(g->mono, 0, size, t->folder, strlen(t->folder)) : 0;
        float name_w = draw_fit(g, f, 0, size, tx, tbase,
                                t->folder ? SK_MAX(room - fw - (float)(6 * s), 0) : room, t->name, text);
        if (t->folder)
            draw_fit(g, f, 0, size, tx + name_w + (float)(6 * s), tbase, room - name_w - (float)(6 * s), t->folder,
                     SK_RGB(0x77, 0x77, 0x77));
        bool on_close = hover && w->hover_close;
        if (on_close)
            sk_fill_rect(f, c, SK_RGB(0x3a, 0x3a, 0x3a), SK_BLEND_SRC, NULL);
        const char *times = "\xc3\x97";
        float xw = sk_measure_text(g->mono, 0, size, times, strlen(times));
        sk_draw_text(f, g->cache, g->mono, 0, size, (float)c.x + ((float)c.w - xw) / 2, tbase, times, strlen(times),
                     on_close ? SK_RGB(0xff, 0xff, 0xff) : SK_RGB(0x77, 0x77, 0x77), NULL);
    }
    sk_rect n = rects[w->snap.n];
    if (w->hover_tab == (int)w->snap.n)
        sk_fill_rect(f, n, SK_RGB(0x16, 0x16, 0x16), SK_BLEND_SRC, NULL);
    float pw = sk_measure_text(g->mono, 0, size, "+", 1);
    sk_draw_text(f, g->cache, g->mono, 0, size, (float)n.x + ((float)n.w - pw) / 2, tbase, "+", 1,
                 SK_RGB(0x9a, 0x9a, 0x9a), NULL);
    free(rects);
    free(closes);
}

static void on_blink(void *ud)
{
    gwin *w = ud;
    w->cursor_on = !w->cursor_on;
    redraw(w);
}

static void on_sync_check(void *ud)
{
    gwin *w = ud;
    w->sync_timer = 0;
    sk_window_request_frame(w->win);
}

/* The dashboard's pane at the terminal's rect, in the terminal theme's
 * colours. */
static void draw_dash(gwin *w, sk_image *f, sk_term *t)
{
    sk_term_palette p;
    sk_term_lock(t);
    sk_term_palette_of(t, &p);
    sk_term_unlock(t);
    sk_rect a = term_area(w);
    sk_dashview_style st = {w->g->mono, w->g->cache, sk_window_scale(w->win), (sk_color)(0xff000000u | p.fg),
                            (sk_color)(0xff000000u | p.bg)};
    sk_fill_rect(f, a, st.bg, SK_BLEND_SRC, NULL);
    sk_dashview *v = active_dview(w);
    if (v)
        sk_dashview_draw(v, f, a, &st);
}

static void draw(gwin *w)
{
    gui *g = w->g;
    sk_image *f = sk_window_frame(w->win);
    sk_rect dirty[2];
    size_t nd = 0;
    /* The drop-down and its shadow lie over what is below: while it is
     * open, each frame is drawn whole and it goes on top. */
    bool menu = sk_menu_open(w->menu);
    if (menu)
        w->all = true;
    if (w->all) {
        draw_chrome(w, f);
        /* The stage is the page's black around the terminal. */
        sk_fill_rect(f, stage_area(w), SK_RGB(0, 0, 0), SK_BLEND_SRC, NULL);
    }
    sk_term *t = active_term(w);
    snap_tab *at = active_tab(w);
    if (t && at->kind == SK_TAB_DASHBOARD) {
        /* Drawn whole each time: every view and hover asks for all. */
        if (w->all)
            draw_dash(w, f, t);
        t = NULL;
    }
    if (t) {
        sk_rect a = term_area(w);
        sk_image area = sk_image_view(f, a);
        sk_term_lock(t);
        bool hold = false;
        if (sk_term_synchronized(t)) {
            /* xterm.js ends synchronized output held past a second. */
            uint64_t now = sk_now_ms();
            if (!w->sync_since)
                w->sync_since = now;
            if (now - w->sync_since >= SYNC_HOLD_MS)
                sk_term_end_sync(t);
            else
                hold = true;
            if (hold && !w->sync_timer)
                w->sync_timer = sk_ui_timer(SYNC_HOLD_MS - (now - w->sync_since) + 1, 0, on_sync_check, w);
        } else {
            w->sync_since = 0;
        }
        if (!hold) {
            if (w->all) {
                sk_term_palette p;
                sk_term_palette_of(t, &p);
                sk_fill_rect(&area, sk_image_bounds(&area), (sk_color)(0xff000000u | p.bg), SK_BLEND_SRC, NULL);
            }
            sk_termview_state st = {
                .focused = w->focused,
                .cursor_on = w->cursor_on,
                .link = w->hover_link,
                .link_row0 = w->link_r0,
                .link_col0 = w->link_c0,
                .link_row1 = w->link_r1,
                .link_col1 = w->link_c1,
            };
            sk_rect r = sk_termview_draw(w->view, t, &area, &st, w->all);
            if (!sk_rect_empty(r))
                dirty[nd++] = sk_rect_make(r.x + a.x, r.y + a.y, r.w, r.h);
        }
        sk_term_cursor c;
        sk_term_cursor_of(t, &c);
        int cw, ch;
        sk_termview_cell(w->view, &cw, &ch);
        int cx = SK_MIN(c.x, w->ui_cols - 1) * cw, cy = c.y * ch;
        /* An input method's composition, at the cursor over the cells,
         * underlined, as xterm.js's composition view shows it. */
        if (!hold && w->preedit && *w->preedit && c.y >= 0 && c.y < w->ui_rows) {
            sk_term_palette p;
            sk_term_palette_of(t, &p);
            float px = (float)(w->style.font_px * w->style.scale);
            float tw = sk_measure_text(g->mono, 0, px, w->preedit, strlen(w->preedit));
            sk_rect box = sk_rect_make(cx, cy, (int)ceilf(tw), ch);
            sk_fill_rect(&area, box, (sk_color)(0xff000000u | p.bg), SK_BLEND_SRC, NULL);
            sk_draw_text(&area, g->cache, g->mono, 0, px, (float)cx, (float)cy + (float)ch * 0.78f, w->preedit,
                         strlen(w->preedit), (sk_color)(0xff000000u | p.fg), &box);
            int thick = SK_MAX(1, (int)lround(w->style.scale));
            sk_fill_rect(&area, sk_rect_make(cx, cy + ch - thick, box.w, thick), (sk_color)(0xff000000u | p.fg),
                         SK_BLEND_SRC, NULL);
            dirty[nd++] = sk_rect_make(box.x + a.x, box.y + a.y, box.w, box.h);
        }
        /* The candidate window goes beside the cursor. */
        double s = sk_window_scale(w->win);
        int ix = (int)lround(cx / s), iy = (int)lround((cy + a.y) / s);
        if (ix != w->ime_x || iy != w->ime_y) {
            w->ime_x = ix;
            w->ime_y = iy;
            sk_window_set_ime_rect(w->win, ix, iy, (int)lround(cw / s), (int)lround(ch / s));
        }
        if (c.blink && !w->blink_timer) {
            w->blink_timer = sk_ui_timer(BLINK_MS, BLINK_MS, on_blink, w);
        } else if (!c.blink && w->blink_timer) {
            sk_ui_cancel(w->blink_timer);
            w->blink_timer = 0;
            w->cursor_on = true;
        }
        sk_term_unlock(t);
    }
    if (menu) {
        sk_menu_style st = menu_style(w);
        sk_menu_draw(w->menu, f, &st);
    }
    if (w->all)
        sk_window_present(w->win, NULL, 0);
    else if (nd)
        sk_window_present(w->win, dirty, nd);
    w->all = false;
}

/* ---- Input ---- */

static void send_active(gwin *w, const sk_buf *b)
{
    snap_tab *t = active_tab(w);
    if (t && b->len)
        post_input(w->g, t->id, b->ptr, b->len);
}

static void copy_selection(gwin *w, bool primary)
{
    sk_term *t = active_term(w);
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
    gwin *w = ud;
    sk_term *t = active_term(w);
    if (!t || !utf8 || !*utf8)
        return;
    sk_term_input_modes m;
    sk_term_input_modes_of(t, &m);
    sk_buf out = {0};
    sk_term_paste(&m, utf8, strlen(utf8), &out);
    sk_term_scroll_to_bottom(t);
    send_active(w, &out);
    sk_buf_free(&out);
}

/* The app's own chords, which the CLI never sees: decision 0023's on
 * Linux, with Cmd for Ctrl+Shift on macOS. */
static bool chord(gwin *w, const sk_key_event *k)
{
    gui *g = w->g;
    uint32_t m = k->mods & (SK_MOD_SHIFT | SK_MOD_CTRL | SK_MOD_ALT | SK_MOD_SUPER);
#ifdef __APPLE__
    const uint32_t app = SK_MOD_SUPER;
#else
    const uint32_t app = SK_MOD_CTRL | SK_MOD_SHIFT;
#endif
    snap_tab *t = active_tab(w);
    int win = w->snap.id;
    if (k->key == SK_KEY_TAB && (m == SK_MOD_CTRL || m == (SK_MOD_CTRL | SK_MOD_SHIFT))) {
        post_tab(g, do_step, m & SK_MOD_SHIFT ? -1 : 1, win);
        return true;
    }
    if (k->key == SK_KEY_INSERT && m == SK_MOD_SHIFT) {
        sk_clipboard_get(false, on_paste, w);
        return true;
    }
#ifdef __APPLE__
    if (m == (SK_MOD_SUPER | SK_MOD_SHIFT) && (k->base == ']' || k->base == '[')) {
        post_tab(g, do_step, k->base == ']' ? 1 : -1, win);
        return true;
    }
#endif
    if (m != app)
        return false;
    switch (k->base) {
    /* A held chord opens or closes one tab, as tabs.js ignores the
     * repeats of all but Tab. */
    case 't':
        if (!k->repeat)
            post_tab(g, do_new_tab, 0, win);
        return true;
    case 'w':
        if (t && !k->repeat)
            post_tab(g, do_close, t->id, win);
        return true;
    case 'c':
        copy_selection(w, false);
        return true;
    case 'v':
        sk_clipboard_get(false, on_paste, w);
        return true;
    default:
        return false;
    }
}

static void on_key(gwin *w, const sk_key_event *k)
{
    /* An open drop-down takes every key: Left and Right step through
     * the menus, Escape closes it, the rest are dropped (decision 0022). */
    int show;
    if (sk_menu_key(w->menu, k, &show)) {
        if (show >= 0)
            menu_ask(w, show);
        menu_changed(w);
        return;
    }
    if (chord(w, k))
        return;
    sk_term *t = active_term(w);
    if (!t)
        return;
    sk_term_input_modes m;
    sk_term_input_modes_of(t, &m);
    sk_buf out = {0};
    int rows = w->ui_rows;
    sk_term_key_result r = sk_term_key(k, &m, &out);
    switch (r) {
    case SK_TERM_KEY_SEND:
        /* xterm.js's scrollOnUserInput, and a selection ends. */
        sk_term_scroll_to_bottom(t);
        sk_term_select_clear(t);
        send_active(w, &out);
        redraw(w);
        break;
    case SK_TERM_KEY_PAGE_UP:
    case SK_TERM_KEY_PAGE_DOWN:
        sk_term_scroll(t, r == SK_TERM_KEY_PAGE_UP ? -(rows - 1) : rows - 1);
        redraw(w);
        break;
    default:
        break;
    }
    sk_buf_free(&out);
}

/* The pointer in points from the grid's top-left. */
static void grid_point(gwin *w, double x, double y, double *gx, double *gy)
{
    *gx = x - TERM_INSET;
    *gy = y - top_points(w) - TERM_INSET;
}

static void grid_of(gwin *w, sk_term_grid *grid)
{
    int cw, ch;
    sk_termview_cell(w->view, &cw, &ch);
    double s = sk_window_scale(w->win);
    grid->cols = w->ui_cols;
    grid->rows = w->ui_rows;
    grid->cell_w = cw / s;
    grid->cell_h = ch / s;
}

static void hit(gwin *w, sk_term *t, double gx, double gy, int *col, int *row, int *edge)
{
    double s = sk_window_scale(w->win);
    sk_termview_hit(w->view, t, (int)floor(gx * s), (int)floor(gy * s), col, row, edge);
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

static void open_uri(gui *g, const char *uri)
{
    if (!uri)
        return;
    size_t n = strlen(uri);
    uri_msg *m = sk_xmalloc(sizeof *m + n + 1);
    m->g = g;
    memcpy(m->uri, uri, n + 1);
    sk_loop_post(g->loop, do_open, m);
}

/* The web links addon reads the buffer line by line through these, the
 * line of the last read kept. */
typedef struct {
    sk_term *t;
    sk_term_cell *cells;
    int cols, line;
    bool wrapped, have;
} line_src;

static bool src_load(line_src *s, int y)
{
    if (s->line != y) {
        int cols = s->cols;
        s->have = sk_term_line(s->t, y, s->cells, &cols, &s->wrapped);
        s->line = y;
    }
    return s->have;
}

static bool src_row(void *ud, int y, int *cols, bool *wrapped)
{
    line_src *s = ud;
    if (!src_load(s, y))
        return false;
    *cols = s->cols;
    *wrapped = s->wrapped;
    return true;
}

static int src_cell(void *ud, int y, int x, uint32_t *cp, int max, int *width)
{
    line_src *s = ud;
    if (!src_load(s, y) || x < 0 || x >= s->cols || max < 1)
        return 0;
    const sk_term_cell *c = &s->cells[x];
    *width = c->width;
    if (c->cp) {
        cp[0] = c->cp;
        return 1;
    }
    int n = (int)SK_MIN((uint32_t)max, c->ncomb);
    for (int i = 0; i < n; i++)
        cp[i] = c->comb[i];
    return n;
}

/* The web link (addon-web-links) under viewport cell col, row into the
 * hover state, or none. True when that changed. */
static bool detect_link(gwin *w, sk_term *t, int col, int row)
{
    int r0 = -1, c0 = 0, r1 = 0, c1 = 0;
    char *uri = NULL;
    sk_term_lock(t);
    int cols, rows, top, base, lines;
    sk_term_size(t, &cols, &rows);
    sk_term_viewport(t, &top, &base, &lines);
    line_src s = {t, sk_xcalloc((size_t)cols, sizeof *s.cells), cols, -1, false, false};
    sk_term_link_src src = {&s, src_row, src_cell};
    sk_term_links found = {0};
    int y = top + row;
    sk_term_links_find(&src, y, &found);
    for (size_t i = 0; i < found.len && !uri; i++) {
        const sk_term_link *l = &found.ptr[i];
        bool after = y > l->y0 || (y == l->y0 && col >= l->x0);
        bool before = y < l->y1 || (y == l->y1 && col < l->x1);
        if (after && before) {
            r0 = l->y0 - top;
            c0 = l->x0;
            r1 = l->y1 - top;
            c1 = l->x1;
            uri = sk_xstrdup(l->uri);
        }
    }
    sk_term_links_free(&found);
    free(s.cells);
    sk_term_unlock(t);
    bool changed = r0 != w->link_r0 || c0 != w->link_c0 || r1 != w->link_r1 || c1 != w->link_c1;
    free(w->link_uri);
    w->link_uri = uri;
    w->link_r0 = r0;
    w->link_c0 = c0;
    w->link_r1 = r1;
    w->link_c1 = c1;
    return changed;
}

/* Which tab of the strip x falls on, its close box, or the new-tab box
 * (index n). -1 for none. */
static int strip_hit(gwin *w, double ptx, double pty, bool *on_close)
{
    *on_close = false;
    if (!tabs_shown(w) || pty < IDENTITY_H || pty >= IDENTITY_H + TABS_H)
        return -1;
    double s = sk_window_scale(w->win);
    int x = (int)floor(ptx * s);
    sk_image *f = sk_window_frame(w->win);
    sk_rect *rects = sk_xcalloc(w->snap.n + 1, sizeof *rects), *closes = sk_xcalloc(w->snap.n + 1, sizeof *closes);
    tab_layout(w, f->w, rects, closes);
    int hit = -1;
    for (size_t i = 0; i <= w->snap.n && hit < 0; i++) {
        if (x < rects[i].x || x >= rects[i].x + rects[i].w)
            continue;
        *on_close = i < w->snap.n && x >= closes[i].x && x < closes[i].x + closes[i].w;
        hit = (int)i;
    }
    free(rects);
    free(closes);
    return hit;
}

/* The tab strip's hover follows the pointer; true when it changed. */
static bool track_hover(gwin *w, double x, double y)
{
    bool on_close = false;
    int i = strip_hit(w, x, y, &on_close);
    if (i == w->hover_tab && on_close == w->hover_close)
        return false;
    w->hover_tab = i;
    w->hover_close = on_close;
    return true;
}

/* The strip's press, drag and release: a press picks a tab, a middle
 * click or its close box closes it, + opens a tab, a drag moves it, and
 * a release outside the window pops it out (decision 0023). */
static bool strip_mouse(gwin *w, const sk_event *ev)
{
    gui *g = w->g;
    int win = w->snap.id;
    bool on_close;
    if (ev->type == SK_EV_MOUSE_DOWN) {
        int i = strip_hit(w, ev->mouse.x, ev->mouse.y, &on_close);
        if (i < 0)
            return true;
        if ((size_t)i == w->snap.n) {
            post_tab(g, do_new_tab, 0, win);
        } else if (ev->mouse.button == 2 || (on_close && ev->mouse.button == 1)) {
            post_tab(g, do_close, w->snap.tabs[i].id, win);
        } else if (ev->mouse.button == 1) {
            post_tab(g, do_activate, w->snap.tabs[i].id, win);
            w->drag_tab = w->snap.tabs[i].id;
            w->drag_x0 = ev->mouse.x;
            w->dragging = false;
        }
        return true;
    }
    if (!w->drag_tab)
        return false;
    if (ev->type == SK_EV_MOUSE_MOVE) {
        if (!w->dragging && fabs(ev->mouse.x - w->drag_x0) < DRAG_PX)
            return true;
        w->dragging = true;
        /* tabs.js: the tab goes before the first other tab whose middle
         * lies right of the pointer, wherever the pointer is. */
        sk_image *f = sk_window_frame(w->win);
        sk_rect *rects = sk_xcalloc(w->snap.n + 1, sizeof *rects), *closes = sk_xcalloc(w->snap.n + 1, sizeof *closes);
        tab_layout(w, f->w, rects, closes);
        double x = ev->mouse.x * sk_window_scale(w->win);
        long at = 0, now = -1;
        for (size_t i = 0; i < w->snap.n; i++) {
            if (w->snap.tabs[i].id == w->drag_tab) {
                now = (long)i;
                continue;
            }
            if (x >= rects[i].x + rects[i].w / 2.0)
                at++;
        }
        free(rects);
        free(closes);
        if (now >= 0 && at != now)
            post_tab(g, do_move, w->drag_tab, (int)at);
        return true;
    }
    if (ev->type == SK_EV_MOUSE_UP) {
        int ww, wh;
        sk_window_size(w->win, &ww, &wh);
        bool outside = ev->mouse.x < 0 || ev->mouse.y < 0 || ev->mouse.x >= ww || ev->mouse.y >= wh;
        if (w->dragging && outside)
            post_tab(g, do_pop, w->drag_tab, win);
        w->drag_tab = 0;
        w->dragging = false;
        return true;
    }
    return false;
}

static void on_mouse(gwin *w, const sk_event *ev)
{
    gui *g = w->g;
    /* The drop-down is above everything: what lands on it is its own,
     * and a press elsewhere closes it and goes on to what is there. */
    if (sk_menu_open(w->menu)) {
        if (ev->type == SK_EV_MOUSE_MOVE && sk_menu_mouse_move(w->menu, ev->mouse.x, ev->mouse.y))
            redraw(w);
        if (ev->type == SK_EV_MOUSE_DOWN && ev->mouse.button == 1) {
            sk_menu_press p = sk_menu_mouse_down(w->menu, ev->mouse.x, ev->mouse.y);
            menu_press(w, &p);
            if (p.kind != SK_MENU_PRESS_OUTSIDE)
                return;
        } else if (sk_menu_contains(w->menu, ev->mouse.x, ev->mouse.y)) {
            return;
        } else if (ev->type == SK_EV_MOUSE_DOWN) {
            sk_menu_close(w->menu);
            menu_changed(w);
        }
    }
    sk_term *t = active_term(w);
    double gx, gy;
    grid_point(w, ev->mouse.x, ev->mouse.y, &gx, &gy);
    /* The strips above the stage are the app's: a press there is never
     * the terminal's. */
    bool chrome = ev->mouse.y < top_points(w);
    if (ev->type == SK_EV_MOUSE_MOVE && !w->selecting && track_hover(w, ev->mouse.x, ev->mouse.y))
        redraw(w);
    if (w->drag_tab || (ev->type == SK_EV_MOUSE_DOWN && chrome && !w->selecting)) {
        if (strip_mouse(w, ev))
            return;
    }
    if (!t || (!chrome && dash_mouse(w, ev)))
        return;
    sk_term_input_modes m;
    sk_term_input_modes_of(t, &m);
    sk_term_grid grid;
    grid_of(w, &grid);
    sk_buf out = {0};
    int col, row, edge;
    hit(w, t, gx, gy, &col, &row, &edge);
    bool changed = false;
#ifdef __APPLE__
    const uint32_t opener = SK_MOD_SUPER;
#else
    const uint32_t opener = SK_MOD_CTRL;
#endif
    switch (ev->type) {
    case SK_EV_MOUSE_DOWN:
        if (sk_term_mouse_down(&w->mouse, &m, &grid, gx, gy, ev->mouse.button, ev->mouse.mods, &out))
            break;
        if (ev->mouse.button != 1)
            break;
        if (ev->mouse.mods & opener) {
            /* An OSC 8 link first, then a web link in the text. */
            uint32_t link = link_at(t, col, row);
            if (link) {
                sk_term_lock(t);
                open_uri(g, sk_term_link_uri(t, link));
                sk_term_unlock(t);
                break;
            }
            detect_link(w, t, col, row);
            if (w->link_uri) {
                open_uri(g, w->link_uri);
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
        w->selecting = true;
        changed = true;
        break;
    case SK_EV_MOUSE_MOVE: {
        if (w->selecting) {
            if (gy < 0)
                sk_term_select_drag_scroll(t, -1);
            else if (gy * sk_window_scale(w->win) >= term_area(w).h)
                sk_term_select_drag_scroll(t, 1);
            else
                sk_term_select_move(t, edge, row);
            changed = true;
            break;
        }
        sk_term_mouse_move(&w->mouse, &m, &grid, gx, gy, gy >= 0, ev->mouse.mods, &out);
        uint32_t link = gy >= 0 ? link_at(t, col, row) : 0;
        bool moved = link != w->hover_link;
        w->hover_link = link;
        if (link || gy < 0) {
            moved |= w->link_r0 >= 0;
            free(w->link_uri);
            w->link_uri = NULL;
            w->link_r0 = -1;
        } else {
            moved |= detect_link(w, t, col, row);
        }
        if (moved) {
            sk_window_set_cursor(w->win, link || w->link_uri ? SK_CURSOR_POINTER : SK_CURSOR_IBEAM);
            changed = true;
        }
        break;
    }
    case SK_EV_MOUSE_UP:
        sk_term_mouse_up(&w->mouse, &m, &grid, gx, gy, ev->mouse.button, ev->mouse.mods, &out);
        if (w->selecting && ev->mouse.button == 1) {
            w->selecting = false;
            /* The selection is X11's PRIMARY as it is made, as the app's
             * onSelectionChange wrote it. */
            copy_selection(w, true);
        }
        break;
    default:
        break;
    }
    send_active(w, &out);
    sk_buf_free(&out);
    if (changed)
        redraw(w);
}

static void on_scroll(gwin *w, const sk_event *ev)
{
    /* A positive dy scrolls back, up; the menu takes lines down. */
    if (sk_menu_open(w->menu) && sk_menu_scroll(w->menu, ev->scroll.x, ev->scroll.y,
                                                -(ev->scroll.precise ? ev->scroll.dy / 20.8 : ev->scroll.dy))) {
        redraw(w);
        return;
    }
    /* The dashboard's pane scrolls by points, three lines of 18 to a
     * notch. */
    snap_tab *at = active_tab(w);
    if (at && at->kind == SK_TAB_DASHBOARD) {
        sk_dashview *dv = active_dview(w);
        if (dv && sk_dashview_scroll(dv, -(ev->scroll.precise ? ev->scroll.dy : ev->scroll.dy * 54)))
            redraw(w);
        return;
    }
    sk_term *t = active_term(w);
    if (!t)
        return;
    double gx, gy;
    grid_point(w, ev->scroll.x, ev->scroll.y, &gx, &gy);
    sk_term_input_modes m;
    sk_term_input_modes_of(t, &m);
    sk_term_grid grid;
    grid_of(w, &grid);
    sk_buf out = {0};
    if (sk_term_mouse_wheel(&w->mouse, &m, &grid, gx, gy, ev->scroll.dy, ev->scroll.precise, ev->scroll.mods,
                            &out)) {
        send_active(w, &out);
    } else {
        /* The view scrolls: by lines, or by points a cell's height to a
         * line, the rest kept for the next event. */
        w->wheel += ev->scroll.precise ? ev->scroll.dy / grid.cell_h : ev->scroll.dy;
        int lines = (int)w->wheel;
        w->wheel -= lines;
        if (lines) {
            sk_term_scroll(t, -lines);
            redraw(w);
        }
    }
    sk_buf_free(&out);
}

static void on_event(void *ud, const sk_event *ev)
{
    gui *g = ud;
    gwin *w = ev->win ? sk_window_ud(ev->win) : NULL;
    if (!w)
        return;
    switch (ev->type) {
    case SK_EV_CLOSE:
        /* As closing a window does (decision 0016): its tabs hang up,
         * and the app ends with the last tab. */
        post_tab(g, do_close_win, 0, w->snap.id);
        break;
    case SK_EV_RESIZE: {
        double s = sk_window_scale(w->win);
        if (s != w->style.scale) {
            w->style.scale = s;
            sk_termview_set_style(w->view, &w->style);
        }
        fit(w);
        redraw(w);
        break;
    }
    case SK_EV_FRAME:
        draw(w);
        break;
    case SK_EV_FOCUS:
        w->focused = ev->focused;
        focus_term(w);
        if (ev->focused)
            post_tab(g, do_touch, 0, w->snap.id);
        redraw(w);
        break;
    case SK_EV_KEY_DOWN:
        on_key(w, &ev->key);
        break;
    case SK_EV_MOUSE_DOWN:
    case SK_EV_MOUSE_UP:
    case SK_EV_MOUSE_MOVE:
        on_mouse(w, ev);
        break;
    case SK_EV_MOUSE_LEAVE:
        if (w->hover_link || w->link_r0 >= 0 || w->hover_tab >= 0) {
            clear_hover(w);
            w->hover_tab = -1;
            redraw(w);
        }
        break;
    case SK_EV_SCROLL:
        on_scroll(w, ev);
        break;
    case SK_EV_TEXT: {
        /* What an input method commits is typed, as xterm.js sends a
         * composition's end. */
        snap_tab *at = active_tab(w);
        if (sk_menu_open(w->menu))
            break;
        if (at && ev->text && *ev->text) {
            sk_term_scroll_to_bottom(at->st->term);
            sk_term_select_clear(at->st->term);
            post_input(g, at->id, ev->text, strlen(ev->text));
        }
        break;
    }
    case SK_EV_PREEDIT:
        free(w->preedit);
        w->preedit = ev->text && *ev->text ? sk_xstrdup(ev->text) : NULL;
        redraw(w);
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
    sk_buf core; /* what the core added: the tabs' members, comma first */
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

static void window_json(sk_jw *jw, gwin *w)
{
    int pw, ph;
    sk_window_size(w->win, &pw, &ph);
    sk_jw_object(jw);
    sk_jw_key(jw, SK_STR("id"));
    sk_jw_int(jw, w->snap.id);
    sk_jw_key(jw, SK_STR("w"));
    sk_jw_int(jw, pw);
    sk_jw_key(jw, SK_STR("h"));
    sk_jw_int(jw, ph);
    sk_jw_key(jw, SK_STR("scale"));
    sk_jw_number(jw, sk_window_scale(w->win));
    sk_jw_key(jw, SK_STR("cols"));
    sk_jw_int(jw, w->ui_cols);
    sk_jw_key(jw, SK_STR("rows"));
    sk_jw_int(jw, w->ui_rows);
    sk_jw_key(jw, SK_STR("strip"));
    sk_jw_bool(jw, tabs_shown(w));
    sk_jw_key(jw, SK_STR("active"));
    sk_jw_int(jw, w->snap.active);
    sk_jw_key(jw, SK_STR("tabs"));
    sk_jw_array(jw);
    for (size_t i = 0; i < w->snap.n; i++)
        sk_jw_int(jw, w->snap.tabs[i].id);
    sk_jw_array_end(jw);
    /* Each tab's left and width in the strip, in points. */
    sk_jw_key(jw, SK_STR("tab_rects"));
    sk_jw_array(jw);
    if (tabs_shown(w)) {
        sk_rect *rects = sk_xcalloc(w->snap.n + 1, sizeof *rects), *closes = sk_xcalloc(w->snap.n + 1, sizeof *closes);
        tab_layout(w, sk_window_frame(w->win)->w, rects, closes);
        double s = sk_window_scale(w->win);
        for (size_t i = 0; i < w->snap.n; i++) {
            sk_jw_array(jw);
            sk_jw_number(jw, rects[i].x / s);
            sk_jw_number(jw, rects[i].w / s);
            sk_jw_array_end(jw);
        }
        free(rects);
        free(closes);
    }
    sk_jw_array_end(jw);
    sk_jw_object_end(jw);
}

/* The window a request names with "win", else the first. */
static gwin *target(gui *g, const sk_json *msg)
{
    double id;
    if (sk_json_as_number(sk_json_get(msg, "win"), &id))
        return gwin_of(g, (int)id);
    gwin *first = NULL;
    for (size_t i = 0; i < g->wins.len; i++)
        if (!first || g->wins.ptr[i]->snap.id < first->snap.id)
            first = g->wins.ptr[i];
    return first;
}

static void probe(gui *g, gwin *w, ctl_msg *m)
{
    sk_buf b = {0};
    sk_jw jw;
    sk_jw_init(&jw, &b, 0);
    sk_jw_object(&jw);
    sk_jw_key(&jw, SK_STR("ok"));
    sk_jw_bool(&jw, true);
    /* The core's members, comma first, after the writer's last value. */
    sk_buf_append(&b, m->core.ptr, m->core.len);
    sk_jw_key(&jw, SK_STR("windows"));
    sk_jw_array(&jw);
    for (size_t i = 0; i < g->wins.len; i++)
        window_json(&jw, g->wins.ptr[i]);
    sk_jw_array_end(&jw);
    sk_jw_key(&jw, SK_STR("window"));
    if (w)
        window_json(&jw, w);
    else
        sk_jw_null(&jw);
    sk_jw_key(&jw, SK_STR("menu"));
    if (w)
        sk_menu_state(w->menu, &jw);
    else
        sk_jw_null(&jw);
    /* The dashboard's pane, when it is the active tab's. */
    sk_jw_key(&jw, SK_STR("dashboard"));
    sk_dashview *dv = w ? active_dview(w) : NULL;
    if (dv)
        sk_dashview_state(dv, &jw);
    else
        sk_jw_null(&jw);
    /* The web link under the pointer, as the addon found it. */
    sk_jw_key(&jw, SK_STR("link"));
    if (w && w->link_uri)
        sk_jw_string(&jw, sk_str_c(w->link_uri));
    else
        sk_jw_null(&jw);
    sk_term *t = w ? active_term(w) : NULL;
    sk_jw_key(&jw, SK_STR("screen"));
    if (t) {
        sk_term_lock(t);
        char *text = sk_term_screen_text(t);
        sk_jw_string(&jw, sk_str_c(text));
        free(text);
        char *sel = sk_term_has_selection(t) ? sk_term_selection_text(t) : NULL;
        sk_jw_key(&jw, SK_STR("selection"));
        if (sel)
            sk_jw_string(&jw, sk_str_c(sel));
        else
            sk_jw_null(&jw);
        free(sel);
        sk_term_cursor c;
        sk_term_cursor_of(t, &c);
        sk_jw_key(&jw, SK_STR("cursor"));
        sk_jw_array(&jw);
        sk_jw_int(&jw, c.x);
        sk_jw_int(&jw, c.y);
        sk_jw_array_end(&jw);
        sk_term_unlock(t);
    } else {
        sk_jw_null(&jw);
    }
    sk_jw_object_end(&jw);
    sk_control_answer(m->req, b.ptr, b.len);
    sk_buf_free(&b);
}

/* On the UI thread: each request goes the way the platform's events go,
 * to the window it names. */
static void ui_control(void *ud)
{
    ctl_msg *m = ud;
    gui *g = m->g;
    sk_arena a;
    sk_arena_init(&a, 0);
    const sk_json *msg = sk_json_parse(&a, m->msg.ptr, m->msg.len, NULL);
    sk_str op = {0};
    sk_json_as_string(sk_json_get(msg, "op"), &op);
    gwin *w = target(g, msg);
    sk_event ev;
    memset(&ev, 0, sizeof ev);
    ev.win = w ? w->win : NULL;
    const char *why = NULL;
    if (sk_str_eq_c(op, "probe")) {
        probe(g, w, m);
        m->req = NULL;
    } else if (!w) {
        why = "no such window";
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
    } else if (sk_str_eq_c(op, "menu")) {
        /* Until a skin places its button, the check gives the button's
         * rect, and the menu to show, or none to close it. */
        const sk_json *b = sk_json_get(msg, "button");
        for (size_t i = 0; i < 4 && b && b->type == SK_JSON_ARRAY && i < b->array.len; i++)
            sk_json_as_number(sk_json_at(b, i), &w->button[i]);
        sk_str show = {0};
        sk_json_as_string(sk_json_get(msg, "show"), &show);
        int menu = -1;
        for (int i = 0; i < SK_MENU_COUNT; i++)
            if (sk_str_eq_c(show, sk_menu_titles[i]))
                menu = i;
        if (menu >= 0) {
            menu_ask(w, menu);
        } else {
            sk_menu_close(w->menu);
            menu_changed(w);
        }
    } else if (sk_str_eq_c(op, "shot")) {
        sk_str path = {0};
        sk_json_as_string(sk_json_get(msg, "path"), &path);
        if (!shot(w, path.ptr))
            why = "cannot read the window back or write the image";
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
    sk_jw jw;
    sk_jw_init(&jw, &m->msg, 0);
    sk_jw_value(&jw, msg);
    if (sk_str_eq_c(op, "probe")) {
        /* The tabs' members of the probe, in place of skinnedProbe's. */
        sk_buf obj = {0};
        sk_jw_init(&jw, &obj, 0);
        sk_jw_object(&jw);
        sk_tab *active = sk_tabs_active(g->tabs);
        sk_jw_key(&jw, SK_STR("active"));
        sk_jw_int(&jw, active ? active->id : 0);
        sk_jw_key(&jw, SK_STR("tabs"));
        sk_jw_array(&jw);
        for (size_t i = 0; i < sk_tabs_count(g->tabs); i++) {
            sk_tab *t = sk_tabs_at(g->tabs, i);
            sk_session *s = sk_clitab_session(t);
            sk_jw_object(&jw);
            sk_jw_key(&jw, SK_STR("id"));
            sk_jw_int(&jw, t->id);
            sk_jw_key(&jw, SK_STR("win"));
            sk_jw_int(&jw, t->win);
            sk_jw_key(&jw, SK_STR("kind"));
            sk_jw_string(&jw, sk_str_c(t->kind == SK_TAB_MONITOR     ? "monitor"
                                       : t->kind == SK_TAB_DASHBOARD ? "dashboard"
                                                                     : "cli"));
            sk_jw_key(&jw, SK_STR("pid"));
            sk_jw_int(&jw, s ? sk_session_pid(s) : 0);
            sk_jw_key(&jw, SK_STR("active"));
            sk_jw_bool(&jw, t == active);
            sk_jw_key(&jw, SK_STR("unseen"));
            sk_jw_bool(&jw, t->unseen);
            sk_jw_key(&jw, SK_STR("label"));
            sk_jw_string(&jw, sk_str_c(t->label));
            sk_jw_key(&jw, SK_STR("name"));
            sk_jw_string(&jw, sk_str_c(sk_tab_name(t)));
            sk_jw_key(&jw, SK_STR("cwd"));
            sk_jw_string(&jw, sk_str_c(t->cwd));
            sk_jw_key(&jw, SK_STR("argv"));
            sk_jw_array(&jw);
            for (size_t k = 0; k < t->cmd.len; k++)
                sk_jw_string(&jw, sk_str_c(t->cmd.ptr[k]));
            sk_jw_array_end(&jw);
            sk_jw_object_end(&jw);
        }
        sk_jw_array_end(&jw);
        sk_jw_object_end(&jw);
        /* The object's members alone, comma first, for the UI to wrap. */
        sk_buf_append_c(&m->core, ",");
        if (obj.len > 2)
            sk_buf_append(&m->core, obj.ptr + 1, obj.len - 2);
        sk_buf_free(&obj);
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
    atomic_init(&g->frame_posted, false);
    atomic_init(&g->core_done, false);
    /* For the live checks: windows off the screen. */
    const char *hidden = sk_envp_get(env, "SKINNED_HIDDEN");
    g->hidden = hidden && *hidden == '1';
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
    /* The user's settings, read at launch (decision 0022), and where
     * the last skins and the profiles are kept. */
    sk_env_current(&g->paths);
    g->settings_file = sk_appsettings_file(&g->paths);
    if (g->settings_file)
        sk_appsettings_load(&g->settings, g->settings_file);
    g->font_px = font_of(&g->settings);
    g->last_dir = sk_last_dir(&g->paths);
    g->profiles_dir = sk_profiles_dir(&g->paths);

    /* The first window opens before its first tab, so the tab starts at
     * the window's size. It is the tabs' window 0. */
    gwin *first = gwin_new(g, 0);
    if (!first)
        goto out;
    sk_rect a = term_area(first);
    sk_termview_fit(first->view, a.w, a.h, &first->ui_cols, &first->ui_rows);
    cwin c0 = {0, first->ui_cols, first->ui_rows};
    sk_vec_push(&g->cwins, c0);

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
    /* The agent sessions the app did not start (decision 0027), and the
     * monitor tabs that watch them. */
    sk_foreign_config fc;
    memset(&fc, 0, sizeof fc);
    fc.tabs = g->tabs;
    fc.loop = g->loop;
    fc.env = env;
    fc.home = tc.home;
    fc.profiles_dir = g->profiles_dir;
    fc.log = log_line;
    fc.ud = g;
    g->foreign = sk_foreign_new(&fc);
    g->mon.loop = g->loop;
    g->mon.tabs = g->tabs;
    g->mon.home = tc.home;
    g->mon.watcher = mon_watcher;
    g->mon.strip = mon_strip;
    g->mon.pane = mon_pane;
    g->mon.log = log_line;
    g->mon.ud = g;
    /* The sessions dashboard (decision 0028) and its issue boards
     * (decision 0029). */
    sk_boards_opts bo;
    memset(&bo, 0, sizeof bo);
    bo.loop = g->loop;
    bo.log = log_line;
    bo.log_ud = g;
    g->boards = sk_boards_new(&bo);
    g->dash.loop = g->loop;
    g->dash.tabs = g->tabs;
    g->dash.list = dash_list;
    g->dash.watcher = mon_watcher;
    g->dash.board = dash_board;
    g->dash.strip = mon_strip;
    g->dash.send = dash_send;
    g->dash.log = log_line;
    g->dash.ud = g;
    sk_loop_signal(g->loop, SIGINT, on_stop, g);
    sk_loop_signal(g->loop, SIGTERM, on_stop, g);
    sk_loop_signal(g->loop, SIGHUP, on_stop, g);
    sk_loop_timer(g->loop, POLL_MS, POLL_MS, on_strip_poll, g);
    /* A check that asked for the socket and cannot have it would wait
     * on a window it cannot reach: the app ends instead. */
    sk_control *ctl = NULL;
    const char *control = sk_envp_get(env, "SKINNED_CONTROL");
    if (control && *control && !(ctl = sk_control_open(g->loop, control, on_control, g, &err))) {
        sk_tell("%s", err);
        free(err);
        sk_tabs_free(g->tabs);
        sk_boards_free(g->boards);
        sk_foreign_free(g->foreign);
        sk_loop_free(g->loop);
        goto out;
    }

    /* The first tab runs the command as typed (decision 0003). Until
     * the core thread starts, this thread is the core's. */
    sk_tab_spec spec;
    memset(&spec, 0, sizeof spec);
    spec.kind = SK_TAB_CLI;
    spec.cmd = o->cmd;
    spec.cwd = l.cwd;
    if (!sk_tabs_add(g->tabs, &spec, 0, &err)) {
        sk_tell("cannot open the tab: %s", err ? err : "unknown");
        free(err);
    } else {
        sk_log("window: %s, relay %s, %dx%d", l.file, l.relay ? l.relay : "(none)", c0.cols, c0.rows);
        post_snapshot(g);
        pthread_t core;
        if (pthread_create(&core, NULL, core_main, g) != 0) {
            sk_tell("cannot start the core thread: %s", strerror(errno));
        } else {
            sk_window_focus(first->win);
            if (!sk_ui_run())
                sk_log("the display went away");
            /* The windows' end is the tabs' too: the core ends with the
             * last of them. */
            if (!atomic_load(&g->core_done))
                sk_loop_post(g->loop, do_hangup_all, g);
            pthread_join(core, NULL);
            status = sk_tabs_done(g->tabs) ? sk_tabs_exit_status(g->tabs) : g->status;
        }
    }
    /* The tabs' watch agents are the foreign sessions', so the tabs go
     * first. */
    sk_tabs_free(g->tabs);
    sk_boards_free(g->boards);
    sk_foreign_free(g->foreign);
    for (size_t i = 0; i < g->ctabs.len; i++)
        st_unref(g->ctabs.ptr[i].st);
    sk_vec_free(&g->ctabs);
    sk_vec_free(&g->cwins);
    free(g->strip_key);
    for (size_t i = 0; i < g->problems.len; i++) {
        free(g->problems.ptr[i].title);
        free(g->problems.ptr[i].detail);
    }
    sk_vec_free(&g->problems);
    sk_control_close(ctl);
    sk_loop_free(g->loop);

out:
    while (g->wins.len)
        gwin_free(g->wins.ptr[--g->wins.len]);
    sk_vec_free(&g->wins);
    for (size_t i = 0; i < g->dviews.len; i++)
        sk_dashview_free(g->dviews.ptr[i].v);
    sk_vec_free(&g->dviews);
    /* The cache keys glyphs by face, so it goes before the faces do. */
    sk_glyph_cache_free(g->cache);
    sk_fontset_free(g->mono);
    sk_fontset_free(g->sans);
    sk_fonts_free(g->fonts);
    sk_ui_close();
    sk_appsettings_free(&g->settings);
    free(g->settings_file);
    free(g->last_dir);
    free(g->profiles_dir);
    free(g);
    sk_launch_close(&l);
    return status;
}
