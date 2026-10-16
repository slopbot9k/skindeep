/* The sessions dashboard (decision 0028), the session of its tab, as
 * app/dashboard.js and makeDashboard and openDashboard of app/main.js
 * have it. It has no pty and no child, its pid is 0, and it ends with no
 * status of its own (0), which reaches sk_tabs_ended from the loop.
 * While it runs it samples every live session every interval into an
 * sk_history and sends the page what to draw. A session in a tab is read
 * from that tab's agent and bus. Any other gets a watch agent
 * (sk_foreign_watch) on a bus of its own, closed and freed when the
 * session ends, when a tab takes it, or when the dashboard ends. Each
 * row also carries the issue board of its session's project (decision
 * 0029), and each dial open is a column (decision 0033).
 *
 * What the JavaScript sent its view goes to the config's callbacks; the
 * view is the window's. The strip a page load wants is the window's to
 * ask for, with sk_dashboard_strip.
 *
 * Every call is made on one thread, the core's. */
#ifndef SK_APP_DASHBOARD_H
#define SK_APP_DASHBOARD_H

#include "../sessions/board.h"
#include "../sessions/history.h"
#include "foreign.h"

/* The app's, shared by its dashboard tabs; every pointer outlives them. */
typedef struct {
    sk_loop *loop;
    sk_tabs *tabs; /* set once the tabs are made, before a tab is */
    /* 0 takes the default: SK_HISTORY_INTERVAL_MS, SK_HISTORY_POINTS and
     * SK_HISTORY_KEEP_ENDED_MS. */
    uint64_t interval_ms;
    size_t points;
    double keep_ended_ms;
    /* The app's dials (decision 0033), read when a dashboard is made;
     * NULL: none. */
    const sk_strv *dials;
    /* The menu's live sessions now, as sk_foreign_sessions lists them.
     * Required. The dashboard frees them. */
    void (*list)(void *ud, sk_foreign_entries *out);
    /* The watch agent of a session elsewhere on bus, as sk_foreign_watch
     * makes it. Required. The dashboard prepares, attaches, closes and
     * frees it. */
    sk_agent *(*watcher)(void *ud, const sk_foreign_session *s, sk_bus *bus);
    /* The issue board of a session's folder, as sk_boards_get gives it.
     * NULL: none. */
    void (*board)(void *ud, const char *folder, sk_board *out);
    /* ms since the epoch, each tick's time; NULL: the real clock. */
    double (*now)(void);
    /* The tab's identity strip, and what the page draws: the JSON of
     * sk_history_view. Optional. */
    void (*strip)(void *ud, sk_tab *tab, const sk_tab_strip *strip);
    void (*send)(void *ud, sk_tab *tab, const char *view);
    sk_log_fn log;
    void *ud;
} sk_dashboard_config;

/* Makes tab->session for spec, the dashboard's, with the app's dials.
 * Nothing starts. 0, or -1 with *err a malloc'd reason. */
int sk_dashboard_make(const sk_dashboard_config *c, sk_tab *tab, const sk_tab_spec *spec, char **err);
/* openDashboard: the dashboard's tab made active, else a new one in cwd,
 * the app's own folder, made active in the current window. NULL with
 * *err set when it could not be made, or with no error once the app is
 * ending. */
sk_tab *sk_dashboard_open(sk_tabs *tabs, const char *cwd, char **err);

/* Once, from idle, when the tab's pane is ready: the strip sent, a first
 * sample, and one every interval after. */
void sk_dashboard_start(sk_tab *tab);
/* One sample of every live session, sent while it runs: what the timer
 * does, and what a tab that moved does once its new pane is ready. */
void sk_dashboard_sample(sk_tab *tab);
/* The dials open (decision 0033), sent at once while it runs. */
void sk_dashboard_set_dials(sk_tab *tab, char *const *names, size_t n);

/* The tab's strip; false for a tab that is not the dashboard's. */
bool sk_dashboard_strip(const sk_tab *tab, sk_tab_strip *out);
/* The tab's own bus, which nothing writes; NULL for another tab. */
sk_bus *sk_dashboard_bus(const sk_tab *tab);
/* For the live checks: { ticks, rows, dials, watching, made: [{ key,
 * closed, tailing }] }, each watch agent made and whether its transcript
 * or rollout is followed. false, writing nothing, for another tab. */
bool sk_dashboard_report(const sk_tab *tab, sk_jw *w);

/* What the page draws of a board: null for no project, { state:
 * 'loading' }, { state: 'no access' } or { state: 'ok', open, closed,
 * openNow, closed14 }, as app/board.js gives it. */
void sk_dashboard_board(sk_jw *w, const sk_board *b);

#endif
