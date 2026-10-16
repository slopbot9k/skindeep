/* A read-only tab of an agent session the app did not start (decision
 * 0027), as app/monitor.js and makeMonitor of app/main.js make it. Its
 * meters come from the session's own side channels through a watch
 * agent (sk_foreign_watch) on a bus of its own. It has no pty and no
 * child: it never types into the session and never signals it, and its
 * pid is 0, so nothing that ends the app's children reaches the watched
 * process. Its pane says what it watches. It ends when the session ends
 * or the tab closes, with no status of its own (0), which reaches
 * sk_tabs_ended from the loop.
 *
 * What the JavaScript sent its view goes to the config's callbacks; the
 * view is the window's. The strip a page load wants is the window's to
 * ask for, with sk_monitor_strip.
 *
 * Every call is made on one thread, the core's. */
#ifndef SK_APP_MONITOR_H
#define SK_APP_MONITOR_H

#include "foreign.h"

#define SK_MONITOR_POLL_MS 2000

/* The app's, shared by its monitor tabs; every pointer outlives them. */
typedef struct {
    sk_loop *loop;
    sk_tabs *tabs;     /* set once the tabs are made, before a tab is */
    const char *home;  /* the strip's and the pane's ~ */
    uint64_t poll_ms;  /* 0: SK_MONITOR_POLL_MS */
    /* The watch agent of w on bus, as sk_foreign_watch makes it.
     * Required. The monitor prepares, attaches, closes and frees it. */
    sk_agent *(*watcher)(void *ud, const sk_foreign_session *w, sk_bus *bus);
    /* Whether pid still runs the process that started at started; NULL:
     * sk_sessions_same. */
    bool (*same)(void *ud, pid_t pid, double started);
    /* The tab's identity strip, and text for its pane as a pty's bytes
     * would come. Optional. */
    void (*strip)(void *ud, sk_tab *tab, const sk_tab_strip *strip);
    void (*pane)(void *ud, sk_tab *tab, const char *text);
    sk_log_fn log;
    void *ud;
} sk_monitor_config;

/* Makes tab->session for spec, a monitor's: spec->data is the
 * sk_foreign_session to watch, and the tab's label its sk_foreign_label.
 * The agent is made; nothing starts. 0, or -1 with *err a malloc'd
 * reason. */
int sk_monitor_make(const sk_monitor_config *c, sk_tab *tab, const sk_tab_spec *spec, char **err);

/* Once, from idle, when the tab's pane is ready: the agent prepared and
 * attached to the watched process, the pane told, the strip sent and the
 * polls begun. A session that has ended ends the tab instead. */
void sk_monitor_start(sk_tab *tab);
/* What each poll does: the session's end ends the tab, and a new notice
 * of the agent's goes to the strip. */
void sk_monitor_poll(sk_tab *tab);

/* The tab's strip as it is now; false for a tab that is not a monitor's.
 * The strings last until the next call on the tab. */
bool sk_monitor_strip(const sk_tab *tab, sk_tab_strip *out);
/* The tab's parts, NULL for a tab that is not a monitor's. */
sk_agent *sk_monitor_agent(const sk_tab *tab);
sk_bus *sk_monitor_bus(const sk_tab *tab);
/* The process watched; false for a tab that is not a monitor's. */
bool sk_monitor_watched(const sk_tab *tab, pid_t *pid, double *started);

#endif
