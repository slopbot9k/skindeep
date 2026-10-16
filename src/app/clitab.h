/* A CLI tab's session (app/main.js's makeTab): the command on a pty
 * through its agent adapter beside Generic (decision 0021), with its own
 * bus and its own directory, tab-<id>, under the launch's runtime dir
 * for the adapter's generated files (decision 0016). It is the tabs'
 * sk_tab_session; the window starts, feeds and sizes it through
 * sk_clitab_session. Its end goes to sk_tabs_ended.
 *
 * Every call is made on one thread, the core's. */
#ifndef SK_APP_CLITAB_H
#define SK_APP_CLITAB_H

#include "../agents/agent.h"
#include "../core/bus.h"
#include "../core/session.h"
#include "tabs.h"

/* The app's, shared by its CLI tabs; every pointer outlives them. */
typedef struct {
    sk_loop *loop;
    sk_tabs *tabs;          /* set once the tabs are made, before a tab is */
    const char *run_dir;    /* the launch's runtime dir */
    const char *root;       /* the runtime root the relays search */
    const char *store;      /* where generated Claude settings go */
    const char *relay;      /* the path of skinned-relay, or NULL */
    sk_statusline_mode statusline;
    /* The tab's CLI exists, its pid known. Optional. */
    void (*spawned)(void *ud, sk_tab *tab, pid_t pid);
    /* Bytes from the tab's pty, as they came. Optional. */
    void (*output)(void *ud, sk_tab *tab, const char *bytes, size_t n);
    sk_log_fn log;
    sk_log_fn tell;
    void *ud;
} sk_clitab_config;

/* Makes tab->session for spec, a CLI tab's: tab already has its id,
 * command, folder, environment, program and label. Nothing starts. 0,
 * or -1 with *err a malloc'd reason. */
int sk_clitab_make(const sk_clitab_config *c, sk_tab *tab, const sk_tab_spec *spec, char **err);

/* The tab's parts, NULL for a tab that is not a CLI tab's. */
sk_session *sk_clitab_session(const sk_tab *tab);
sk_agent *sk_clitab_agent(const sk_tab *tab);
sk_bus *sk_clitab_bus(const sk_tab *tab);
/* The strip's note, such as what was not carried, or NULL. */
const char *sk_clitab_note(const sk_tab *tab);

#endif
