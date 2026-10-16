/* New tab... (decision 0025), the core's half, as app/main.js has it:
 * chooserState, what the dialog offers, chooserOpen, which checks the
 * dialog's pick and opens its tab, chooser:forget, and folderOf, the
 * folder as typed. The dialog itself is app/chooser.h's, on the UI
 * thread.
 *
 * sk_newtab_offer, sk_newtab_open and sk_newtab_forget are made on the
 * core thread, which owns the tabs. The rest touch nothing shared, and
 * a state or a pick, which own all they hold, may pass between the
 * threads. */
#ifndef SK_APP_NEWTAB_H
#define SK_APP_NEWTAB_H

#include "foreign.h"
#include "tabs.h"

/* A profile as the dialog lists it. remembered: a record exists, so it
 * can be forgotten. */
typedef struct {
    char *dir;   /* absolute */
    char *shown; /* dir with ~ */
    bool is_default, remembered;
} sk_newtab_profile;
typedef SK_VEC(sk_newtab_profile) sk_newtab_profiles;

/* An agent as the dialog offers it: its name and variable are
 * sk_profile_agents[agent]'s. */
typedef struct {
    sk_profile_agent agent;
    sk_newtab_profiles profiles;
    /* The command line: the window's command with the options it carries
     * when the window runs this agent, else the bare CLI, each word
     * quoted for a shell. */
    char *line;
    /* The window's options it does not carry, quoted; "" for none. */
    char *dropped;
    /* The profile it starts on: the active tab's when that runs this
     * agent, else the default. */
    char *profile;
} sk_newtab_agent;

typedef struct {
    sk_newtab_agent agents[SK_PROFILE_AGENTS];
    sk_profile_agent agent; /* the agent it starts on */
    char *folder;           /* the folder it starts in, with ~ */
    /* What the dialog reads a folder with, as sk_newtab_folder_of does:
     * the home (NULL: none) and the folder a relative one is taken from,
     * the active tab's live folder when the state was made. */
    char *home;
    char *base;
} sk_newtab_state;

void sk_newtab_state_free(sk_newtab_state *s);

/* What the dialog asks to open, each string malloc'd. agent is "claude"
 * or "codex", profile the profile's dir, folder as typed. */
typedef struct {
    char *agent, *profile, *folder, *line;
} sk_newtab_pick;

void sk_newtab_pick_free(sk_newtab_pick *p);

typedef struct {
    sk_tabs *tabs;
    /* The home scan, which the sessions the app did not start share
     * (sk_foreign_home_scan); NULL: no profile is found by a scan. */
    sk_foreign *foreign;
    /* The window's command (decision 0003), the app's environment, its
     * home (NULL: none) and its own folder. */
    char *const *cmd;
    const sk_envp *env;
    const char *home;
    const char *cwd;
    /* The records of profiles (sk_profiles_dir); NULL: none kept. */
    const char *profiles_dir;
    /* A tab's live folder, malloc'd, or NULL when it cannot be told.
     * NULL: the kernel's, of the tab's CLI (sk_procinfo_cwd). */
    char *(*live_cwd)(void *ud, const sk_tab *tab);
    /* ms since the epoch, for the records; NULL: the real clock. */
    double (*now)(void);
    sk_log_fn log;
    void *ud;
} sk_newtab_config;

/* chooserState: what the dialog of window win (< 0: the current one)
 * offers, into out, which the caller frees. fresh: the home scanned
 * again, as each opening of the dialog does; Forget keeps the scan of
 * the dialog open. */
void sk_newtab_offer(const sk_newtab_config *cfg, int win, bool fresh, sk_newtab_state *out);

/* chooserOpen: the pick checked and its tab opened in win (< 0: the
 * current one), on its profile, in its folder; a profile other than the
 * default is then recorded. 0, or -1 with *err a malloc'd reason the
 * dialog shows, as main.js words it. */
int sk_newtab_open(const sk_newtab_config *cfg, int win, const sk_newtab_pick *pick, char **err);

/* chooser:forget: the record of agent's profile dir removed. The dialog
 * then takes sk_newtab_offer(cfg, win, false, ...). */
void sk_newtab_forget(const sk_newtab_config *cfg, const char *agent, const char *dir);

/* folderOf: typed, trimmed, as a shell takes a folder: ~ is home, ~/x
 * lies under it, and a relative folder is base's. Absolute and
 * normalized, malloc'd. */
char *sk_newtab_folder_of(const char *typed, const char *home, const char *base);
/* Where the folder picker starts (chooser:folder): the folder typed when
 * it is one, else the home, else base. malloc'd. */
char *sk_newtab_browse_start(const char *typed, const char *home, const char *base);

#endif
