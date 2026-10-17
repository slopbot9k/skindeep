#include "gui.h"

#include "../audio/sounds.h"
#include "../base/log.h"
#include "../base/utf8.h"
#include "../core/command.h"
#include "../core/loop.h"
#include "../core/names.h"
#include "../platform/ask.h"
#include "../platform/audio.h"
#include "../platform/picker.h"
#include "../sessions/files.h"
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
#include "js.h"
#include "lastskin.h"
#include "launch.h"
#include "chooser.h"
#include "menu.h"
#include "newtab.h"
#include "rename.h"
#include "skins.h"
#include "skinterm.h"
#include "tabviz.h"
#include "vizfeed.h"
#include "vizwin.h"
#include "../viz/viz.h"
#include "monitor.h"
#include "runtime.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* skin/compile.h's: that header brings the pack's own types, whose
 * sk_cursor is not platform/ui.h's. */
char *sk_skinc_beside(const char *self, const char *path);

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
    bool list_off; /* the active tab's choice of what its list replaces (decision 0034) */
    char *title;
    strip_text strip;
} snap_win;

typedef struct {
    snap_win *wins;
    size_t n;
    int current; /* the current window, whose title the visualization takes */
    int unseen;  /* this launch's problems not yet seen: the button's dot */
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
    /* New tab... (decision 0025), above everything while it is open. */
    sk_chooser *chooser;
    /* A tab's drop-down and its name edited in place (decision 0032). */
    sk_rename *rename;
    bool strips;   /* the strips alone are to be drawn again */
    bool term_all; /* the strips and the terminal drawn whole */
    bool term_press; /* a press the terminal took, held until its release */
    bool asking;     /* Close N tabs? is open */
    /* The hover text under the pointer, as a title attribute showed it,
     * where the pointer was, in points, and whether it shows yet. */
    char *tip;
    double tip_x, tip_y;
    bool tip_shown;
    int tip_timer;
    /* The skin in the stage (decision 0037), the timer of its next step,
     * the least size it gave the window, the pointer's shape. */
    sk_skinview *skin;
    int skin_timer;
    uint64_t skin_due; /* when the timer fires, ms */
    int min_w, min_h;
    sk_cursor cursor;
    /* While a drop-down or a dialog is up, what is below it is drawn
     * here and copied to the frame under it: base, and the rect the
     * overlays last covered. */
    sk_image base;
    sk_rect overlay;
    /* The skin's sounds (decision 0024), and the timer of their next
     * tick. */
    sk_sounds *sounds;
    int sounds_timer;
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
    uint64_t names_key; /* the tabs' names last posted: a hash */
    int status;
    sk_env paths;
    sk_foreign *foreign;
    sk_monitor_config mon;
    sk_dashboard_config dash;
    sk_boards *boards;
    sk_newtab_config newtab;
    sk_vizfeed *viz; /* the visualization's feed (decision 0015) */
    /* The skins (decisions 0008, 0037), the palette every terminal
     * takes from the current one, and the dials open (decision 0033). */
    sk_skins *skins;
    sk_term_palette palette;
    bool have_palette;
    sk_strv dials;
    bool sound_muted; /* the skin's sounds muted (decision 0024) */
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
    double font_setting; /* the terminals' font size setting; 0: the skin's */
    SK_VEC(gwin *) wins;
    /* The terminals' glyphs, made anew with each skin: a skin's own
     * faces go with its pack, and the cache keys glyphs by face. */
    sk_glyph_cache *term_cache;
    /* The skin's sounds: muted (--mute, SKINNED_MUTE, the menu), the
     * mixer the windows' players share, and the output, opened the first
     * time a sound may play. */
    bool muted;
    sk_mixer *mixer;
    sk_audio *audio;
    /* The visualization's window and the tab strip's bands. */
    sk_vizwin *vizwin;
    sk_tabviz *tabviz;
    uint64_t viz_at; /* the last frame's time, ms */
    /* The current skin, as the core last posted it: its pack, what its
     * terminals take, their font set, its kept split sizes, the dials,
     * and this launch's problems not yet seen (the button's dot). */
    sk_skinpack *pack;
    sk_skin_terminal skin_term;
    sk_fontset *term_fonts;
    SK_VEC(char *) panel_ids;
    SK_VEC(double) panel_sizes;
    sk_strv ui_dials;
    int unseen;
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
    /* The skin's theme, before any thread shares the terminal. */
    if (g->have_palette)
        sk_term_set_palette(s->term, &g->palette);
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

/* ---- Messages between the threads ----
 *
 * A message the receiving loop never runs, as the app ends, is freed by
 * its drop when that loop goes. Most are one block. */

static void drop_free(void *ud)
{
    free(ud);
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

static void drop_snapshot(void *ud)
{
    void **pair = ud;
    snapshot *s = pair[1];
    for (size_t i = 0; i < s->n; i++)
        free_snap_win(&s->wins[i]);
    free(s->wins);
    free(s);
    free(pair);
}

/* Every window's tabs as the UI shows them, posted to it. */
static void post_snapshot(gui *g)
{
    /* At launch, before the tabs: their first change posts one. */
    if (!g->tabs)
        return;
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
        w->list_off = active && active->list_off;
        sk_buf title = {0};
        sk_identity_title(&title, active ? sk_tab_name(active) : "skinned");
        w->title = title.ptr;
        if (active)
            strip_of(g, active, &w->strip);
    }
    sk_vec_free(&list);
    s->current = sk_tabs_current(g->tabs);
    s->unseen = (int)(g->problems.len - g->problems_seen);
    void **pair = sk_xmalloc(2 * sizeof *pair);
    pair[0] = g;
    pair[1] = s;
    sk_ui_post_owned(ui_snapshot, pair, drop_snapshot);
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
    sk_loop_post_owned(g->loop, do_input, m, drop_free);
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

/* The last screen of a tab's terminal as text, as its CLI ends by
 * itself (decision 0030): the core feeds the terminal, so it holds every
 * byte the CLI wrote. */
static char *tab_screen(void *ud, sk_tab *tab)
{
    ctab *ct = find(ud, tab->id);
    return ct && ct->st->term ? sk_term_screen_text(ct->st->term) : NULL;
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
    sk_loop_post_owned(g->loop, fn, m, drop_free);
}

/* A problem for Housekeeping, logged as main.js logs it. */
static void add_problem(gui *g, const char *title, const char *detail)
{
    problem p = {sk_xstrdup(title), sk_xstrdup(detail ? detail : "")};
    sk_vec_push(&g->problems, p);
    sk_log("%s: %s", title, p.detail);
    /* An unseen problem puts the dot on the button (decision 0022). */
    post_snapshot(g);
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

static void ui_viz_frame(void *ud);

static void drop_viz_frame(void *ud)
{
    void **pair = ud;
    sk_vizfeed_msg_free(pair[1]);
    free(pair);
}

/* The visualization's frame, to the UI thread, which owns it then. */
static void viz_frame(void *ud, sk_vizfeed_msg *msg)
{
    void **pair = sk_xmalloc(2 * sizeof *pair);
    pair[0] = ud;
    pair[1] = msg;
    sk_ui_post_owned(ui_viz_frame, pair, drop_viz_frame);
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
    sk_ui_post_owned(ui_dash_view, m, drop_free);
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

/* The visualization's panes follow the tabs (decision 0015): every tab
 * in the list's order, its bus, whether it is the active one and has a
 * pane. gone: a tab leaving, its bus about to go. */
static void viz_tabs(gui *g, const sk_tab *gone)
{
    if (!g->viz)
        return;
    size_t n = sk_tabs_count(g->tabs), k = 0;
    sk_vizfeed_tab *l = sk_xcalloc(n ? n : 1, sizeof *l);
    sk_tab *active = sk_tabs_active(g->tabs);
    for (size_t i = 0; i < n; i++) {
        sk_tab *t = sk_tabs_at(g->tabs, i);
        if (t == gone)
            continue;
        sk_bus *bus = t->kind == SK_TAB_MONITOR     ? sk_monitor_bus(t)
                      : t->kind == SK_TAB_DASHBOARD ? sk_dashboard_bus(t)
                                                    : sk_clitab_bus(t);
        l[k++] = (sk_vizfeed_tab){t->id, bus, t == active, t->kind != SK_TAB_DASHBOARD};
    }
    sk_vizfeed_tabs(g->viz, l, k);
    free(l);
}

/* ---- The skin on the core thread (decisions 0005 to 0008, 0037) ---- */

static sk_bus *bus_of(const sk_tab *t)
{
    return t->kind == SK_TAB_MONITOR     ? sk_monitor_bus(t)
           : t->kind == SK_TAB_DASHBOARD ? sk_dashboard_bus(t)
                                         : sk_clitab_bus(t);
}

/* The user's terminal font size, 0 when unset: the skin's own then. */
static double font_setting(const gui *g)
{
    return g->settings.set[SK_SETTING_FONT_SIZE] ? g->settings.value[SK_SETTING_FONT_SIZE] : 0;
}

/* A skin for the windows, with the split sizes the user keeps for it;
 * pack NULL for the sizes alone. */
typedef struct {
    gui *g;
    sk_skinpack *pack;
    char *skin;
    char **ids;
    double *sizes;
    size_t n;
} skin_msg;

static void drop_skin(void *ud)
{
    skin_msg *m = ud;
    sk_skinpack_unref(m->pack);
    for (size_t i = 0; i < m->n; i++)
        free(m->ids[i]);
    free(m->ids);
    free(m->sizes);
    free(m->skin);
    free(m);
}

static skin_msg *panels_msg(gui *g, const char *skin)
{
    skin_msg *m = sk_xcalloc(1, sizeof *m);
    m->g = g;
    m->skin = sk_xstrdup(skin);
    for (size_t i = 0; i < g->settings.panels.len; i++) {
        const sk_panel_sizes *p = &g->settings.panels.ptr[i];
        if (strcmp(p->skin, skin))
            continue;
        m->ids = sk_xcalloc(p->sizes.len ? p->sizes.len : 1, sizeof *m->ids);
        m->sizes = sk_xcalloc(p->sizes.len ? p->sizes.len : 1, sizeof *m->sizes);
        for (size_t k = 0; k < p->sizes.len; k++) {
            m->ids[m->n] = sk_xstrdup(p->sizes.ptr[k].id);
            m->sizes[m->n++] = p->sizes.ptr[k].size;
        }
    }
    return m;
}

static void ui_skin(void *ud);
static void ui_panels(void *ud);

/* The current skin's palette, which every new terminal takes. */
static void skin_palette(gui *g)
{
    sk_skinpack *p = sk_skins_current(g->skins);
    if (!p)
        return;
    sk_skin_terminal t;
    sk_skinpack_terminal(p, &t);
    sk_termview_style style;
    sk_termview_default_style(&style);
    sk_skin_term_style(&t, font_setting(g), &g->palette, &style);
    g->have_palette = true;
}

/* The current skin: its palette to every terminal now and every new
 * one, the pack and its panels to the windows. */
static void post_skin(gui *g)
{
    sk_skinpack *p = sk_skins_current(g->skins);
    if (!p)
        return;
    skin_palette(g);
    for (size_t i = 0; i < g->ctabs.len; i++) {
        sk_term *term = g->ctabs.ptr[i].st->term;
        sk_term_lock(term);
        sk_term_set_palette(term, &g->palette);
        sk_term_unlock(term);
    }
    skin_msg *m = panels_msg(g, sk_skinpack_name(p));
    m->pack = sk_skinpack_ref(p);
    sk_ui_post_owned(ui_skin, m, drop_skin);
}

/* The current skin recorded as this window's last (decision 0022). */
static void remember_skin(gui *g)
{
    /* In ms, as last.js's Date.now(): records of one second still sort. */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    double now = (double)ts.tv_sec * 1000 + (double)(ts.tv_nsec / 1000000);
    if (g->last_dir && sk_skins_remember(g->skins, g->last_dir, getpid(), now) != 0)
        sk_log("last skin: %s", strerror(errno));
}

typedef struct {
    gui *g;
    int win;
    sk_skin_metrics m;
} metrics_msg;

static void drop_metrics(void *ud)
{
    metrics_msg *m = ud;
    sk_skin_metrics_free(&m->m);
    free(m);
}

static void ui_metrics(void *ud);

/* main.js's 'metrics' ten times a second: each window's active tab's
 * bus, copied for its skin. */
/* A tab's name can follow its CLI's own title, as a /rename in Claude
 * Code sets it (decision 0032): checked with the metrics, every 100 ms,
 * as app/main.js sent the tabs then, and posted when one changed. */
static void check_names(gui *g)
{
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < sk_tabs_count(g->tabs); i++) {
        const sk_tab *t = sk_tabs_at(g->tabs, i);
        const char *name = sk_tab_name(t);
        for (const char *p = name ? name : ""; *p; p++)
            h = (h ^ (uint8_t)*p) * 1099511628211ull;
        h = (h ^ 0x1e ^ (uint64_t)(unsigned)t->id << 8) * 1099511628211ull;
    }
    if (h != g->names_key) {
        g->names_key = h;
        post_snapshot(g);
    }
}

static void on_metrics(sk_loop *loop, void *ud)
{
    (void)loop;
    gui *g = ud;
    check_names(g);
    int ids[64];
    size_t nw = SK_MIN(sk_tabs_wins(g->tabs, ids, SK_LEN(ids)), SK_LEN(ids));
    for (size_t k = 0; k < nw; k++) {
        sk_tab *a = sk_tabs_active_of(g->tabs, ids[k]);
        sk_bus *bus = a ? bus_of(a) : NULL;
        if (!bus)
            continue;
        metrics_msg *m = sk_xcalloc(1, sizeof *m);
        m->g = g;
        m->win = ids[k];
        sk_skin_metrics_of(bus, &m->m);
        sk_ui_post_owned(ui_metrics, m, drop_metrics);
    }
}

typedef struct {
    gui *g;
    int id;
    bool off;
    double size;
    char text[]; /* a dial's metric, a split's id */
} skin_act;

static void ui_dials(void *ud);

static void drop_dials(void *ud)
{
    void **pair = ud;
    sk_strv *v = pair[1];
    sk_strv_free(v);
    free(v);
    free(pair);
}

/* The dials to the views and the dashboards. */
static void send_dials(gui *g)
{
    for (size_t i = 0; i < sk_tabs_count(g->tabs); i++) {
        sk_tab *t = sk_tabs_at(g->tabs, i);
        if (t->kind == SK_TAB_DASHBOARD)
            sk_dashboard_set_dials(t, g->dials.ptr, g->dials.len);
    }
    sk_strv *copy = sk_xcalloc(1, sizeof *copy);
    for (size_t i = 0; i < g->dials.len; i++)
        sk_strv_push(copy, g->dials.ptr[i]);
    void **pair = sk_xmalloc(2 * sizeof *pair);
    pair[0] = g;
    pair[1] = copy;
    sk_ui_post_owned(ui_dials, pair, drop_dials);
}

/* dial:toggle (decision 0033): a known metric's dial opened or closed;
 * opening the first opens the dashboard as the menu does. */
static void do_dial(void *ud)
{
    skin_act *m = ud;
    gui *g = m->g;
    if (sk_metric_known(sk_str_c(m->text))) {
        size_t at = g->dials.len;
        for (size_t i = 0; i < g->dials.len; i++)
            if (!strcmp(g->dials.ptr[i], m->text))
                at = i;
        bool opened = at == g->dials.len;
        if (opened) {
            sk_strv_push(&g->dials, m->text);
        } else {
            free(g->dials.ptr[at]);
            memmove(&g->dials.ptr[at], &g->dials.ptr[at + 1], (g->dials.len - at) * sizeof *g->dials.ptr);
            g->dials.len--;
        }
        send_dials(g);
        bool dashboard = false;
        for (size_t i = 0; i < sk_tabs_count(g->tabs); i++)
            dashboard |= sk_tabs_at(g->tabs, i)->kind == SK_TAB_DASHBOARD;
        char *err = NULL;
        if (opened && !dashboard && !sk_dashboard_open(g->tabs, g->l->cwd, &err) && err) {
            add_problem(g, "Sessions dashboard failed", err);
            free(err);
        }
    }
    free(m);
}

/* tab:list-off (decision 0034): what the tab's list replaces shown. */
static void do_list_off(void *ud)
{
    skin_act *m = ud;
    sk_tab *t = sk_tabs_get(m->g->tabs, m->id);
    if (t && t->list_off != m->off) {
        t->list_off = m->off;
        post_snapshot(m->g);
    }
    free(m);
}

/* The skin's split sizes as the user keeps them, saved and sent to
 * every window. */
static void save_panels(gui *g, const char *skin)
{
    if (g->settings_file && sk_appsettings_save(g->settings_file, &g->settings) != 0)
        sk_log("settings: %s", strerror(errno));
    sk_ui_post_owned(ui_panels, panels_msg(g, skin), drop_skin);
}

/* panels:set (decision 0035): a split let go at size. */
static void do_panel(void *ud)
{
    skin_act *m = ud;
    gui *g = m->g;
    sk_skinpack *p = sk_skins_current(g->skins);
    if (p && isfinite(m->size) && m->size >= 0) {
        sk_appsettings_set_panel(&g->settings, sk_skinpack_name(p), m->text, m->size);
        save_panels(g, sk_skinpack_name(p));
    }
    free(m);
}

/* Decision 0037's watch of a skin opened by path, once a second: a
 * change compiled is the skin again, as a reload; one that does not
 * compile keeps the current skin and says why. */
static void on_skins_poll(sk_loop *loop, void *ud)
{
    (void)loop;
    gui *g = ud;
    sk_buf errs = {0};
    switch (sk_skins_poll(g->skins, &errs)) {
    case SK_SKINS_RELOADED:
        post_skin(g);
        break;
    case SK_SKINS_REFUSED: {
        sk_buf t = {0};
        sk_buf_printf(&t, "Skin %s changed and was refused", sk_skins_current_id(g->skins));
        add_problem(g, t.ptr, sk_buf_cstr(&errs));
        sk_buf_free(&t);
        break;
    }
    default:
        break;
    }
    sk_buf_free(&errs);
}

static void on_change(void *ud)
{
    viz_tabs(ud, NULL);
    post_snapshot(ud);
}

static void on_active(void *ud, sk_tab *tab)
{
    (void)tab;
    viz_tabs(ud, NULL);
    post_snapshot(ud);
}

/* A CLI that exited on its own with a status other than 0, while other
 * tabs remain, under Housekeeping with the dot (decision 0023), as
 * app/main.js tells it: why it could not start, its command line and its
 * folder. */
static void exit_problem(gui *g, const sk_tab *tab, int code)
{
    sk_session *s = sk_clitab_session(tab);
    const char *home = sk_envp_get(g->env, "HOME");
    sk_buf title = {0}, detail = {0};
    sk_buf_printf(&title, "%s exited with status %d", tab->label, code);
    const char *why = s ? sk_session_failure(s) : NULL;
    if (why)
        sk_buf_printf(&detail, "cannot start %s: %s\n", tab->cmd.ptr[0], why);
    char *const *argv = s && sk_session_argv(s) ? sk_session_argv(s) : tab->cmd.ptr;
    sk_identity_quote_argv(&detail, argv);
    sk_buf_append_c(&detail, "\nin ");
    sk_identity_tildify(&detail, tab->cwd, home);
    add_problem(g, title.ptr, detail.ptr);
    sk_buf_free(&title);
    sk_buf_free(&detail);
}

static void on_gone(void *ud, sk_tab *tab, int code, sk_tab *next)
{
    gui *g = ud;
    viz_tabs(g, tab);
    if (!next && !tab->closed && code != 0 && tab->kind == SK_TAB_CLI && sk_tabs_count(g->tabs) > 0)
        exit_problem(g, tab, code);
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

typedef struct {
    gui *g;
    int id;
    char name[];
} rename_msg;

/* tab:rename (decision 0032): the name kept, and written into the CLI's
 * record where it keeps one. */
static void do_rename(void *ud)
{
    rename_msg *m = ud;
    sk_tabs_rename(m->g->tabs, m->id, m->name);
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
    row_item(v, "New tab...", ACT_CHOOSER, SK_ROW_CLOSES, NULL);
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
    /* The skin's own value, which Skin default names: settings.js reads
     * it from the manifest, terminal.fontSize. */
    sk_arena arena;
    sk_arena_init(&arena, 0);
    const sk_json *manifest = NULL;
    sk_skinpack *current = g->skins ? sk_skins_current(g->skins) : NULL;
    if (current) {
        sk_skin_terminal t;
        sk_skinpack_terminal(current, &t);
        char text[64];
        snprintf(text, sizeof text, "{\"terminal\":{\"fontSize\":%g}}", t.font_px);
        manifest = sk_json_parse(&arena, text, strlen(text), NULL);
    }
    sk_appsettings_menu(&g->settings, manifest, rows);
    sk_arena_free(&arena);
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
    /* The skin's splits back to its own sizes (decision 0035). */
    sk_skinpack *skin = g->skins ? sk_skins_current(g->skins) : NULL;
    row_item(v, "Reset panel sizes", ACT_RESET_PANELS, SK_ROW_WAITS, NULL)->disabled =
        !skin || !sk_skinpack_splits(skin);
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
    if (g->problems_seen != g->problems.len) {
        g->problems_seen = g->problems.len;
        post_snapshot(g);
    }
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

/* Skins: Open skin... and the skins, the current one marked. A skin
 * opened by path shows its folder: it may replace a shipped skin of the
 * same name for the run. */
static void skins_rows(gui *g, row_vec *v)
{
    row_item(v, "Open skin...", ACT_OPEN_SKIN, SK_ROW_WAITS, NULL);
    row_add(v, SK_ROW_RULE, NULL);
    row_add(v, SK_ROW_HEADING, "Skins");
    for (size_t i = 0; i < sk_skins_count(g->skins); i++) {
        sk_skins_entry e = sk_skins_at(g->skins, i);
        sk_buf text = {0};
        sk_buf_printf(&text, "%s%s", e.current ? "* " : "  ", e.id);
        sk_menu_row *r = row_item(v, text.ptr, ACT_SELECT_SKIN, SK_ROW_WAITS, NULL);
        r->current = e.current;
        r->arg = sk_xstrdup(e.id);
        if (e.location) {
            sk_buf shown = {0};
            sk_identity_tildify(&shown, e.location, sk_envp_get(g->env, "HOME"));
            r->location = sk_xstrdup(sk_buf_cstr(&shown));
            sk_buf_free(&shown);
        }
        sk_buf_free(&text);
    }
}

/* A flyout of the visualization's choices, the current one marked. */
static void viz_flyout(row_vec *v, const char *title, const sk_vizfeed_choice *c, size_t n, int action)
{
    size_t at = v->len;
    row_add(v, SK_ROW_FLYOUT, title);
    sk_menu_row *f = &v->ptr[at];
    f->nsub = n;
    f->sub = sk_xcalloc(n ? n : 1, sizeof *f->sub);
    for (size_t i = 0; i < n; i++) {
        char text[128];
        snprintf(text, sizeof text, "%s%s", c[i].current ? "* " : "  ", c[i].label);
        sk_menu_row *s = &f->sub[i];
        s->kind = SK_ROW_ITEM;
        s->text = sk_xstrdup(text);
        s->current = c[i].current;
        s->disabled = !c[i].available;
        s->action = action;
        s->then = SK_ROW_WAITS;
        s->arg = sk_xstrdup(c[i].id);
    }
}

/* Visualization: the window, its modes and inputs (decision 0015), and
 * the skin's sounds (decision 0024), which wait for the skin runtime. */
static void viz_rows(gui *g, row_vec *v)
{
    bool open = sk_vizfeed_is_open(g->viz);
    row_item(v, open ? "Hide visualization" : "Show visualization", ACT_TOGGLE_VIZ, SK_ROW_CLOSES, NULL);
    sk_vizfeed_choice modes[SK_VIZ_MODES], inputs[SK_VIZ_INPUTS];
    size_t nm = sk_vizfeed_modes(g->viz, modes), ni = sk_vizfeed_inputs(g->viz, inputs);
    viz_flyout(v, "Visualization mode", modes, nm, ACT_VIZ_MODE);
    viz_flyout(v, "Visualization input", inputs, ni, ACT_VIZ_INPUT);
    row_item(v, g->sound_muted ? "Unmute skin sounds" : "Mute skin sounds", ACT_TOGGLE_SOUND, SK_ROW_CLOSES, NULL);
}

/* ---- New tab... on the core thread ---- */

typedef struct {
    gui *g;
    int win;
    sk_newtab_state st;
    sk_newtab_pick pick;
    char *err;
} chooser_msg;

static void chooser_msg_free(chooser_msg *m)
{
    sk_newtab_state_free(&m->st);
    sk_newtab_pick_free(&m->pick);
    free(m->err);
    free(m);
}

static void drop_chooser(void *ud)
{
    chooser_msg_free(ud);
}

static void ui_chooser_open(void *ud);
static void ui_chooser_answer(void *ud);
static void ui_chooser_forgot(void *ud);

/* chooserState for window win, the home scanned again: the dialog opens
 * on it. */
static void chooser_offer(gui *g, int win)
{
    chooser_msg *m = sk_xcalloc(1, sizeof *m);
    m->g = g;
    m->win = win;
    sk_tabs_touch(g->tabs, win);
    sk_newtab_offer(&g->newtab, win, true, &m->st);
    sk_ui_post_owned(ui_chooser_open, m, drop_chooser);
}

static void do_chooser_offer(void *ud)
{
    tab_msg *m = ud;
    chooser_offer(m->g, m->win);
    free(m);
}

/* chooserOpen: the tab opened, or why not for the dialog to show. */
static void do_chooser_open(void *ud)
{
    chooser_msg *m = ud;
    sk_tabs_touch(m->g->tabs, m->win);
    if (sk_newtab_open(&m->g->newtab, m->win, &m->pick, &m->err) != 0 && !m->err)
        m->err = sk_xstrdup("the tab could not be opened");
    sk_ui_post_owned(ui_chooser_answer, m, drop_chooser);
}

/* chooser:forget, then the offer again from the same scan. */
static void do_chooser_forget(void *ud)
{
    chooser_msg *m = ud;
    sk_newtab_forget(&m->g->newtab, m->pick.agent, m->pick.profile);
    sk_newtab_offer(&m->g->newtab, m->win, false, &m->st);
    sk_ui_post_owned(ui_chooser_forgot, m, drop_chooser);
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

static void drop_menu(void *ud)
{
    menu_msg *m = ud;
    sk_menu_rows_free(m->rows, m->n);
    free(m);
}

static void drop_sessions(void *ud)
{
    menu_msg *m = ud;
    sk_menu_sessions_free(m->sessions, m->n);
    free(m);
}

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
        skins_rows(g, &v);
        break;
    default:
        viz_rows(g, &v);
        break;
    }
    m->rows = v.ptr;
    m->n = v.len;
    sk_ui_post_owned(ui_menu, m, drop_menu);
}

/* The agent sessions again, for the box that shows them. */
static void do_sessions(void *ud)
{
    menu_msg *m = ud;
    sessions_now(m->g, &m->sessions, &m->n);
    sk_ui_post_owned(ui_sessions, m, drop_sessions);
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
static void ui_mute(void *ud);
static void ui_settings(void *ud);

static void drop_answer(void *ud)
{
    act_msg *m = ud;
    free(m->error);
    free(m->note);
    free(m);
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
        s->font_px = font_setting(g);
        sk_ui_post_owned(ui_settings, s, drop_free);
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
    case ACT_CHOOSER:
        chooser_offer(g, m->win);
        break;
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
    case ACT_SELECT_SKIN:
    case ACT_OPEN_SKIN: {
        /* skin:select and skin:open: the skin shown and recorded, or
         * refused with the current skin kept (decision 0008). */
        sk_buf errs = {0};
        bool ok = m->action == ACT_SELECT_SKIN ? sk_skins_select(g->skins, m->arg, &errs)
                                               : sk_skins_open(g->skins, m->arg, &errs);
        if (ok) {
            post_skin(g);
            remember_skin(g);
            sk_buf_free(&errs);
        } else {
            /* Its messages, one a line, the last line's break dropped. */
            if (errs.len && errs.ptr[errs.len - 1] == '\n')
                errs.ptr[--errs.len] = 0;
            err = errs.ptr ? errs.ptr : sk_xstrdup("the skin was refused");
        }
        break;
    }
    case ACT_RESET_PANELS: {
        sk_skinpack *p = sk_skins_current(g->skins);
        if (p) {
            sk_appsettings_set_panel(&g->settings, sk_skinpack_name(p), NULL, 0);
            save_panels(g, sk_skinpack_name(p));
        }
        break;
    }
    case ACT_TOGGLE_VIZ:
        sk_vizfeed_toggle(g->viz);
        break;
    case ACT_TOGGLE_SOUND: {
        /* sound:toggle: muted for the launch, every window's sounds. */
        g->sound_muted = !g->sound_muted;
        act_msg *s = sk_xcalloc(1, sizeof *s);
        s->g = g;
        s->waits = g->sound_muted;
        sk_ui_post_owned(ui_mute, s, drop_free);
        break;
    }
    case ACT_VIZ_MODE:
        if (!sk_vizfeed_select(g->viz, m->arg)) {
            sk_buf b = {0};
            sk_buf_printf(&b, "no visualization mode %s", m->arg);
            err = b.ptr;
        }
        break;
    case ACT_VIZ_INPUT: {
        const char *why = NULL;
        if (!sk_vizfeed_select_input(g->viz, m->arg, &why)) {
            sk_buf b = {0};
            sk_buf_printf(&b, "%s", why ? why : "no visualization input");
            err = b.ptr;
        }
        break;
    }
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
        sk_ui_post_owned(ui_answer, m, drop_answer);
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
    /* tabs.js holds the strip as it is while a tab's drop-down or a name
     * being edited is up. */
    return w->snap.n > 1 || sk_rename_popup_open(w->rename) || sk_rename_editing(w->rename);
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

/* The terminal's part of the frame, in pixels: the skin's rect for it,
 * or without a skin #term's 4 points in. */
static sk_rect term_area(gwin *w)
{
    if (w->skin)
        return sk_skinview_terminal_px(w->skin);
    sk_rect s = stage_area(w);
    int in = px(w, TERM_INSET);
    return sk_rect_make(s.x + in, s.y + in, SK_MAX(0, s.w - 2 * in), SK_MAX(0, s.h - 2 * in));
}

/* The strips and the terminal drawn whole, the skin as it changed:
 * what a key, a blink, a selection or a tab's change asks. */
static void redraw(gwin *w)
{
    w->term_all = true;
    sk_window_request_frame(w->win);
}

/* Everything drawn whole, the skin too: a resize, a new skin, the
 * stage moved by the tab strip or a split. */
static void redraw_all(gwin *w)
{
    w->all = true;
    sk_window_request_frame(w->win);
}

/* The strips alone drawn again: a tab band's column, a name being
 * edited; no reason to draw the terminal or the skin. */
static void redraw_strips(gwin *w)
{
    w->strips = true;
    sk_window_request_frame(w->win);
}

/* A drop-down or the dialog changed: each frame composites them over
 * what is below, so a frame is all it takes. */
static void overlay_frame(gwin *w)
{
    sk_window_request_frame(w->win);
}

static void skin_stage(gwin *w);
static void skin_floor(gwin *w);

/* The terminal's top-left in window points. */
static void term_origin(gwin *w, double *x, double *y)
{
    if (w->skin) {
        double r[4];
        sk_skinview_terminal_rect(w->skin, r);
        *x = r[0];
        *y = r[1];
        return;
    }
    *x = TERM_INSET;
    *y = top_points(w) + TERM_INSET;
}

/* The grid that fits, sent to the core when it changed: the skin's
 * stage and floor first, where the terminal's rect comes from. */
static void fit(gwin *w)
{
    skin_stage(w);
    skin_floor(w);
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
    sk_loop_post_owned(w->g->loop, do_resize, m, drop_free);
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

/* ---- The skin in a window (decisions 0005 to 0008, 0037) ---- */

/* The pointer's shape, set when it changes. */
static void set_cursor(gwin *w, sk_cursor c)
{
    if (c == w->cursor)
        return;
    w->cursor = c;
    sk_window_set_cursor(w->win, c);
}

/* The stage is the window below the strips. */
static void skin_stage(gwin *w)
{
    if (!w->skin)
        return;
    int pw, ph;
    sk_window_size(w->win, &pw, &ph);
    double top = top_points(w);
    sk_skinview_set_stage(w->skin, 0, top, pw, SK_MAX(0.0, ph - top), sk_window_scale(w->win));
}

/* scale.js's floor, grown by the tab strip while it shows, the window
 * grown to it (main.js's fitWindow). */
static void skin_floor(gwin *w)
{
    if (!w->g->pack)
        return;
    sk_skin_window sw;
    sk_skinpack_window(w->g->pack, &sw);
    int min_h = sw.min_height + (tabs_shown(w) ? TABS_H : 0);
    if (sw.min_width == w->min_w && min_h == w->min_h)
        return;
    w->min_w = sw.min_width;
    w->min_h = min_h;
    sk_window_set_min_size(w->win, w->min_w, w->min_h);
}

/* The terminals' look from the skin (skin.js's styleTerminal): the
 * selection's and the cursor's colours, the font set, and its size,
 * the user's setting over the skin's. */
static void skin_term_style(gwin *w)
{
    gui *g = w->g;
    if (!g->pack)
        return;
    sk_term_palette pal;
    sk_skin_term_style(&g->skin_term, g->font_setting, &pal, &w->style);
    w->style.fonts = g->term_fonts ? g->term_fonts : g->mono;
    w->style.cache = g->term_cache ? g->term_cache : g->cache;
    w->style.scale = sk_window_scale(w->win);
    sk_termview_set_style(w->view, &w->style);
}

static void skin_frame(gwin *w)
{
    sk_window_request_frame(w->win);
}

static void on_skin_timer(void *ud)
{
    gwin *w = ud;
    w->skin_timer = 0;
    skin_frame(w);
}

/* The window's view of the current skin, made anew, told all it is
 * told after: the kept splits, the dials, the list's choice, the menu. */
static void skin_view_make(gwin *w)
{
    gui *g = w->g;
    sk_skinview_free(w->skin);
    w->skin = NULL;
    if (w->skin_timer) {
        sk_ui_cancel(w->skin_timer);
        w->skin_timer = 0;
    }
    if (!g->pack)
        return;
    w->skin = sk_skinview_new(g->pack, (double)sk_now_ms());
    skin_stage(w);
    sk_skinview_set_panels(w->skin, (const char *const *)g->panel_ids.ptr, g->panel_sizes.ptr, g->panel_ids.len);
    sk_skinview_set_dials(w->skin, (const char *const *)g->ui_dials.ptr, g->ui_dials.len);
    sk_skinview_set_list_off(w->skin, w->snap.list_off);
    sk_skinview_set_menu(w->skin, sk_menu_open(w->menu), g->unseen > 0);
    skin_floor(w);
}

/* The skin's sounds: a player each window, ticked when it says. */
static void sounds_due(gwin *w, long ms);

static void on_sounds_tick(void *ud)
{
    gwin *w = ud;
    w->sounds_timer = 0;
    sounds_due(w, sk_sounds_tick(w->sounds));
}

static void sounds_due(gwin *w, long ms)
{
    if (w->sounds_timer) {
        sk_ui_cancel(w->sounds_timer);
        w->sounds_timer = 0;
    }
    if (ms >= 0)
        w->sounds_timer = sk_ui_timer((uint64_t)ms, 0, on_sounds_tick, w);
}

/* The current skin's sounds into the window's player, muted as the
 * launch is; the output opened the first time one may sound. */
static void skin_sounds(gwin *w)
{
    gui *g = w->g;
    if (!w->sounds || !g->pack)
        return;
    sk_sounds_entry e[SK_SOUNDS_MAX];
    size_t n = sk_skinpack_sounds(g->pack, e, SK_SOUNDS_MAX);
    if (n && !g->muted && !g->audio)
        g->audio = sk_audio_open(g->mixer);
    sk_sounds_set(w->sounds, e, n);
    sounds_due(w, sk_sounds_mute(w->sounds, g->muted));
    sk_skin_sounds_release(e, n);
}

/* The menu's mute: every window's player muted or not, the output
 * opened the first time a sound may play. */
static void ui_mute(void *ud)
{
    act_msg *m = ud;
    gui *g = m->g;
    g->muted = m->waits;
    for (size_t i = 0; i < g->wins.len; i++) {
        gwin *w = g->wins.ptr[i];
        if (!w->sounds)
            continue;
        if (!g->muted && !g->audio && sk_sounds_count(w->sounds))
            g->audio = sk_audio_open(g->mixer);
        sounds_due(w, sk_sounds_mute(w->sounds, g->muted));
    }
    free(m);
}

/* The kept split sizes of the current skin, from a message. */
static void take_panels(gui *g, const skin_msg *m)
{
    for (size_t i = 0; i < g->panel_ids.len; i++)
        free(g->panel_ids.ptr[i]);
    g->panel_ids.len = g->panel_sizes.len = 0;
    for (size_t i = 0; m && i < m->n; i++) {
        sk_vec_push(&g->panel_ids, sk_xstrdup(m->ids[i]));
        sk_vec_push(&g->panel_sizes, m->sizes[i]);
    }
}

/* A skin for every window: its pack, its terminals' look in a font set
 * and glyph cache of their own, its kept splits, a view and sounds a
 * window, each window's floor and grid again. */
static void ui_take_skin(gui *g, sk_skinpack *p, const skin_msg *m)
{
    if (!p)
        return;
    sk_skinpack *old = g->pack;
    sk_fontset *old_fonts = g->term_fonts;
    sk_glyph_cache *old_cache = g->term_cache;
    g->pack = sk_skinpack_ref(p);
    sk_skinpack_terminal(p, &g->skin_term);
    g->term_fonts = sk_skin_term_fonts(&g->skin_term, g->fonts);
    g->term_cache = sk_glyph_cache_new((size_t)16 << 20);
    take_panels(g, m);
    for (size_t i = 0; i < g->wins.len; i++) {
        gwin *w = g->wins.ptr[i];
        skin_term_style(w);
        skin_view_make(w);
        skin_sounds(w);
        fit(w);
        redraw_all(w);
    }
    /* The glyphs are keyed by face: the cache goes before the faces. */
    sk_glyph_cache_free(old_cache);
    sk_fontset_free(old_fonts);
    sk_skinpack_unref(old);
}

static void ui_skin(void *ud)
{
    skin_msg *m = ud;
    ui_take_skin(m->g, m->pack, m);
    drop_skin(m);
}

static void ui_panels(void *ud)
{
    skin_msg *m = ud;
    gui *g = m->g;
    if (g->pack && !strcmp(sk_skinpack_name(g->pack), m->skin)) {
        take_panels(g, m);
        for (size_t i = 0; i < g->wins.len; i++) {
            gwin *w = g->wins.ptr[i];
            if (!w->skin)
                continue;
            sk_skinview_set_panels(w->skin, (const char *const *)g->panel_ids.ptr, g->panel_sizes.ptr,
                                   g->panel_ids.len);
            fit(w);
            redraw_all(w);
        }
    }
    drop_skin(m);
}

static void ui_metrics(void *ud)
{
    metrics_msg *m = ud;
    gwin *w = gwin_of(m->g, m->win);
    if (w && w->skin) {
        sk_skinview_set_metrics(w->skin, m->m.items, m->m.n);
        skin_frame(w);
    }
    drop_metrics(m);
}

static void ui_dials(void *ud)
{
    void **pair = ud;
    gui *g = pair[0];
    sk_strv *v = pair[1];
    sk_strv_free(&g->ui_dials);
    for (size_t i = 0; i < v->len; i++)
        sk_strv_push(&g->ui_dials, v->ptr[i]);
    for (size_t i = 0; i < g->wins.len; i++) {
        gwin *w = g->wins.ptr[i];
        if (!w->skin)
            continue;
        sk_skinview_set_dials(w->skin, (const char *const *)g->ui_dials.ptr, g->ui_dials.len);
        skin_frame(w);
    }
    drop_dials(pair);
}

typedef struct {
    gui *g;
    int win;
} close_ask;

/* The answer to Close N tabs?, for the window it was asked of, if it is
 * still there. */
static void close_answered(void *ud, bool yes)
{
    close_ask c = *(close_ask *)ud;
    free(ud);
    gwin *w = gwin_of(c.g, c.win);
    if (!w)
        return;
    w->asking = false;
    if (yes)
        post_tab(c.g, do_close_win, 0, c.win);
}

/* A window of several tabs asks before it hangs them up (decision 0023),
 * as main.js asks with a dialog on the window; a second close while it
 * asks changes nothing. */
static void ask_close(gwin *w)
{
    if (w->asking)
        return;
    w->asking = true;
    sk_buf msg = {0};
    sk_buf_printf(&msg, "Close %zu tabs?", w->snap.n);
    sk_ask_question q = {"claude-skins", msg.ptr, "Each tab's CLI is hung up.", "Close tabs", "Cancel"};
    close_ask *c = sk_xmalloc(sizeof *c);
    *c = (close_ask){w->g, w->snap.id};
    sk_ask_confirm(w->win, w->g->loop, &q, close_answered, c);
    sk_buf_free(&msg);
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
    w->cursor = SK_CURSOR_IBEAM;
    /* The skin's window (decision 0006), else main.js's without one. */
    sk_skin_window sw = {WINDOW_W, WINDOW_H, WINDOW_MIN_W, WINDOW_MIN_H};
    if (g->pack)
        sk_skinpack_window(g->pack, &sw);
    w->min_w = sw.min_width;
    w->min_h = sw.min_height;
    sk_window_opts wo = {.title = "skinned",
                         .w = sw.width,
                         .h = sw.height,
                         .min_w = sw.min_width,
                         .min_h = sw.min_height,
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
    w->style.font_px = 14;
    w->style.scale = sk_window_scale(w->win);
    w->view = sk_termview_new(&w->style);
    skin_term_style(w);
    sk_term_mouse_init(&w->mouse);
    w->menu = sk_menu_new();
    w->rename = sk_rename_new();
    if (g->mixer) {
        w->sounds = sk_sounds_new(g->mixer, NULL, NULL, (uint64_t)getpid() * 31 + (uint64_t)id);
        skin_sounds(w);
    }
    sk_vec_push(&g->wins, w);
    skin_view_make(w);
    return w;
}

static void gwin_free(gwin *w)
{
    if (w->tip_timer)
        sk_ui_cancel(w->tip_timer);
    free(w->tip);
    if (w->blink_timer)
        sk_ui_cancel(w->blink_timer);
    if (w->sync_timer)
        sk_ui_cancel(w->sync_timer);
    if (w->sessions_timer)
        sk_ui_cancel(w->sessions_timer);
    if (w->skin_timer)
        sk_ui_cancel(w->skin_timer);
    if (w->sounds_timer)
        sk_ui_cancel(w->sounds_timer);
    sk_sounds_free(w->sounds);
    sk_skinview_free(w->skin);
    sk_image_free(&w->base);
    sk_menu_free(w->menu);
    sk_chooser_free(w->chooser);
    sk_rename_free(w->rename);
    free_snap_win(&w->snap);
    free(w->link_uri);
    free(w->preedit);
    sk_termview_free(w->view);
    sk_window_free(w->win);
    free(w);
}

static void drop_dviews(gui *g);
static snap_tab *snap_tab_of(gwin *w, int id);

/* Each window as the snapshot has it: new windows open, windows with no
 * tabs left close. */
static void ui_snapshot(void *ud)
{
    void **pair = ud;
    gui *g = pair[0];
    snapshot *s = pair[1];
    free(pair);
    g->unseen = s->unseen;
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
        bool strip = tabs_shown(w);
        free_snap_win(&w->snap);
        w->snap = *sw;
        sk_window_set_title(w->win, w->snap.title);
        /* A name being edited whose tab left ends, the terminal's focus
         * back as an edit's end gives it. */
        if (sk_rename_editing(w->rename) && !snap_tab_of(w, sk_rename_tab(w->rename))) {
            sk_rename_end(w->rename);
            w->ime_x = w->ime_y = -1;
            focus_term(w);
        }
        bool moved = strip != tabs_shown(w) || fresh;
        if (moved)
            fit(w);
        /* The active tab's list choice and the problems' dot. */
        if (w->skin) {
            sk_skinview_set_list_off(w->skin, w->snap.list_off);
            sk_skinview_set_menu(w->skin, sk_menu_open(w->menu), s->unseen > 0);
            skin_frame(w);
        }
        if (switched) {
            sk_term_mouse_reset(&w->mouse);
            w->selecting = false;
            clear_hover(w);
            focus_term(w);
        }
        if (fresh)
            sk_window_focus(w->win);
        /* The tab strip shown or gone moves the stage: everything. */
        if (moved)
            redraw_all(w);
        else
            redraw(w);
    }
    drop_dviews(g);
    /* A band for each tab with a session, every window's; the window
     * the visualization sits by gives it its title. */
    if (g->tabviz) {
        SK_VEC(int) ids = {0};
        for (size_t i = 0; i < g->wins.len; i++)
            for (size_t k = 0; k < g->wins.ptr[i]->snap.n; k++)
                if (g->wins.ptr[i]->snap.tabs[k].kind != SK_TAB_DASHBOARD)
                    sk_vec_push(&ids, g->wins.ptr[i]->snap.tabs[k].id);
        sk_tabviz_keep(g->tabviz, ids.ptr, ids.len);
        sk_vec_free(&ids);
    }
    gwin *cur = gwin_of(g, s->current);
    if (g->vizwin && cur && cur->snap.title) {
        sk_buf t = {0};
        sk_buf_printf(&t, "visualization: %s", cur->snap.title);
        sk_vizwin_set_title(g->vizwin, t.ptr);
        sk_buf_free(&t);
    }
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
    sk_loop_post_owned(w->g->loop, do_menu, m, drop_free);
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
    sk_loop_post_owned(w->g->loop, do_sessions, m, drop_free);
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
    /* The button shows pressed while the drop-down is open. */
    if (w->skin) {
        sk_skinview_set_menu(w->skin, sk_menu_open(w->menu), w->g->unseen > 0);
        skin_frame(w);
    }
    overlay_frame(w);
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
        overlay_frame(w);
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
    g->font_setting = m->font_px;
    for (size_t i = 0; i < g->wins.len; i++) {
        gwin *w = g->wins.ptr[i];
        skin_term_style(w);
        fit(w);
        redraw(w);
    }
    free(m);
}

/* A press on the drop-down: what it asks for, run on the core. */
/* A row's action to the core, arg its argument, answered to press. */
static void post_action(gwin *w, int action, const char *arg, bool waits, unsigned press)
{
    size_t n = arg ? strlen(arg) : 0;
    act_msg *m = sk_xcalloc(1, sizeof *m + n + 1);
    m->g = w->g;
    m->win = w->snap.id;
    m->action = action;
    m->press = press;
    m->waits = waits;
    if (n)
        memcpy(m->arg, arg, n);
    sk_loop_post_owned(w->g->loop, do_action, m, drop_free);
}

/* Open skin...'s folder (skin:open): the core opens it; a cancel closes
 * the menu, a picker that cannot run says why in it. */
typedef struct {
    gui *g;
    int win;
    unsigned press;
} pick_req;

static void on_skin_folder(void *ud, const char *dir, const char *err)
{
    pick_req *r = ud;
    gwin *w = gwin_of(r->g, r->win);
    if (w && dir) {
        post_action(w, ACT_OPEN_SKIN, dir, true, r->press);
    } else if (w) {
        sk_menu_answer(w->menu, r->press, err, NULL);
        menu_changed(w);
    }
    free(r);
}

static void menu_press(gwin *w, const sk_menu_press *p)
{
    if (p->kind == SK_MENU_PRESS_SHOW) {
        menu_ask(w, p->menu);
    } else if (p->kind == SK_MENU_PRESS_ACTION && p->action == ACT_OPEN_SKIN) {
        pick_req *r = sk_xmalloc(sizeof *r);
        *r = (pick_req){w->g, w->snap.id, p->press};
        sk_picker_opts o = {"Open skin", NULL, false};
        sk_picker_folder(w->win, w->g->loop, &o, on_skin_folder, r);
    } else if (p->kind == SK_MENU_PRESS_ACTION || p->kind == SK_MENU_PRESS_SESSION) {
        post_action(w, p->kind == SK_MENU_PRESS_SESSION ? ACT_SESSION : p->action, p->arg, p->waits, p->press);
    }
    menu_changed(w);
}

/* ---- New tab... ---- */

static bool chooser_up(gwin *w)
{
    return w->chooser && sk_chooser_is_open(w->chooser);
}

static void on_chooser_changed(void *ud)
{
    gwin *w = ud;
    if (chooser_up(w))
        set_cursor(w, sk_chooser_cursor(w->chooser));
    overlay_frame(w);
}

/* The window's dialog, made when it is first asked for: the core's loop,
 * which runs Linux's folder picker, comes after the first window. */
static sk_chooser *chooser_of(gwin *w)
{
    if (!w->chooser) {
        sk_chooser_config c;
        memset(&c, 0, sizeof c);
        c.win = w->win;
        c.loop = w->g->loop;
        c.changed = on_chooser_changed;
        c.ud = w;
        w->chooser = sk_chooser_new(&c);
    }
    return w->chooser;
}

/* window.Chooser.open: the core's offer first. */
static void chooser_ask(gwin *w)
{
    post_tab(w->g, do_chooser_offer, 0, w->snap.id);
}

/* The dialog closed: the terminal has the keys again. */
static void chooser_closed(gwin *w)
{
    focus_term(w);
    set_cursor(w, SK_CURSOR_IBEAM);
    w->ime_x = w->ime_y = -1;
    redraw(w);
}

static void ui_chooser_open(void *ud)
{
    chooser_msg *m = ud;
    gwin *w = gwin_of(m->g, m->win);
    if (w) {
        sk_chooser_open(chooser_of(w), &m->st);
        /* The command line has the focus, not the terminal. */
        sk_term *t = active_term(w);
        if (t)
            sk_term_set_focus(t, false);
        on_chooser_changed(w);
    }
    chooser_msg_free(m);
}

static void ui_chooser_answer(void *ud)
{
    chooser_msg *m = ud;
    gwin *w = gwin_of(m->g, m->win);
    if (w && chooser_up(w)) {
        if (m->err) {
            sk_chooser_fail(w->chooser, m->err);
            overlay_frame(w);
        } else {
            sk_chooser_close(w->chooser);
            chooser_closed(w);
        }
    }
    chooser_msg_free(m);
}

static void ui_chooser_forgot(void *ud)
{
    chooser_msg *m = ud;
    gwin *w = gwin_of(m->g, m->win);
    if (w && chooser_up(w)) {
        sk_chooser_forgot(w->chooser, &m->st);
        overlay_frame(w);
    }
    chooser_msg_free(m);
}

/* What the dialog asked for after an event: its pick to the core, its
 * close, its pointer and its input method's place. */
static void chooser_result(gwin *w, sk_chooser_result r)
{
    if (r.act == SK_CHOOSER_OPEN || r.act == SK_CHOOSER_FORGET) {
        chooser_msg *m = sk_xcalloc(1, sizeof *m);
        m->g = w->g;
        m->win = w->snap.id;
        sk_chooser_pick(w->chooser, &m->pick);
        sk_loop_post_owned(w->g->loop, r.act == SK_CHOOSER_OPEN ? do_chooser_open : do_chooser_forget, m,
                           drop_chooser);
    } else if (r.act == SK_CHOOSER_CLOSED) {
        chooser_closed(w);
        return;
    }
    if (chooser_up(w)) {
        set_cursor(w, sk_chooser_cursor(w->chooser));
        int x, y, cw, ch;
        if (sk_chooser_ime_rect(w->chooser, &x, &y, &cw, &ch))
            sk_window_set_ime_rect(w->win, x, y, cw, ch);
    }
    if (r.redraw || r.act != SK_CHOOSER_NONE)
        overlay_frame(w);
}

/* While the dialog is open it takes the keys, the pointer and the input
 * method's text, as the page's modal class kept them from the terminal
 * and the chords. True for an event it took. */
static bool chooser_event(gwin *w, const sk_event *ev)
{
    if (!chooser_up(w))
        return false;
    switch (ev->type) {
    case SK_EV_KEY_DOWN:
        chooser_result(w, sk_chooser_key(w->chooser, &ev->key));
        return true;
    case SK_EV_MOUSE_DOWN:
    case SK_EV_MOUSE_UP:
    case SK_EV_MOUSE_MOVE:
    case SK_EV_MOUSE_LEAVE:
    case SK_EV_SCROLL:
        chooser_result(w, sk_chooser_mouse(w->chooser, ev));
        return true;
    case SK_EV_TEXT:
        chooser_result(w, sk_chooser_text(w->chooser, ev->text ? ev->text : ""));
        return true;
    case SK_EV_PREEDIT:
        chooser_result(w, sk_chooser_preedit(w->chooser, ev->text ? ev->text : ""));
        return true;
    default:
        return false;
    }
}

/* ---- A tab's drop-down and rename (decision 0032) ---- */

static snap_tab *snap_tab_of(gwin *w, int id)
{
    for (size_t i = 0; i < w->snap.n; i++)
        if (w->snap.tabs[i].id == id)
            return &w->snap.tabs[i];
    return NULL;
}

/* The edit ends: committed, the name trimmed as String.trim trims it
 * goes to the core when it is not empty and not the tab's name. */
static void rename_finish(gwin *w, bool commit)
{
    if (!sk_rename_editing(w->rename))
        return;
    int id = sk_rename_tab(w->rename);
    const char *s = sk_rename_text(w->rename);
    size_t lo = 0, hi = strlen(s);
    while (lo < hi) {
        uint32_t cp;
        size_t k = sk_utf8_decode(s + lo, hi - lo, &cp, NULL);
        if (!k || !sk_js_space(cp))
            break;
        lo += k;
    }
    while (hi > lo) {
        size_t at = hi - 1;
        while (at > lo && ((unsigned char)s[at] & 0xC0) == 0x80)
            at--;
        uint32_t cp;
        sk_utf8_decode(s + at, hi - at, &cp, NULL);
        if (!sk_js_space(cp))
            break;
        hi = at;
    }
    snap_tab *t = snap_tab_of(w, id);
    if (commit && t && hi > lo && (strlen(t->name) != hi - lo || memcmp(t->name, s + lo, hi - lo))) {
        rename_msg *m = sk_xmalloc(sizeof *m + hi - lo + 1);
        m->g = w->g;
        m->id = id;
        memcpy(m->name, s + lo, hi - lo);
        m->name[hi - lo] = 0;
        sk_loop_post_owned(w->g->loop, do_rename, m, drop_free);
    }
    bool strip = tabs_shown(w);
    sk_rename_end(w->rename);
    w->ime_x = w->ime_y = -1;
    focus_term(w);
    /* The strip the edit held may go now, moving the stage. */
    if (strip != tabs_shown(w)) {
        fit(w);
        redraw_all(w);
    } else {
        redraw(w);
    }
}

/* The clipboard answers later, maybe after its window has gone: the
 * answer finds the window by its id. */
typedef struct {
    gui *g;
    int win;
    void (*fn)(gwin *w, const char *utf8);
} paste_req;

static void on_pasted(void *ud, const char *utf8)
{
    paste_req *r = ud;
    gwin *w = gwin_of(r->g, r->win);
    if (w)
        r->fn(w, utf8);
    free(r);
}

static void ask_clipboard(gwin *w, bool primary, void (*fn)(gwin *w, const char *utf8))
{
    paste_req *r = sk_xmalloc(sizeof *r);
    *r = (paste_req){w->g, w->snap.id, fn};
    sk_clipboard_get(primary, on_pasted, r);
}

static void rename_paste(gwin *w, const char *utf8)
{
    if (!sk_rename_editing(w->rename) || !utf8 || !*utf8)
        return;
    sk_field_insert(sk_rename_field(w->rename), utf8);
    redraw_strips(w);
}

/* A key's clipboard part, as the page's input had it. */
static void rename_clip(gwin *w, sk_field_key_result *r)
{
    if (r->copy)
        sk_clipboard_set(r->copy);
    free(r->copy);
    r->copy = NULL;
    if (r->paste)
        ask_clipboard(w, false, rename_paste);
    if (r->selected) {
        char *s = sk_field_selection(sk_rename_field(w->rename));
        sk_primary_set(s);
        free(s);
    }
}

/* The pointer while the drop-down or the field is up. True when the
 * event was theirs; false when it goes on to what is under it. */
static bool rename_mouse(gwin *w, const sk_event *ev)
{
    if (sk_rename_popup_open(w->rename)) {
        if (ev->type == SK_EV_MOUSE_MOVE && sk_rename_popup_hover(w->rename, ev->mouse.x, ev->mouse.y))
            overlay_frame(w);
        if (ev->type == SK_EV_MOUSE_DOWN) {
            bool rename, strip = tabs_shown(w);
            int id;
            bool on = sk_rename_popup_press(w->rename, ev->mouse.x, ev->mouse.y, ev->mouse.button, &rename, &id);
            overlay_frame(w);
            snap_tab *t = rename ? snap_tab_of(w, id) : NULL;
            if (t) {
                sk_rename_edit(w->rename, id, t->name);
                sk_term *term = active_term(w);
                if (term)
                    sk_term_set_focus(term, false);
                /* The field in the tab's place, and its caret for the
                 * input method, come with the strip's drawing. */
                redraw_strips(w);
            }
            /* The strip the drop-down held may go now, moving the stage. */
            if (strip != tabs_shown(w)) {
                fit(w);
                redraw_all(w);
            }
            if (on)
                return true;
        }
    }
    if (!sk_rename_editing(w->rename))
        return false;
    sk_menu_style st = menu_style(w);
    bool selected = false, primary = false;
    bool in_strip = tabs_shown(w) && ev->mouse.y >= IDENTITY_H && ev->mouse.y < IDENTITY_H + TABS_H;
    /* A press on the strip keeps the focus in the field, as the page's
     * preventDefault kept it, and does nothing else while it edits. */
    if (ev->type == SK_EV_MOUSE_DOWN && in_strip) {
        sk_rename_mouse(w->rename, ev, &st, &selected, &primary);
        if (selected)
            rename_clip(w, &(sk_field_key_result){true, NULL, false, true});
        if (primary)
            ask_clipboard(w, true, rename_paste);
        redraw_strips(w);
        return true;
    }
    size_t caret = sk_rename_field(w->rename)->caret;
    sk_rename_how how = sk_rename_mouse(w->rename, ev, &st, &selected, &primary);
    if (how == SK_RENAME_COMMIT) {
        rename_finish(w, true);
        return false;
    }
    if (selected)
        rename_clip(w, &(sk_field_key_result){true, NULL, false, true});
    if (ev->type != SK_EV_MOUSE_MOVE || selected || caret != sk_rename_field(w->rename)->caret)
        redraw_strips(w);
    return ev->type != SK_EV_MOUSE_MOVE || in_strip;
}

/* ---- The pointer on the skin ---- */

/* A message for the core of what the skin's pointer did. */
static void post_skin_act(gwin *w, sk_task_cb fn, int id, bool off, double size, const char *text)
{
    size_t n = text ? strlen(text) : 0;
    skin_act *m = sk_xcalloc(1, sizeof *m + n + 1);
    m->g = w->g;
    m->id = id;
    m->off = off;
    m->size = size;
    if (n)
        memcpy(m->text, text, n);
    sk_loop_post_owned(w->g->loop, fn, m, drop_free);
}

/* Whether (x, y) is on the skin's menu button, whose press opens or
 * closes the drop-down: never a press outside it. */
static bool on_button(gwin *w, double x, double y)
{
    if (!w->skin)
        return false;
    double r[4];
    sk_skinview_menu_rect(w->skin, r);
    return x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3];
}

/* The skin takes every pointer event first (view.h): true when it was
 * the skin's. Its button opens and closes the drop-down below it, a
 * readout its dial (decision 0033), a list's toggle what the list
 * replaces (decision 0034), a split's handle the split (decision 0035),
 * kept where it is let go. */
static bool skin_mouse(gwin *w, const sk_event *ev)
{
    if (!w->skin)
        return false;
    sk_skin_mouse what = ev->type == SK_EV_MOUSE_DOWN ? SK_SKIN_MOUSE_DOWN
                         : ev->type == SK_EV_MOUSE_UP ? SK_SKIN_MOUSE_UP
                                                      : SK_SKIN_MOUSE_MOVE;
    sk_skin_event e = sk_skinview_mouse(w->skin, what, ev->mouse.x, ev->mouse.y, ev->mouse.button);
    skin_frame(w);
    switch (e.action) {
    case SK_SKIN_MENU:
        if (sk_menu_open(w->menu)) {
            sk_menu_close(w->menu);
            menu_changed(w);
        } else {
            sk_skinview_menu_rect(w->skin, w->button);
            menu_ask(w, SK_MENU_FILE);
        }
        break;
    case SK_SKIN_DIAL:
        post_skin_act(w, do_dial, 0, false, 0, e.metric);
        break;
    case SK_SKIN_TOGGLE: {
        snap_tab *t = active_tab(w);
        if (t)
            post_skin_act(w, do_list_off, t->id, e.off, 0, NULL);
        sk_skinview_set_list_off(w->skin, e.off);
        break;
    }
    case SK_SKIN_SPLIT_MOVE:
        fit(w);
        redraw_all(w);
        break;
    case SK_SKIN_SPLIT_DONE:
        post_skin_act(w, do_panel, 0, false, e.size, e.split);
        fit(w);
        redraw_all(w);
        break;
    default:
        break;
    }
    return e.action != SK_SKIN_NOTHING;
}

static sk_dashview *active_dview(gwin *w);

/* The pointer's shape at (x, y): the skin's where it says one, else the
 * strips' arrow, on the dashboard a live row's hand and the arrow, else
 * a link's hand or the terminal's I-beam. */
static void pointer_shape(gwin *w, double x, double y)
{
    sk_skin_cursor c = w->skin ? sk_skinview_cursor(w->skin) : SK_SKIN_CURSOR_NONE;
    switch (c) {
    case SK_SKIN_CURSOR_ARROW:
        set_cursor(w, SK_CURSOR_ARROW);
        return;
    case SK_SKIN_CURSOR_POINTER:
        set_cursor(w, SK_CURSOR_POINTER);
        return;
    case SK_SKIN_CURSOR_TEXT:
        set_cursor(w, SK_CURSOR_IBEAM);
        return;
    case SK_SKIN_CURSOR_RESIZE_EW:
        set_cursor(w, SK_CURSOR_RESIZE_EW);
        return;
    case SK_SKIN_CURSOR_RESIZE_NS:
        set_cursor(w, SK_CURSOR_RESIZE_NS);
        return;
    default:
        break;
    }
    sk_dashview *dv = y >= top_points(w) ? active_dview(w) : NULL;
    if (y < top_points(w)) {
        set_cursor(w, SK_CURSOR_ARROW);
    } else if (dv) {
        double ox, oy;
        term_origin(w, &ox, &oy);
        set_cursor(w, sk_dashview_hit(dv, x - ox, y - oy) ? SK_CURSOR_POINTER : SK_CURSOR_ARROW);
    } else {
        set_cursor(w, w->hover_link || w->link_uri ? SK_CURSOR_POINTER : SK_CURSOR_IBEAM);
    }
}

/* ---- The visualization (decision 0015) ---- */

/* The feed's frame, 30 times a second: the window follows it, and the
 * tab strips' bands step from it, open or not. */
static void ui_viz_frame(void *ud)
{
    void **pair = ud;
    gui *g = pair[0];
    sk_vizfeed_msg *m = pair[1];
    free(pair);
    if (g->vizwin)
        sk_vizwin_frame(g->vizwin, m);
    if (g->tabviz) {
        sk_tabviz_feed(g->tabviz, m);
        uint64_t now = sk_now_ms();
        double dt = g->viz_at ? (double)(now - g->viz_at) / 1000 : 0;
        g->viz_at = now;
        if (sk_tabviz_step(g->tabviz, SK_MIN(dt, 0.1)))
            for (size_t i = 0; i < g->wins.len; i++)
                if (tabs_shown(g->wins.ptr[i]))
                    redraw_strips(g->wins.ptr[i]);
    }
    sk_vizfeed_msg_free(m);
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

/* How long the pointer rests before its hover text shows, as Chromium's
 * tooltips wait. */
#define TIP_MS 500

static void on_tip_due(void *ud)
{
    gwin *w = ud;
    w->tip_timer = 0;
    if (w->tip) {
        w->tip_shown = true;
        overlay_frame(w);
    }
}

/* The hover text under the pointer at (x, y) points, NULL for none: a
 * new one shows after TIP_MS, and the one shown goes at once. */
static void tip_set(gwin *w, const char *text, double x, double y)
{
    if (text && w->tip && !strcmp(text, w->tip))
        return;
    if (w->tip_timer)
        sk_ui_cancel(w->tip_timer);
    w->tip_timer = 0;
    if (w->tip_shown)
        overlay_frame(w);
    w->tip_shown = false;
    free(w->tip);
    w->tip = text ? sk_xstrdup(text) : NULL;
    w->tip_x = x;
    w->tip_y = y;
    if (w->tip)
        w->tip_timer = sk_ui_timer(TIP_MS, 0, on_tip_due, w);
}

/* The hover text's box, in device pixels: below the pointer, kept on
 * the frame. */
#define TIP_PX 12
#define TIP_PAD_X 6
#define TIP_PAD_Y 4
#define TIP_LINE 16

static sk_rect tip_box(gwin *w, const sk_image *f)
{
    double s = sk_window_scale(w->win);
    float tw = sk_measure_text(w->g->sans, SK_STYLE_REGULAR, (float)(TIP_PX * s), w->tip, strlen(w->tip));
    int bw = SK_MIN((int)ceil(tw + 2 * TIP_PAD_X * s), f->w), bh = (int)ceil((TIP_LINE + 2 * TIP_PAD_Y) * s);
    int x = (int)lround(w->tip_x * s), y = (int)lround((w->tip_y + 20) * s);
    if (y + bh > f->h)
        y = (int)lround((w->tip_y - 4) * s) - bh;
    x = SK_MAX(0, SK_MIN(x, f->w - bw));
    y = SK_MAX(0, y);
    return sk_rect_make(x, y, bw, bh);
}

static void tip_draw(gwin *w, sk_image *f)
{
    double s = sk_window_scale(w->win);
    sk_rect r = tip_box(w, f);
    sk_fill_rect(f, r, SK_RGB(0x5a, 0x5a, 0x5a), SK_BLEND_SRC_OVER, NULL);
    int b = SK_MAX(1, (int)lround(s));
    sk_fill_rect(f, sk_rect_make(r.x + b, r.y + b, r.w - 2 * b, r.h - 2 * b), SK_RGB(0x1f, 0x1f, 0x1f),
                 SK_BLEND_SRC_OVER, NULL);
    float px = (float)(TIP_PX * s), base = sk_baseline_in(w->g->sans, SK_STYLE_REGULAR, px, (float)r.y,
                                                           (float)r.h);
    sk_draw_fit(f, w->g->cache, w->g->sans, SK_STYLE_REGULAR, px, (float)(r.x + TIP_PAD_X * s), base,
                (float)(r.w - 2 * TIP_PAD_X * s), w->tip, SK_RGB(0xe8, 0xe8, 0xe8), false, &r);
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
    double ox, oy;
    term_origin(w, &ox, &oy);
    double x = ev->mouse.x - ox, y = ev->mouse.y - oy;
    if (ev->type == SK_EV_MOUSE_MOVE) {
        if (sk_dashview_hover(v, x, y))
            redraw(w);
        sk_buf tip = {0};
        tip_set(w, sk_dashview_title_at(v, x, y, &tip) ? tip.ptr : NULL, ev->mouse.x, ev->mouse.y);
        sk_buf_free(&tip);
    } else {
        tip_set(w, NULL, 0, 0);
    }
    if (ev->type == SK_EV_MOUSE_DOWN && ev->mouse.button == 1) {
        const char *key = sk_dashview_hit(v, x, y);
        if (key) {
            size_t n = strlen(key);
            act_msg *m = sk_xcalloc(1, sizeof *m + n + 1);
            m->g = w->g;
            m->win = w->snap.id;
            m->action = ACT_SESSION;
            memcpy(m->arg, key, n);
            sk_loop_post_owned(w->g->loop, do_action, m, drop_free);
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
        /* A name being edited is an input in place of the name and the
         * folder: Chromium's size 20, in the strip's 12-point mono, its
         * padding and border. */
        if (sk_rename_tab(w->rename) == t->id)
            pts = tab_fixed() + 20 * sk_measure_text(g->mono, 0, size, "0", 1) / s + 2 * 3 + 2;
        /* A tab with a session has its band (tabviz.js) after the mark. */
        if (t->kind != SK_TAB_DASHBOARD)
            pts += SK_TABVIZ_W + 6;
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
        if (t->kind != SK_TAB_DASHBOARD && g->tabviz) {
            /* The band, flex 0 1 44px and at least 22: when the tab is
             * narrower than its content, it gives up its share of what
             * is missing beside the name and the folder. */
            bool editing = sk_rename_tab(w->rename) == t->id;
            float named = editing ? (float)(20 * sk_measure_text(g->mono, 0, size, "0", 1) + 8 * s)
                                  : sk_measure_text(g->mono, 0, size, t->name, strlen(t->name));
            if (t->folder && !editing)
                named += (float)(6 * s) + sk_measure_text(g->mono, 0, size, t->folder, strlen(t->folder));
            float full = (float)(SK_TABVIZ_W * s), between = (float)(6 * s);
            float missing = full + between + named - room, bw = full;
            if (missing > 0)
                bw = SK_MAX((float)(SK_TABVIZ_MIN_W * s), full - missing * full / (full + named));
            sk_rect band = sk_rect_make((int)lroundf(tx), r.y + (r.h - px(w, SK_TABVIZ_H)) / 2, (int)lroundf(bw),
                                        px(w, SK_TABVIZ_H));
            sk_tabviz_draw(g->tabviz, t->id, f, band, s, &r);
            tx += bw + between;
            room -= bw + between;
        }
        if (sk_rename_tab(w->rename) == t->id) {
            /* The input, 18 points high in the strip's line box. */
            sk_menu_style ms = menu_style(w);
            sk_rename_place(w->rename, tx / s, IDENTITY_H + (TABS_H - 1 - 18) / 2.0, SK_MAX(40.0, room / s), 18);
            sk_rename_draw(w->rename, f, &ms);
        } else {
            float fw = t->folder ? sk_measure_text(g->mono, 0, size, t->folder, strlen(t->folder)) : 0;
            float name_w = draw_fit(g, f, 0, size, tx, tbase,
                                    t->folder ? SK_MAX(room - fw - (float)(6 * s), 0) : room, t->name, text);
            if (t->folder)
                draw_fit(g, f, 0, size, tx + name_w + (float)(6 * s), tbase, room - name_w - (float)(6 * s),
                         t->folder, SK_RGB(0x77, 0x77, 0x77));
        }
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

#define DIRTY_MAX 48

/* What the drop-downs and the dialog cover now, shadows included, in
 * the frame's pixels; empty with none up. */
static sk_rect overlay_bounds(gwin *w, const sk_image *f)
{
    double s = sk_window_scale(w->win);
    sk_rect r = sk_rect_make(0, 0, 0, 0);
    if (sk_menu_open(w->menu))
        r = sk_rect_union(r, sk_menu_bounds(w->menu, s));
    if (sk_rename_popup_open(w->rename))
        r = sk_rect_union(r, sk_rename_popup_bounds(w->rename, s));
    if (chooser_up(w)) {
        int top = px(w, IDENTITY_H);
        r = sk_rect_union(r, sk_rect_make(0, top, f->w, SK_MAX(0, f->h - top)));
    }
    if (w->tip_shown && w->tip && active_dview(w))
        r = sk_rect_union(r, tip_box(w, f));
    return sk_rect_intersect(r, sk_image_bounds(f));
}

static void copy_rect(sk_image *to, const sk_image *from, sk_rect r)
{
    r = sk_rect_intersect(r, sk_image_bounds(from));
    if (!sk_rect_empty(r))
        sk_blit(to, r.x, r.y, from, r, SK_BLEND_SRC, 255, NULL);
}

static void draw(gwin *w)
{
    gui *g = w->g;
    sk_image *f = sk_window_frame(w->win);
    sk_rect dirty[DIRTY_MAX];
    size_t nd = 0;
    bool menu = sk_menu_open(w->menu), chooser = chooser_up(w), popup = sk_rename_popup_open(w->rename);
    bool editing = sk_rename_editing(w->rename), tip = w->tip_shown && w->tip && active_dview(w);
    bool overlay = menu || chooser || popup || tip;
    /* Below the drop-downs and the dialog everything is drawn into the
     * window's base, and copied into the frame where it changed and
     * under them, so that one open over the skin asks no whole skin a
     * frame. They gone, the base is the frame again first: what draws
     * below draws over what it drew last. */
    if (!overlay && w->base.px) {
        if (w->base.w == f->w && w->base.h == f->h && !w->all) {
            copy_rect(f, &w->base, sk_image_bounds(f));
            if (!sk_rect_empty(w->overlay))
                dirty[nd++] = w->overlay;
        } else {
            w->all = true;
        }
        sk_image_free(&w->base);
        memset(&w->base, 0, sizeof w->base);
        w->overlay = sk_rect_make(0, 0, 0, 0);
    }
    if (overlay && (!w->base.px || w->base.w != f->w || w->base.h != f->h)) {
        sk_image_free(&w->base);
        sk_image_init(&w->base, f->w, f->h);
        copy_rect(&w->base, f, sk_image_bounds(f));
        w->overlay = sk_rect_make(0, 0, 0, 0);
    }
    sk_image *dst = overlay ? &w->base : f;
    double due = w->skin ? sk_skinview_step(w->skin, (double)sk_now_ms()) : -1;
    /* whole: everything, the skin too; scene: the strips and the terminal
     * whole, the skin left to its own damage. */
    bool whole = w->all, scene = w->all || w->term_all;
    if (scene) {
        draw_chrome(w, dst);
        /* Without a skin the stage is the page's black around the
         * terminal. */
        if (!w->skin)
            sk_fill_rect(dst, stage_area(w), SK_RGB(0, 0, 0), SK_BLEND_SRC, NULL);
        if (!whole)
            dirty[nd++] = sk_rect_make(0, 0, f->w, px(w, top_points(w)));
    } else if (w->strips) {
        draw_chrome(w, dst);
        dirty[nd++] = sk_rect_make(0, 0, f->w, px(w, top_points(w)));
    }
    w->strips = false;
    /* The skin, never inside the terminal's rect, the terminal over it. */
    if (w->skin)
        nd += sk_skinview_draw(w->skin, dst, whole, dirty + nd, DIRTY_MAX - 5 - nd);
    sk_term *t = active_term(w);
    snap_tab *at = active_tab(w);
    if (t && at->kind == SK_TAB_DASHBOARD) {
        /* Drawn whole each time: every view and hover asks for it. */
        if (scene) {
            draw_dash(w, dst, t);
            if (!whole)
                dirty[nd++] = term_area(w);
        }
        t = NULL;
    }
    if (t) {
        sk_rect a = term_area(w);
        sk_image area = sk_image_view(dst, a);
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
            if (scene) {
                sk_term_palette p;
                sk_term_palette_of(t, &p);
                sk_fill_rect(&area, sk_image_bounds(&area), (sk_color)(0xff000000u | p.bg), SK_BLEND_SRC, NULL);
            }
            /* The terminal has the focus when its window has it and no
             * field of the app's does. */
            sk_termview_state st = {
                .focused = w->focused && !editing && !chooser,
                .cursor_on = w->cursor_on,
                .link = w->hover_link,
                .link_row0 = w->link_r0,
                .link_col0 = w->link_c0,
                .link_row1 = w->link_r1,
                .link_col1 = w->link_c1,
            };
            sk_rect r = sk_termview_draw(w->view, t, &area, &st, scene);
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
        int ix = (int)lround((cx + a.x) / s), iy = (int)lround((cy + a.y) / s);
        if (!chooser && !editing && (ix != w->ime_x || iy != w->ime_y)) {
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
    if (overlay) {
        /* The base into the frame where it changed and under the
         * overlays as they were and are, then the overlays over it. */
        sk_rect now = overlay_bounds(w, f), under = sk_rect_union(now, w->overlay);
        if (whole) {
            copy_rect(f, &w->base, sk_image_bounds(f));
        } else {
            for (size_t i = 0; i < nd; i++)
                copy_rect(f, &w->base, dirty[i]);
            copy_rect(f, &w->base, under);
            if (!sk_rect_empty(under))
                dirty[nd++] = under;
        }
        w->overlay = now;
        if (menu) {
            sk_menu_style st = menu_style(w);
            sk_menu_draw(w->menu, f, &st);
        }
        /* New tab... darkens everything below the identity strip. */
        if (chooser) {
            sk_chooser_style st = {g->sans, g->mono, g->cache, sk_window_scale(w->win)};
            int top = px(w, IDENTITY_H);
            sk_chooser_draw(w->chooser, f, sk_rect_make(0, top, f->w, SK_MAX(0, f->h - top)), &st);
        }
        if (popup) {
            sk_menu_style ms = menu_style(w);
            sk_rename_popup_draw(w->rename, f, &ms);
        }
        if (tip)
            tip_draw(w, f);
    }
    /* A name being edited has the input method's candidates. */
    if (editing) {
        sk_menu_style ms = menu_style(w);
        int ix, iy, iw, ih;
        sk_rename_caret(w->rename, &ms, &ix, &iy, &iw, &ih);
        if (ix != w->ime_x || iy != w->ime_y) {
            w->ime_x = ix;
            w->ime_y = iy;
            sk_window_set_ime_rect(w->win, ix, iy, iw, ih);
        }
    }
    if (whole)
        sk_window_present(w->win, NULL, 0);
    else if (nd)
        sk_window_present(w->win, dirty, nd);
    w->all = w->term_all = false;
    /* The skin's next step: at the next refresh while something moves,
     * else at its next timed change; a set or the pointer asks too. */
    if (due == 0) {
        skin_frame(w);
    } else if (due > 0) {
        /* A sooner change than the timer waits for takes its place. */
        uint64_t when = sk_now_ms() + (uint64_t)ceil(due);
        if (w->skin_timer && when < w->skin_due) {
            sk_ui_cancel(w->skin_timer);
            w->skin_timer = 0;
        }
        if (!w->skin_timer) {
            w->skin_timer = sk_ui_timer((uint64_t)ceil(due), 0, on_skin_timer, w);
            w->skin_due = when;
        }
    }
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

static void term_paste(gwin *w, const char *utf8)
{
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
        ask_clipboard(w, false, term_paste);
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
        ask_clipboard(w, false, term_paste);
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
    /* A name being edited takes the keys; the chords wait. */
    if (sk_rename_editing(w->rename)) {
        sk_field_key_result clip;
        sk_rename_how how = sk_rename_key(w->rename, k, &clip);
        rename_clip(w, &clip);
        if (how != SK_RENAME_GOES_ON)
            rename_finish(w, how == SK_RENAME_COMMIT);
        else
            redraw_strips(w);
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
    double ox, oy;
    term_origin(w, &ox, &oy);
    *gx = x - ox;
    *gy = y - oy;
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
    sk_loop_post_owned(g->loop, do_open, m, drop_free);
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
 * click or its close box closes it, + opens the New tab... chooser
 * (decision 0025), a drag moves it, and a release outside the window
 * pops it out (decision 0023). */
static bool strip_mouse(gwin *w, const sk_event *ev)
{
    gui *g = w->g;
    int win = w->snap.id;
    bool on_close;
    if (ev->type == SK_EV_MOUSE_DOWN) {
        int i = strip_hit(w, ev->mouse.x, ev->mouse.y, &on_close);
        if (i < 0)
            return true;
        int button = ev->mouse.button;
#ifdef __APPLE__
        /* A Ctrl+click is macOS's right click: Chromium sends it as one. */
        if (button == 1 && ev->mouse.mods & SK_MOD_CTRL)
            button = 3;
#endif
        if ((size_t)i == w->snap.n) {
            if (button == 1)
                chooser_ask(w);
        } else if (button == 3) {
            /* The tab's drop-down at the pointer (decision 0032). */
            if (!sk_rename_editing(w->rename)) {
                int ww, wh;
                sk_window_size(w->win, &ww, &wh);
                sk_rename_popup(w->rename, w->snap.tabs[i].id, ev->mouse.x, ev->mouse.y, ww);
                redraw(w);
            }
        } else if (button == 2 || (on_close && button == 1)) {
            post_tab(g, do_close, w->snap.tabs[i].id, win);
        } else if (button == 1) {
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
     * and a press elsewhere closes it and goes on to what is there, but
     * on the skin's button, which closes it itself. */
    bool button_press = ev->type == SK_EV_MOUSE_DOWN && on_button(w, ev->mouse.x, ev->mouse.y);
    if (sk_menu_open(w->menu) && !button_press) {
        if (ev->type == SK_EV_MOUSE_MOVE && sk_menu_mouse_move(w->menu, ev->mouse.x, ev->mouse.y))
            overlay_frame(w);
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
    if (rename_mouse(w, ev))
        return;
    /* The skin next, but not while the terminal's selection or a tab's
     * drag holds the pointer. */
    if (!w->selecting && !w->drag_tab && !w->term_press && skin_mouse(w, ev)) {
        pointer_shape(w, ev->mouse.x, ev->mouse.y);
        return;
    }
    if (ev->type == SK_EV_MOUSE_MOVE)
        pointer_shape(w, ev->mouse.x, ev->mouse.y);
    sk_term *t = active_term(w);
    double gx, gy;
    grid_point(w, ev->mouse.x, ev->mouse.y, &gx, &gy);
    /* The strips above the stage are the app's: a press there is never
     * the terminal's. */
    bool chrome = ev->mouse.y < top_points(w);
    if (ev->type == SK_EV_MOUSE_MOVE && !w->selecting && track_hover(w, ev->mouse.x, ev->mouse.y))
        redraw_strips(w);
    if (w->drag_tab || (ev->type == SK_EV_MOUSE_DOWN && chrome && !w->selecting)) {
        if (strip_mouse(w, ev))
            return;
    }
    /* The pointer in the strips leaves the dashboard's row unlit, and
     * its hover text gone. */
    if (chrome && ev->type == SK_EV_MOUSE_MOVE) {
        sk_dashview *dv = active_dview(w);
        if (dv && sk_dashview_hover(dv, -1e9, -1e9))
            redraw(w);
        tip_set(w, NULL, 0, 0);
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
        /* The terminal holds the pointer until the release. */
        w->term_press = true;
        if (sk_term_mouse_down(&w->mouse, &m, &grid, gx, gy, ev->mouse.button, ev->mouse.mods, &out))
            break;
#ifndef __APPLE__
        /* X11's middle click pastes PRIMARY, as Chromium pastes it into
         * xterm.js's textarea. */
        if (ev->mouse.button == 2) {
            ask_clipboard(w, true, term_paste);
            break;
        }
#endif
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
            set_cursor(w, link || w->link_uri ? SK_CURSOR_POINTER : SK_CURSOR_IBEAM);
            changed = true;
        }
        break;
    }
    case SK_EV_MOUSE_UP:
        w->term_press = false;
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
        overlay_frame(w);
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
    /* The visualization's window is its own, and no gwin. */
    if (g->vizwin && sk_vizwin_event(g->vizwin, ev))
        return;
    gwin *w = ev->win ? sk_window_ud(ev->win) : NULL;
    if (!w)
        return;
    /* A release ends the terminal's hold on the pointer, whoever took
     * it. */
    if (chooser_event(w, ev)) {
        if (ev->type == SK_EV_MOUSE_UP)
            w->term_press = false;
        return;
    }
    switch (ev->type) {
    case SK_EV_CLOSE:
        /* As closing a window does (decision 0016): its tabs hang up,
         * and the app ends with the last tab. Several ask first. */
        if (w->snap.n >= 2 && !g->hidden)
            ask_close(w);
        else
            post_tab(g, do_close_win, 0, w->snap.id);
        break;
    case SK_EV_RESIZE: {
        double s = sk_window_scale(w->win);
        if (s != w->style.scale) {
            w->style.scale = s;
            sk_termview_set_style(w->view, &w->style);
        }
        fit(w);
        redraw_all(w);
        break;
    }
    case SK_EV_FRAME:
        draw(w);
        break;
    case SK_EV_FOCUS:
        w->focused = ev->focused;
        /* The window's blur is the input's too, which commits. */
        if (!ev->focused)
            rename_finish(w, true);
        focus_term(w);
        if (ev->focused)
            post_tab(g, do_touch, 0, w->snap.id);
        redraw(w);
        break;
    case SK_EV_KEY_DOWN:
        tip_set(w, NULL, 0, 0);
        on_key(w, &ev->key);
        break;
    case SK_EV_MOUSE_DOWN:
    case SK_EV_MOUSE_UP:
    case SK_EV_MOUSE_MOVE:
        on_mouse(w, ev);
        if (ev->type == SK_EV_MOUSE_UP)
            w->term_press = false;
        break;
    case SK_EV_MOUSE_LEAVE: {
        tip_set(w, NULL, 0, 0);
        if (w->skin) {
            sk_skinview_mouse(w->skin, SK_SKIN_MOUSE_LEAVE, 0, 0, 0);
            skin_frame(w);
        }
        sk_dashview *dv = active_dview(w);
        if (dv && sk_dashview_hover(dv, -1e9, -1e9))
            redraw(w);
        if (w->hover_link || w->link_r0 >= 0 || w->hover_tab >= 0) {
            clear_hover(w);
            w->hover_tab = -1;
            redraw(w);
        }
        break;
    }
    case SK_EV_SCROLL:
        on_scroll(w, ev);
        break;
    case SK_EV_TEXT: {
        /* What an input method commits is typed, as xterm.js sends a
         * composition's end. */
        snap_tab *at = active_tab(w);
        if (sk_menu_open(w->menu))
            break;
        if (sk_rename_editing(w->rename)) {
            if (ev->text && *ev->text) {
                sk_field_insert(sk_rename_field(w->rename), ev->text);
                redraw_strips(w);
            }
            break;
        }
        if (at && ev->text && *ev->text) {
            sk_term_scroll_to_bottom(at->st->term);
            sk_term_select_clear(at->st->term);
            post_input(g, at->id, ev->text, strlen(ev->text));
        }
        break;
    }
    case SK_EV_PREEDIT:
        /* A composition is the terminal's only while it has the keys. */
        free(w->preedit);
        w->preedit = NULL;
        if (ev->text && *ev->text && !sk_rename_editing(w->rename) && !sk_menu_open(w->menu))
            w->preedit = sk_xstrdup(ev->text);
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
    /* The grid's top-left and a cell, in points. */
    double ox, oy;
    term_origin(w, &ox, &oy);
    int cw, ch;
    sk_termview_cell(w->view, &cw, &ch);
    sk_jw_key(jw, SK_STR("grid"));
    sk_jw_array(jw);
    sk_jw_number(jw, ox);
    sk_jw_number(jw, oy);
    sk_jw_number(jw, cw / sk_window_scale(w->win));
    sk_jw_number(jw, ch / sk_window_scale(w->win));
    sk_jw_array_end(jw);
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
    /* The skin: its name, where its button and the terminal are. */
    sk_jw_key(&jw, SK_STR("skin"));
    if (w && w->skin) {
        double r[4];
        sk_jw_object(&jw);
        sk_jw_key(&jw, SK_STR("name"));
        sk_jw_string(&jw, sk_str_c(sk_skinpack_name(g->pack)));
        sk_jw_key(&jw, SK_STR("button"));
        sk_skinview_menu_rect(w->skin, r);
        sk_jw_array(&jw);
        for (int i = 0; i < 4; i++)
            sk_jw_number(&jw, r[i]);
        sk_jw_array_end(&jw);
        sk_jw_key(&jw, SK_STR("terminal"));
        sk_skinview_terminal_rect(w->skin, r);
        sk_jw_array(&jw);
        for (int i = 0; i < 4; i++)
            sk_jw_number(&jw, r[i]);
        sk_jw_array_end(&jw);
        sk_jw_key(&jw, SK_STR("muted"));
        sk_jw_bool(&jw, g->muted);
        sk_jw_object_end(&jw);
    } else {
        sk_jw_null(&jw);
    }
    /* The visualization's window: open, and a pane a tab. */
    sk_jw_key(&jw, SK_STR("viz"));
    if (g->vizwin) {
        sk_jw_object(&jw);
        sk_jw_key(&jw, SK_STR("open"));
        sk_jw_bool(&jw, sk_vizwin_is_open(g->vizwin));
        sk_jw_key(&jw, SK_STR("panes"));
        sk_jw_array(&jw);
        for (size_t i = 0; i < sk_vizwin_panes(g->vizwin); i++)
            sk_jw_int(&jw, sk_vizwin_pane_tab(g->vizwin, i));
        sk_jw_array_end(&jw);
        sk_jw_object_end(&jw);
    } else {
        sk_jw_null(&jw);
    }
    /* A tab's drop-down and the name being edited (decision 0032). */
    sk_jw_key(&jw, SK_STR("rename"));
    if (w) {
        sk_jw_object(&jw);
        sk_jw_key(&jw, SK_STR("popup"));
        sk_jw_bool(&jw, sk_rename_popup_open(w->rename));
        sk_jw_key(&jw, SK_STR("editing"));
        sk_jw_int(&jw, sk_rename_tab(w->rename));
        sk_jw_key(&jw, SK_STR("text"));
        sk_jw_string(&jw, sk_str_c(sk_rename_text(w->rename)));
        sk_jw_object_end(&jw);
    } else {
        sk_jw_null(&jw);
    }
    /* New tab..., and where its controls are. */
    sk_jw_key(&jw, SK_STR("chooser"));
    if (w && w->chooser) {
        sk_chooser_json(w->chooser, &jw);
        sk_jw_key(&jw, SK_STR("chooser_boxes"));
        sk_jw_object(&jw);
        static const char *const boxes[] = {"line", "folder", "profile", "cancel", "open", "error"};
        for (size_t i = 0; i < SK_LEN(boxes); i++) {
            double box[4];
            if (!sk_chooser_box(w->chooser, boxes[i], box))
                continue;
            sk_jw_key(&jw, sk_str_c(boxes[i]));
            sk_jw_array(&jw);
            for (int k = 0; k < 4; k++)
                sk_jw_number(&jw, box[k]);
            sk_jw_array_end(&jw);
        }
        sk_jw_object_end(&jw);
    } else {
        sk_jw_null(&jw);
    }
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
static void drop_control(void *ud)
{
    ctl_msg *m = ud;
    if (m->req)
        sk_control_drop(m->req);
    sk_buf_free(&m->msg);
    sk_buf_free(&m->core);
    free(m);
}

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
    /* For the live checks: an internal error, as a broken invariant
     * would make one. */
    if (sk_str_eq_c(op, "die"))
        sk_die("asked to by the control socket");
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
        /* Housekeeping's problems, by title. */
        sk_jw_key(&jw, SK_STR("problems"));
        sk_jw_array(&jw);
        for (size_t i = 0; i < g->problems.len; i++)
            sk_jw_string(&jw, sk_str_c(g->problems.ptr[i].title));
        sk_jw_array_end(&jw);
        sk_jw_object_end(&jw);
        /* The object's members alone, comma first, for the UI to wrap. */
        sk_buf_append_c(&m->core, ",");
        if (obj.len > 2)
            sk_buf_append(&m->core, obj.ptr + 1, obj.len - 2);
        sk_buf_free(&obj);
    }
    sk_ui_post_owned(ui_control, m, drop_control);
}

/* ---- The run ---- */

/* The repository's skins/ beside the build the program runs from
 * (build/<mode>/skinned): opening one of its folders restores that
 * shipped skin. NULL where there is none. */
static char *shipped_dir(const char *self)
{
    char whole[PATH_MAX];
    if (!self || !strchr(self, '/') || !realpath(self, whole))
        return NULL;
    for (int up = 0; up < 3; up++) {
        char *slash = strrchr(whole, '/');
        if (!slash || slash == whole)
            return NULL;
        *slash = 0;
    }
    char *dir = sk_path_join(whole, "skins");
    if (!sk_files_is_dir(dir)) {
        free(dir);
        return NULL;
    }
    return dir;
}

/* Windows that recorded different skins within SK_LAST_RACE_MS of the
 * newest (decision 0022): which one this launch opens, asked as main.js
 * asks it, each shown with ~ for the home directory, the newest the
 * default. The live checks open no dialog: the newest is taken. Either
 * way the records are forgotten, so the next launch is not asked. */
static const char *raced_choice(gui *g, const sk_strv *choices, const char *home)
{
    int k = -1;
    if (!g->hidden) {
        sk_strv shown = {0};
        for (size_t i = 0; i < choices->len; i++) {
            sk_buf b = {0};
            sk_identity_tildify(&b, choices->ptr[i], home);
            sk_strv_push(&shown, b.ptr);
            sk_buf_free(&b);
        }
        k = sk_ask_choice("claude-skins", "Windows chose different skins at nearly the same time.",
                          "Which skin should this window open?", (const char *const *)shown.ptr, shown.len);
        sk_strv_free(&shown);
    }
    if (k < 0) {
        sk_log("windows chose different skins at once: %s, the newest, is taken", choices->ptr[0]);
        k = 0;
    }
    if (sk_last_forget(g->last_dir) != 0)
        sk_log("last skin: %s", strerror(errno));
    return choices->ptr[k];
}

/* text and a newline on stdout, past short writes. */
static void echo_screen(const char *text)
{
    sk_buf b = {0};
    sk_buf_printf(&b, "%s\n", text);
    for (size_t done = 0; done < b.len;) {
        ssize_t n = write(1, b.ptr + done, b.len - done);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            sk_log("last screen: %s", n < 0 ? strerror(errno) : "short write");
            break;
        }
        done += (size_t)n;
    }
    sk_buf_free(&b);
}

int sk_gui_run(const sk_options *o, const sk_envp *env, const char *self)
{
    sk_launch l;
    int status = sk_launch_open(&l, o, env, self, NULL);
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
    g->font_setting = font_setting(g);
    g->muted = o->mute;
    g->last_dir = sk_last_dir(&g->paths);
    g->profiles_dir = sk_profiles_dir(&g->paths);

    /* The skin before any window, which takes its size (decisions 0006,
     * 0008, 0022): --skin or SKINNED_SKIN, refused with the app's status;
     * else the newest record of the last skins, a remembered skin that
     * no longer loads giving way to the default, as a problem; windows
     * that chose different skins at once ask which. A launch records its
     * skin, a fallback is not. */
    char *skinc = sk_skinc_beside(self, sk_envp_get(env, "PATH"));
    char *shipped = shipped_dir(self);
    sk_skins_opts so = {skinc, NULL, shipped, sk_envp_get(env, "HOME")};
    g->skins = sk_skins_new(&so);
    free(skinc);
    free(shipped);
    sk_buf skin_errs = {0};
    char *problem_title = NULL, *problem_detail = NULL;
    bool started, remember = false;
    if (o->skin) {
        started = remember = sk_skins_start(g->skins, o->skin, &skin_errs);
    } else {
        sk_last_records records = {0};
        sk_strv choices = {0};
        const char *spec = NULL;
        if (g->last_dir) {
            sk_last_read(g->last_dir, &records);
            sk_last_prune(g->last_dir, &records, SK_LAST_RACE_MS);
        }
        if (sk_last_pick(&records, SK_LAST_RACE_MS, &spec, &choices) && !spec && choices.len)
            spec = raced_choice(g, &choices, sk_envp_get(env, "HOME"));
        started = sk_skins_start_remembered(g->skins, spec, &problem_title, &problem_detail, &skin_errs);
        /* A remembered skin refused, for Housekeeping. */
        bool refused = problem_title != NULL;
        if (refused)
            add_problem(g, problem_title, problem_detail);
        free(problem_title);
        free(problem_detail);
        remember = started && spec && !refused;
        sk_strv_free(&choices);
        sk_last_records_free(&records);
    }
    if (!started) {
        for (char *line = skin_errs.ptr, *nl; line && *line; line = nl ? nl + 1 : NULL) {
            nl = strchr(line, '\n');
            if (nl)
                *nl = 0;
            sk_tell("%s", line);
        }
        sk_buf_free(&skin_errs);
        goto out;
    }
    sk_buf_free(&skin_errs);
    if (remember)
        remember_skin(g);
    skin_palette(g);
    /* The windows the skin's, with the split sizes kept for it, and
     * its sounds through one mixer for every window's player. */
    g->sound_muted = o->mute;
    g->mixer = sk_mixer_new(48000);
    skin_msg *first_skin = panels_msg(g, sk_skinpack_name(sk_skins_current(g->skins)));
    ui_take_skin(g, sk_skins_current(g->skins), first_skin);
    drop_skin(first_skin);

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
    g->cli.screen = tab_screen;
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
    g->dash.dials = &g->dials;
    g->dash.ud = g;
    /* New tab... (decision 0025). */
    g->newtab.tabs = g->tabs;
    g->newtab.foreign = g->foreign;
    g->newtab.cmd = o->cmd;
    g->newtab.env = env;
    g->newtab.home = tc.home;
    g->newtab.cwd = l.cwd;
    g->newtab.profiles_dir = g->profiles_dir;
    g->newtab.log = log_line;
    g->newtab.ud = g;
    /* The visualization (decision 0015): its feed on the core thread,
     * open at launch unless --no-viz, its window and the tab bands on
     * this one. */
    sk_vizfeed_config vc;
    memset(&vc, 0, sizeof vc);
    char *mode_dir = sk_vizfeed_mode_dir(&g->paths), *input_dir = sk_vizfeed_input_dir(&g->paths);
    vc.loop = g->loop;
    vc.mode_dir = mode_dir;
    vc.input_dir = input_dir;
    vc.frame = viz_frame;
    vc.log = log_line;
    vc.ud = g;
    g->viz = sk_vizfeed_new(&vc);
    free(mode_dir);
    free(input_dir);
    if (o->viz)
        sk_vizfeed_open(g->viz);
    sk_vizwin_config wc;
    memset(&wc, 0, sizeof wc);
    wc.mono = g->mono;
    wc.cache = g->cache;
    wc.feed = g->viz;
    wc.title = "visualization: skinned";
    wc.hidden = g->hidden;
    g->vizwin = sk_vizwin_new(&wc);
    g->tabviz = sk_tabviz_new((uint64_t)getpid() ^ sk_now_ms());
    sk_loop_signal(g->loop, SIGINT, on_stop, g);
    sk_loop_signal(g->loop, SIGTERM, on_stop, g);
    sk_loop_signal(g->loop, SIGHUP, on_stop, g);
    sk_loop_timer(g->loop, POLL_MS, POLL_MS, on_strip_poll, g);
    /* The skin's metrics ten times a second (main.js's), and a skin
     * opened by path watched once a second (decision 0037). */
    sk_loop_timer(g->loop, 100, 100, on_metrics, g);
    sk_loop_timer(g->loop, 1000, 1000, on_skins_poll, g);
    /* A check that asked for the socket and cannot have it would wait
     * on a window it cannot reach: the app ends instead. */
    sk_control *ctl = NULL;
    const char *control = sk_envp_get(env, "SKINNED_CONTROL");
    if (control && *control && !(ctl = sk_control_open(g->loop, control, on_control, g, &err))) {
        sk_tell("%s", err);
        free(err);
        sk_vizfeed_free(g->viz);
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
            /* A window that ends because its last CLI exited by itself
             * leaves that CLI's last screen in the terminal it was
             * started from, as the CLI would have left it (decision
             * 0030). */
            const char *last = sk_tabs_done(g->tabs) ? sk_tabs_last_screen(g->tabs) : NULL;
            if (last && isatty(1))
                echo_screen(last);
        }
    }
    /* The feed reads the tabs' buses, and the tabs' watch agents are the
     * foreign sessions': the feed goes first, then the tabs. */
    sk_vizfeed_free(g->viz);
    g->viz = NULL;
    sk_tabs_free(g->tabs);
    sk_boards_free(g->boards);
    sk_foreign_free(g->foreign);
    for (size_t i = 0; i < g->ctabs.len; i++)
        st_unref(g->ctabs.ptr[i].st);
    sk_vec_free(&g->ctabs);
    sk_vec_free(&g->cwins);
    free(g->strip_key);
    sk_control_close(ctl);
    sk_loop_free(g->loop);

out:
    while (g->wins.len)
        gwin_free(g->wins.ptr[--g->wins.len]);
    sk_vec_free(&g->wins);
    sk_vizwin_free(g->vizwin);
    sk_tabviz_free(g->tabviz);
    for (size_t i = 0; i < g->dviews.len; i++)
        sk_dashview_free(g->dviews.ptr[i].v);
    sk_vec_free(&g->dviews);
    /* The players went with their windows: the output, then the mixer. */
    sk_audio_close(g->audio);
    sk_mixer_free(g->mixer);
    /* The cache keys glyphs by face, so it goes before the faces do:
     * the terminals' set's, the skin's own among them, and the app's. */
    sk_glyph_cache_free(g->term_cache);
    sk_fontset_free(g->term_fonts);
    sk_skinpack_unref(g->pack);
    for (size_t i = 0; i < g->panel_ids.len; i++)
        free(g->panel_ids.ptr[i]);
    sk_vec_free(&g->panel_ids);
    sk_vec_free(&g->panel_sizes);
    sk_strv_free(&g->ui_dials);
    sk_strv_free(&g->dials);
    sk_skins_free(g->skins);
    for (size_t i = 0; i < g->problems.len; i++) {
        free(g->problems.ptr[i].title);
        free(g->problems.ptr[i].detail);
    }
    sk_vec_free(&g->problems);
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
