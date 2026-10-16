/* The agent sessions of this user the app did not start (decision 0027),
 * as app/main.js lists and watches them for the menu, the monitor tabs
 * and the sessions dashboard (decision 0028): listSessions with its home
 * rescans, keyOf and watching, foreignLabel, knownProfiles, channelOf,
 * watchAgent and openSession. A session in a tab, its own or one it
 * watches, is that tab's. Any other's transcript or rollout is looked
 * for among the profiles the app knows (decision 0025).
 *
 * Every call is made on one thread, the core's. */
#ifndef SK_APP_FOREIGN_H
#define SK_APP_FOREIGN_H

#include "../agents/agent.h"
#include "../sessions/sessions.h"
#include "tabs.h"

/* A session whose channel the known profiles do not hold has the home
 * scanned again, at most once in this many ms. */
#define SK_FOREIGN_RESCAN_MS 10000

/* A session as the scan found it, with its transcript or rollout when
 * one was found. */
typedef struct {
    sk_live_session s;
    sk_session_channel *found; /* NULL: none found, or not looked for */
} sk_foreign_session;

/* One of the menu's agent sessions: the scan entry, the tab that shows
 * it, and the menu's row. A session in a CLI tab, or started inside
 * one, is not looked for: its found is NULL. */
typedef struct {
    sk_foreign_session s;
    /* The tab that shows it, or NULL, with that tab's agent and bus.
     * They last only until the tabs next change. */
    sk_tab *tab;
    sk_agent *agent;
    sk_bus *bus;
    /* The row: "<pid>:<start>", the folder with ~, the label, the age in
     * ms, the tab's id (0: none), "tab", "monitor" or "", and working,
     * waiting, idle or unknown. */
    char *key, *folder, *label;
    double age;
    int tab_id;
    const char *where, *state;
} sk_foreign_entry;
typedef SK_VEC(sk_foreign_entry) sk_foreign_entries;

void sk_foreign_entries_free(sk_foreign_entries *l);

typedef struct {
    sk_tabs *tabs;
    /* Ticks the watch agents; NULL: whoever holds one ticks it. */
    sk_loop *loop;
    /* The app's environment and home: its own profile, the folders' and
     * labels' ~, the home scan. */
    const sk_envp *env;
    const char *home;
    /* The records of profiles (sk_profiles_dir), or NULL. */
    const char *profiles_dir;
    /* Where the sessions come from. A NULL member is the machine's:
     * sk_sessions_scan of this user, sk_sessions_ancestors,
     * sk_sessions_same, and sk_sessions_claude_transcript or
     * sk_sessions_codex_rollout among profiles (NULL-terminated). */
    void (*scan)(void *ud, sk_live_sessions *out);
    size_t (*ancestors)(void *ud, pid_t pid, pid_t *out, size_t max);
    bool (*same)(void *ud, pid_t pid, double started);
    bool (*channel)(void *ud, const sk_live_session *s, char *const *profiles, sk_session_channel *out);
    /* ms since the epoch, for ages, states and the rescans; NULL: the
     * real clock. */
    double (*now)(void);
    sk_log_fn log;
    void *ud;
} sk_foreign_config;

/* The pointers of cfg outlive it, and it outlives every agent it made. */
typedef struct sk_foreign sk_foreign;

sk_foreign *sk_foreign_new(const sk_foreign_config *cfg);
void sk_foreign_free(sk_foreign *f);

/* keyOf: "<pid>:<started, rounded>". */
void sk_foreign_key(sk_buf *out, pid_t pid, double started);
/* foreignLabel: the agent with its profile's name, as a tab of that
 * profile is labelled, or "<agent> [?]" when the profile cannot be
 * told. */
void sk_foreign_label(sk_buf *out, sk_profile_agent kind, const sk_session_channel *found,
                      const char *home);

/* The config dirs under the home (decision 0025), scanned again when the
 * last scan is older than max_age ms; 0 for a fresh one. The chooser of
 * New tab... shares it. Valid until the next scan. */
const sk_profile_founds *sk_foreign_home_scan(sk_foreign *f, double max_age);
/* knownProfiles: the profiles a session the app did not start may run
 * on: the default, the app's, the open CLI tabs', those recorded and
 * those the last home scan found, each once, appended to out. */
void sk_foreign_profiles(sk_foreign *f, sk_profile_agent kind, sk_strv *out);
/* channelOf: s's transcript or rollout among the known profiles. */
bool sk_foreign_channel(sk_foreign *f, const sk_live_session *s, sk_session_channel *out);
/* watching: the first monitor tab of the process pid that still runs,
 * or NULL. */
sk_tab *sk_foreign_watching(sk_foreign *f, pid_t pid);

/* listSessions: every live session of this user, those in a tab first,
 * else the newest first, appended to out. A session started inside a
 * tab's CLI is that tab's, unless that CLI is itself a session listed. */
void sk_foreign_sessions(sk_foreign *f, sk_foreign_entries *out);
/* The rows as the menu's sessions:list gives them: [{ key, folder, age,
 * tab, where, label, state }]. */
void sk_foreign_rows(const sk_foreign_entries *l, sk_jw *w);

/* watchAgent: the read-only agent of w on bus (decisions 0027, 0028):
 * Claude Code's transcript, looked for again among the known profiles
 * every 5 s, or Codex's rollout under the home it was found in, else the
 * default, beside Generic counting from the session's start. Nothing is
 * prepared. */
sk_agent *sk_foreign_watch(sk_foreign *f, const sk_foreign_session *w, sk_bus *bus);

/* openSession: the session of key's tab made active, else a monitor tab
 * of it opened (SK_TAB_MONITOR, its spec's data the sk_foreign_session).
 * 0, or -1 with *err a malloc'd reason for the user. */
int sk_foreign_open(sk_foreign *f, const char *key, char **err);

#endif
