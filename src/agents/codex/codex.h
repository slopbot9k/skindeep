/* The parts of the Codex adapter (decisions 0013, 0021), shared by its
 * files and its tests: the rollout parser, the thread names, discovery,
 * and the JavaScript value rules they keep. */
#ifndef SK_CODEX_H
#define SK_CODEX_H

#include "../agent.h"

/* JavaScript's rules for a JSON value, as the JavaScript app applied
 * them (parse.c). NULL stands for undefined. */

/* !!v. */
bool sk_codex_truthy(const sk_json *v);
/* String(v), appended to out. */
void sk_codex_js_string(sk_buf *out, const sk_json *v);
/* s.trim(): without JavaScript's white space and line terminators. */
sk_str sk_codex_trim(sk_str s);
/* s.slice(0, max), max counted in UTF-16 units, appended to out. A
 * character it would cut in two is left out, since half of one has no
 * UTF-8. */
void sk_codex_slice16(sk_buf *out, sk_str s, size_t max);

/* parse.c: rollout lines to metric writes, as app/metrics/codex/parse.js. */

typedef struct {
    double minutes, pct, resets_at; /* resets_at NAN: none */
} sk_codex_window;

typedef struct {
    char *id, *model; /* model NULL: the account's limit */
    sk_codex_window windows[2];
    size_t n;
} sk_codex_limit;

typedef struct {
    bool have_prev;
    double prev_out;
    /* The strings of the session_meta line, NULL for any other value. */
    char *meta_id, *meta_cwd, *meta_source;
    /* The latest windows of each limit_id, in order of first appearance. */
    SK_VEC(sk_codex_limit) limits;
} sk_codex_state;

void sk_codex_state_free(sk_codex_state *s);
/* Writes what one rollout line says to src: values, counters, events
 * and rate samples. history: the line is replayed from the file's past. */
void sk_codex_state_add(sk_codex_state *s, const sk_json *obj, sk_source *src, bool history);
/* The visualization's class of a tool: 3 shell, 2 edit, 1 read, 4 web,
 * 5 agent, else 0. */
int sk_codex_tool_class(sk_str name);

/* thread_name.c: a thread's name (decision 0032), as
 * app/metrics/codex/thread-name.js. */

#define SK_CODEX_NAME_MAX 120

/* The last name text gives thread id, malloc'd, or NULL. */
char *sk_codex_thread_name(sk_str text, const char *id);

typedef struct {
    char *file; /* <CODEX_HOME>/session_index.jsonl, NULL with no home */
    bool stamped;
    long long size;
    double mtime;
    sk_buf text;
    char *name;
} sk_codex_names;

void sk_codex_names_init(sk_codex_names *n, const sk_envp *env);
/* The current name of thread id, or NULL, reading the index again only
 * when its size or mtime changed. Valid until the next call. */
const char *sk_codex_names_of(sk_codex_names *n, const char *id);
void sk_codex_names_free(sk_codex_names *n);

/* discovery.c: which rollout is the session's, as
 * app/metrics/codex/discovery.js. Its times are wall-clock ms, the clock
 * of file mtimes and of the rollouts' stamps. */

/* Now on the wall clock, in whole ms, as Date.now(). */
double sk_codex_wall_ms(void);
/* Date.parse of ECMAScript's date-time format, the one Codex and
 * toISOString write: a date alone is UTC, a time with no offset local.
 * NAN for any other string, the older formats V8 also reads among them. */
double sk_codex_date_parse(sk_str s);

/* What discovery reads of the processes, for tests to fake. */
typedef size_t (*sk_codex_tree_fn)(pid_t pid, pid_t *out, size_t max);
typedef void (*sk_codex_files_fn)(pid_t pid, sk_strv *paths);

typedef struct sk_codex_disc sk_codex_disc;

/* A hit, or a miss: file NULL, how "none", and the routes tried. how is
 * argv, fd, new, forked or resumed, and only argv and fd are exact. */
typedef struct {
    const char *file, *thread;
    const char *how;
    bool exact;
    const char *tried[3];
    size_t n_tried;
} sk_codex_found;

/* A rollout read as history up to byte end. */
typedef struct {
    char *file;
    int64_t end;
} sk_codex_base;

typedef SK_VEC(sk_codex_base) sk_codex_bases;

/* home: CODEX_HOME, NULL when there is none. argv: the user's arguments
 * after the command. cwd: the app's cwd, NULL for the process's.
 * launched: the cut's base, wall-clock ms. */
sk_codex_disc *sk_codex_disc_new(const char *home, char *const *argv, const char *cwd,
                                 double launched);
void sk_codex_disc_free(sk_codex_disc *d);
void sk_codex_disc_set_launched(sk_codex_disc *d, double launched);
/* sk_proc_tree and sk_proc_open_files unless a test gives its own. */
void sk_codex_disc_set_procs(sk_codex_disc *d, sk_codex_tree_fn tree, sk_codex_files_fn files);
/* The session's rollout. pid 0: no child to look in. After a hit, the
 * same hit without scanning. Valid until the next call. */
const sk_codex_found *sk_codex_disc_find(sk_codex_disc *d, pid_t pid);
/* The rollout of thread id, malloc'd, or NULL. */
char *sk_codex_disc_by_id(sk_codex_disc *d, const char *id);
/* The rollouts file's history continues, oldest first, each to the byte
 * its fork was made at (decision 0026). */
void sk_codex_disc_bases(sk_codex_disc *d, const char *file, sk_codex_bases *out);
void sk_codex_bases_free(sk_codex_bases *b);
/* The subagent threads of thread: *spawned those first seen by this
 * call, *active those written within 20 s of now. */
void sk_codex_disc_subagents(sk_codex_disc *d, const char *thread, double now, int *spawned,
                             int *active);

/* codex.c: a test's way in, the line handler of a part that
 * sk_provider_codex made. */
void sk_codex_on_line(void *self, const sk_json *obj, bool history);

#endif
