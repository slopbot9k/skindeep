/* The live agent sessions of this user on the machine (decision 0027),
 * as app/sessions.js finds them: every process named claude or codex, or
 * a node running a script of that name as an npm install does, but
 * Codex's daemons and the CLI an npm wrapper runs, with its folder, its
 * start and its command line. A process's environment is never read,
 * nor any record a CLI keeps of its login. Of Claude Code's record of a
 * running session three fields are read (sk_sessions_claude_record). A
 * session's transcript or rollout is found among the profiles this user
 * chose (decision 0025), by that record, else by name, folder and time.
 * Lists of profiles are NULL-terminated, as an sk_strv's ptr. */
#ifndef SK_SESSIONS_H
#define SK_SESSIONS_H

#include "profiles.h"

#include <sys/types.h>

#define SK_SESSIONS_GRACE_MS 2000
#define SK_SESSIONS_MAX_ANCESTORS 256

typedef struct {
    pid_t pid, ppid;
    sk_profile_agent kind;
    double started; /* ms since the epoch */
    char *cwd;
    sk_strv argv;
} sk_session;
typedef SK_VEC(sk_session) sk_sessions;

/* Every live session of uid, the newest first, appended to out. A
 * process whose parent is a session of its own agent is that session,
 * as the native codex an npm codex runs, and is left out. */
void sk_sessions_scan(uid_t uid, sk_sessions *out);
void sk_sessions_free(sk_sessions *s);

/* pid's ancestors, its parent first, up to init and at most max (and
 * SK_SESSIONS_MAX_ANCESTORS). Returns how many. */
size_t sk_sessions_ancestors(pid_t pid, pid_t *out, size_t max);
/* The same process: alive, and started within a second of started. */
bool sk_sessions_same(pid_t pid, double started);

/* What Claude Code 2.1.296 writes as procStart in its record of a
 * process that started at start (sk_procinfo's): on Linux the clock
 * ticks of /proc/<pid>/stat in decimal; on macOS what `ps -o lstart`
 * prints in UTC, such as "Fri Oct 16 14:44:42 2026" (measured with
 * 2.1.301, 2026-10-16). Appends to out. */
void sk_claude_proc_start(sk_buf *out, sk_os os, uint64_t start);
/* True when procStart, as String() makes it, names that start. Runs of
 * spaces count as one on macOS, where ps pads a day below 10. */
bool sk_claude_proc_start_is(sk_str proc_start, sk_os os, uint64_t start);

typedef struct {
    char *session;
    sk_strv profiles; /* those sharing the sessions folder it is in */
} sk_claude_record;

/* Claude Code keeps <profile>/sessions/<pid>.json while a session runs,
 * naming its session id, a /clear's included. Of it only pid, sessionId
 * and procStart are read; every other value is skipped undecoded, and
 * the .key file beside it is never opened (maintainer, 2026-10-12). It
 * counts only when it names this process, by pid and procStart, and the
 * id is a plain name. False when no profile holds one. */
bool sk_sessions_claude_record(pid_t pid, char *const *profiles, sk_claude_record *out);
void sk_claude_record_free(sk_claude_record *r);

/* A session's transcript or rollout. */
typedef struct {
    char *file;      /* NULL: the session is named, its file not yet written */
    char *session;   /* the session id, or Codex's thread id; may be NULL */
    double mtime;    /* ms; 0 with no file */
    char *profile;   /* NULL when it cannot be told */
    char *home;      /* Codex: the home it was found in; else NULL */
    const char *how; /* Codex: the route; else NULL */
} sk_session_channel;

void sk_session_channel_free(sk_session_channel *c);

/* A Claude session's transcript. By its record when it has one: that
 * session's transcript in the record's profile, file NULL until Claude
 * Code writes it. Else the one of its folder written last since it
 * started, among the profiles, so two sessions of one profile in one
 * folder may show each other's. The profile is the record's, else the
 * one profile that holds session-env/<id>. pid 0 skips the record. False
 * when none is found. */
bool sk_sessions_claude_transcript(pid_t pid, const char *cwd, double started, char *const *profiles,
                                   sk_session_channel *out);

/* What Codex's rollout discovery (decision 0013) found. */
typedef struct {
    char *file;      /* malloc'd */
    char *thread;    /* malloc'd, or NULL */
    const char *how; /* static: argv, fd, new, forked or resumed */
} sk_codex_rollout;

/* The rollout of the Codex session pid, run with args (its argv without
 * the program) in cwd, launched at ms since the epoch, among the
 * sessions of codex_home: the routes of app/metrics/codex/discovery.js,
 * a new Discovery's find(pid). True and *out filled on a hit. The core
 * passes the real discovery; tests pass their own. */
typedef bool (*sk_codex_rollout_fn)(void *ud, pid_t pid, const char *codex_home, char *const *args,
                                    const char *cwd, double launched, sk_codex_rollout *out);

/* A Codex session's rollout among homes, one try per sessions folder
 * they share, its start as the launch. The profile is the home when one
 * alone keeps that folder. False when none is found. */
bool sk_sessions_codex_rollout(const sk_session *s, char *const *homes, sk_codex_rollout_fn find,
                               void *ud, sk_session_channel *out);

/* What a session does, from when its channel was written: "working"
 * within 10 s, "waiting" within 10 min, else "idle"; "unknown" with no
 * channel (mtime 0). */
const char *sk_sessions_state_of(double mtime, double now);

#endif
