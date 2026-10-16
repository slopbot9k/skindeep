/* The profiles New tab... offers (decision 0025), as app/profiles.js has
 * them. A profile is a config dir: CLAUDE_CONFIG_DIR of Claude Code,
 * CODEX_HOME of Codex (decision 0003). Each profile a tab was opened on
 * is recorded in a file of its own, <dir>/<agent>-<hash>, by write and
 * rename (decision 0022), so no two windows write one file. The scan of
 * the home reads directory listings, never a file's contents. */
#ifndef SK_PROFILES_H
#define SK_PROFILES_H

#include "../base/paths.h"
#include "../core/env.h"

#define SK_PROFILES_SCAN_DEPTH 4
#define SK_PROFILES_SCAN_CAP 20000

typedef enum { SK_PROFILE_CLAUDE, SK_PROFILE_CODEX } sk_profile_agent;

#define SK_PROFILE_AGENTS 2

typedef struct {
    const char *kind;     /* "claude", "codex": the records' agent */
    const char *name;     /* "Claude Code" */
    const char *cmd;      /* "claude" */
    const char *variable; /* "CLAUDE_CONFIG_DIR" */
    const char *dflt;     /* ".claude", under the home */
} sk_profile_agent_info;

extern const sk_profile_agent_info sk_profile_agents[SK_PROFILE_AGENTS];

/* The agent of kind, as the records name it; false for none. */
bool sk_profiles_agent_of(sk_str kind, sk_profile_agent *out);

typedef struct {
    double t; /* when recorded, ms since the epoch */
    sk_profile_agent agent;
    char *dir;
} sk_profile_record;
typedef SK_VEC(sk_profile_record) sk_profile_records;

typedef struct {
    sk_profile_agent agent;
    char *dir;
} sk_profile_found;
typedef SK_VEC(sk_profile_found) sk_profile_founds;

/* A row of New tab...: remembered when a record exists, so it can be
 * forgotten. */
typedef struct {
    char *dir;
    bool is_default, remembered;
} sk_profile_choice;
typedef SK_VEC(sk_profile_choice) sk_profile_choices;

/* <state>/profiles, the records' directory; NULL with no home. */
char *sk_profiles_dir(const sk_env *e);
/* The agent's default profile, ~/.claude or ~/.codex. malloc'd. */
char *sk_profiles_default(sk_profile_agent a);
/* The profile a tab of a runs on under env: its variable resolved, else
 * the default. malloc'd. */
char *sk_profiles_of(const sk_envp *env, sk_profile_agent a);
/* Makes env a tab's on profile p: the variable set to p resolved, or
 * unset for the default profile even when env sets it. */
void sk_profiles_env_for(sk_envp *env, sk_profile_agent a, const char *p);

/* Records p for a at time now in d, made when missing. 0, or -1 with
 * errno. */
int sk_profiles_record(const char *d, sk_profile_agent a, const char *p, double now);
/* Every record in d, newest first, appended to out. A file that is not
 * a record is skipped. */
void sk_profiles_read(const char *d, sk_profile_records *out);
void sk_profiles_forget(const char *d, sk_profile_agent a, const char *p);

/* Every config dir under root, at most depth levels below it, root
 * itself excepted, appended to out sorted by path. Breadth first, links
 * not followed, a config dir not entered, .git, node_modules and .cache
 * skipped, at most cap dirs listed. */
void sk_profiles_scan(const char *root, int depth, size_t cap, sk_profile_founds *out);

/* What New tab... lists for a, each profile once: the default, open (the
 * open tabs' profiles, in tab order), those recorded, newest first, then
 * those the scan found. Appended to out. */
void sk_profiles_list(sk_profile_agent a, const sk_profile_records *records, char *const *open,
                      size_t n_open, const sk_profile_founds *found, sk_profile_choices *out);

void sk_profile_records_free(sk_profile_records *r);
void sk_profile_founds_free(sk_profile_founds *f);
void sk_profile_choices_free(sk_profile_choices *c);

#endif
