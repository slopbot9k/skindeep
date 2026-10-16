/* The tabs of a launch's windows (decision 0023), as app/tabs.js keeps
 * them, with the parts of app/main.js that are the tabs' model: New tab,
 * Reopen closed tab and Duplicate tab (decision 0026), a name given in a
 * tab (decision 0032), the strip's names, a tab whose CLI refused to
 * open a background session (decision 0030), and a tab popped out into
 * a new window.
 *
 * The list holds every window's tabs, each window's in its strip's
 * order, and each window has its own active tab. current is the window
 * the user last acted in, and the active tab is its active tab. Window 0
 * is the first. A tab leaves the list when its session has ended. The
 * app ends with its last tab, with the status of the last tab with a CLI
 * to end, unless an app error decided 125 first (decision 0003). A
 * monitor tab (decision 0027) and the sessions dashboard (decision 0028)
 * have no CLI of the app's and no status of their own.
 *
 * Every call is made on one thread, the core's. */
#ifndef SK_APP_TABS_H
#define SK_APP_TABS_H

#include "../core/env.h"

#include <sys/types.h>

/* A tab's session, behind one face: the CLI's session (app/session.js),
 * a monitor (app/monitor.js) or the dashboard (app/dashboard.js). The
 * tabs never start, feed or size a session. They hang it up, ask what
 * it follows, and free it once it is gone.
 *
 * A session reports its end once, by sk_tabs_ended, with the status of
 * decision 0003: for a CLI, after its child was reaped (decision 0016),
 * 129 when it was hung up before it started. It may report from inside
 * hangup. The tabs call free after the end, and nothing after free.
 * Strings a session returns stay valid until the next call on it. */
typedef enum {
    SK_TAB_SESSION_IDLE,
    SK_TAB_SESSION_PREPARING,
    SK_TAB_SESSION_RUNNING,
    SK_TAB_SESSION_ENDING,
    SK_TAB_SESSION_ENDED,
} sk_tab_session_state;

typedef struct {
    /* Required. Hangs up the CLI by the kill policy of decision 0016:
     * SIGHUP to its process group, SIGKILL 2 s later. Idempotent. */
    void (*hangup)(void *self);
    sk_tab_session_state (*state)(void *self);
    /* The CLI's pid; 0 before the spawn, and for a tab with no CLI. */
    pid_t (*pid)(void *self);
    /* The session id its agent adapter follows (decision 0026), or
     * NULL. */
    const char *(*session_id)(void *self);
    /* The CLI's own record of the session's name (decision 0032), or
     * NULL. */
    const char *(*title)(void *self);
    /* A name given in the tab, into the CLI's record; true when written.
     * NULL when the CLI keeps no record the app writes. */
    bool (*rename)(void *self, const char *name);
    /* The final screen, read when the CLI exited by itself (decision
     * 0030), rows ended by \n, or NULL. */
    const char *(*last_screen)(void *self);
    /* Required. */
    void (*free)(void *self);
} sk_tab_session_ops;

typedef struct {
    const sk_tab_session_ops *ops;
    void *self;
} sk_tab_session;

typedef enum { SK_TAB_CLI, SK_TAB_MONITOR, SK_TAB_DASHBOARD } sk_tab_kind;

/* A tab. The tabs own it and its strings; the caller reads them. */
typedef struct {
    int id;
    int win;
    sk_tab_kind kind;
    sk_strv cmd;   /* the command it runs, program first */
    char *cwd;     /* the folder it started in */
    sk_envp env;   /* its environment, the window's unless New tab... set one */
    char *file;    /* the program cmd[0] resolved to; NULL with no CLI */
    char *label;   /* derived (decision 0004) */
    sk_tab_session session;
    bool closed;   /* hung up by the user or the app: its end is no problem */
    bool unseen;   /* output came while it was not its window's active tab */
    bool moving;   /* popping out: its output waits for the new window */
    bool list_off; /* it shows what its task list replaces (decision 0034) */
    char *named;   /* a name given in the tab, or NULL */
    char *named_over; /* the record's name when it was given, or NULL */
} sk_tab;

typedef SK_VEC(sk_tab *) sk_tab_list;

/* What a tab is made from. The pointers are the caller's and need last
 * only through the call. */
typedef struct {
    sk_tab_kind kind;
    char *const *cmd;    /* NULL-terminated */
    const char *cwd;
    const sk_envp *env;  /* NULL: the window's for a CLI, empty otherwise */
    const char *file;    /* NULL: the window's for a CLI */
    const char *label;   /* NULL: derived from cmd and env */
    const char *note;    /* the strip's note, such as what was not carried */
    void *data;          /* the caller's, for make: a monitor's scan entry */
} sk_tab_spec;

typedef struct sk_tabs sk_tabs;

typedef struct {
    /* Required. Makes tab->session for spec. The tab already has its id,
     * window, kind, command, folder, environment, program and label.
     * 0, or -1 with *err a malloc'd reason. */
    int (*make)(void *ud, sk_tab *tab, const sk_tab_spec *spec, char **err);
    /* A tab whose CLI ended by itself, with code, may have a successor
     * in its place (decision 0030). spec comes filled with the tab's own
     * command, folder, environment and program; follow changes what it
     * needs, building any new command in cmd, and returns true. NULL:
     * follow_background decides. */
    bool (*follow)(void *ud, const sk_tab *tab, int code, sk_tab_spec *spec, sk_strv *cmd);
    /* With no follow: a Claude tab whose CLI refused to open a
     * background session in place is followed by `claude attach ID`. */
    bool follow_background;
    /* To the user (sk_tell) and to the log. */
    void (*tell)(void *ud, const char *msg);
    void (*log)(void *ud, const char *msg);
    /* Events, each optional. They must not free the tabs. */
    void (*change)(void *ud);            /* the list, the order or an active tab */
    void (*active)(void *ud, sk_tab *tab); /* another tab is active in its window */
    /* tab's session ended; next is its successor, or NULL. tab is freed
     * after the call, unless it was the last. */
    void (*gone)(void *ud, sk_tab *tab, int code, sk_tab *next);
    /* win's last tab left while tabs remain in other windows. */
    void (*empty)(void *ud, int win);
    /* The last tab is gone: the app's status. */
    void (*ended)(void *ud, int status);
    void *ud;
    /* The window's command (decision 0003), the program it resolved to,
     * the app's environment, and the home for labels. They outlive the
     * tabs. */
    char *const *cmd;
    const char *file;
    const sk_envp *env;
    const char *home;
} sk_tabs_config;

sk_tabs *sk_tabs_new(const sk_tabs_config *cfg);
/* Frees every tab and its session. Hangs up nothing. */
void sk_tabs_free(sk_tabs *t);

/* The session of tab id ended with code. */
void sk_tabs_ended(sk_tabs *t, int id, int code);

/* A new tab at the end of win's strip (win < 0: current), made active.
 * NULL once the app is ending, or with *err set when make failed. err
 * may be NULL. */
sk_tab *sk_tabs_add(sk_tabs *t, const sk_tab_spec *spec, int win, char **err);

sk_tab *sk_tabs_get(const sk_tabs *t, int id);
/* The list, every window's tabs. */
size_t sk_tabs_count(const sk_tabs *t);
sk_tab *sk_tabs_at(const sk_tabs *t, size_t i);
/* win's tabs in its strip's order, into out, emptied first. */
void sk_tabs_in_win(const sk_tabs *t, int win, sk_tab_list *out);
/* The windows that have tabs, in the order of the list. Returns how
 * many, writing at most cap. */
size_t sk_tabs_wins(const sk_tabs *t, int *out, size_t cap);
sk_tab *sk_tabs_active(const sk_tabs *t);
sk_tab *sk_tabs_active_of(const sk_tabs *t, int win);
int sk_tabs_current(const sk_tabs *t);
void sk_tabs_set_current(sk_tabs *t, int win);
/* A call or the focus came from win: it becomes current when it has
 * tabs. */
void sk_tabs_touch(sk_tabs *t, int win);

void sk_tabs_activate(sk_tabs *t, int id);
/* d 1 the next tab of win (< 0: current), -1 the previous, wrapping. */
void sk_tabs_step(sk_tabs *t, int d, int win);
/* index: the place among the tab's own window's tabs, clamped. */
void sk_tabs_move(sk_tabs *t, int id, long index);
/* The tab leaves its window for win, last in its strip and active
 * there. Its old window activates the neighbour a close would. */
void sk_tabs_detach(sk_tabs *t, int id, int win);
/* Hangs up the tab's CLI. The tab leaves when its session has ended. */
void sk_tabs_close(sk_tabs *t, int id);
void sk_tabs_close_win(sk_tabs *t, int win);
/* Hangs up every tab; no tab is added after. */
void sk_tabs_hangup_all(sk_tabs *t);
/* An app error: told once, 125 decided, every tab hung up. After the
 * decision, or after the last tab, it is only logged. */
void sk_tabs_fail(sk_tabs *t, const char *err);

bool sk_tabs_done(const sk_tabs *t);
bool sk_tabs_ending(const sk_tabs *t);
/* 125 once an app error decided it, else -1. */
int sk_tabs_status(const sk_tabs *t);
/* Once done: the app's status, as ended gave it. */
int sk_tabs_exit_status(const sk_tabs *t);
/* The last tab gone, once the list is empty. */
sk_tab *sk_tabs_last(const sk_tabs *t);
/* The final screen of the last tab's CLI, when the app ends because it
 * exited by itself: the app writes it to its stdout when that is a
 * terminal (decision 0030). NULL otherwise. */
const char *sk_tabs_last_screen(const sk_tabs *t);

/* New tab (decision 0023): the window's command with the options its
 * provider carries, in cwd, which is the live folder of the active
 * tab's CLI or the app's own. Its strip names what was not carried. */
sk_tab *sk_tabs_new_tab(sk_tabs *t, const char *cwd, char **err);
/* Reopen closed tab (decision 0026): the newest closed tab, resuming its
 * session in its agent, profile and folder. A session another tab runs
 * now is that tab's alone, and it is made active instead. NULL when
 * nothing is closed, or with *err set when make failed, the entry kept. */
sk_tab *sk_tabs_reopen(sk_tabs *t, char **err);
/* Duplicate tab: a fork of the active tab's session, beside it. NULL
 * for a tab with no CLI. */
sk_tab *sk_tabs_duplicate(sk_tabs *t, char **err);

/* What the menu's Reopen and Duplicate entries say: whether each can
 * run, and whether it starts a new session for want of a session id. */
typedef struct {
    bool reopen, reopen_fresh, duplicate, duplicate_fresh;
} sk_tabs_menu;
void sk_tabs_menu_state(const sk_tabs *t, sk_tabs_menu *out);

/* The name a tab shows (decision 0032): the newer of the CLI's record
 * and a name given in the tab, else the label. */
const char *sk_tab_name(const sk_tab *tab);
/* A name given in tab id, trimmed and cut at 120 characters, kept for
 * its life and written into the CLI's record where the session keeps
 * one. False, changing nothing, for an empty name. */
bool sk_tabs_rename(sk_tabs *t, int id, const char *name);
/* The folder the strip adds to a tab's name when another tab of its
 * window shows the same name (decision 0004): its basename. */
bool sk_tabs_folder(const sk_tabs *t, const sk_tab *tab, sk_buf *out);
/* The tab's CLI wrote output: a tab not active in its window is marked
 * unseen. True when the strip changed. */
bool sk_tabs_output(sk_tabs *t, sk_tab *tab);

/* Pop out: a tab dragged out of from_win leaves for a new window when
 * it is from_win's, the window keeps another tab, the app is not ending
 * and it is not moving already. Then it is marked moving: its output
 * waits while the old window serializes its terminal. */
bool sk_tabs_pop_begin(sk_tabs *t, int id, int from_win);
/* The new window's id: the tab detached into it, active there, and the
 * window current. -1 when the tab ended meanwhile. */
int sk_tabs_pop_end(sk_tabs *t, int id);
/* The new window's terminal has the tab's buffer and waiting output. */
void sk_tabs_pop_done(sk_tabs *t, int id);

#endif
