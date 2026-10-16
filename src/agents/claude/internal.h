/* The Claude Code adapter's parts, shared by its files and its tests:
 * JavaScript's own reading of values, the compaction rule, the
 * payload parsers, the session core, the settings writer, the hook
 * receiver and the title record (app/metrics/claude/). */
#ifndef SK_CLAUDE_INTERNAL_H
#define SK_CLAUDE_INTERNAL_H

#include "../../base/arena.h"
#include "../../base/json.h"
#include "../../base/map.h"
#include "../../core/bus.h"
#include "../../core/env.h"
#include "../../core/loop.h"
#include "../agent.h"
#include "claude.h"

#include <sys/types.h>

/* One line to log, through fn when it is set. */
void sk_claude_logf(sk_log_fn fn, void *ud, const char *fmt, ...) SK_PRINTF(3, 4);

/* ---- JavaScript's reading of a JSON value (js.c) ---- */

/* Truthiness; v NULL is undefined. */
bool sk_claude_truthy(const sk_json *v);
/* String(v); v NULL is "undefined". Appends to out. */
void sk_claude_to_string(sk_buf *out, const sk_json *v);
/* What \s matches: JavaScript's white space and line terminators. */
bool sk_claude_space(uint32_t cp);
/* String.prototype.trim. */
sk_str sk_claude_trim(sk_str s);
/* s.slice(0, units), counted in UTF-16 code units. A character cut in
 * half becomes U+FFFD, as Node writes the surrogate left alone. */
void sk_claude_slice(sk_buf *out, sk_str s, size_t units);
/* A number made of x with a truthy value: neither 0 nor NaN. */
bool sk_claude_truthy_num(double x);

/* ---- The auto-compact rule (compact.c) ---- */

#define SK_CLAUDE_COMPACT_KEYS 3
/* CLAUDE_CODE_AUTO_COMPACT_WINDOW, DISABLE_AUTO_COMPACT, DISABLE_COMPACT. */
extern const char *const sk_claude_compact_keys[SK_CLAUDE_COMPACT_KEYS];

/* What the app keeps of one settings file (settings.js keep()): the
 * statusLine as it is, autoCompactWindow when a finite number, and of
 * the env block the compaction keys whose values are strings. */
typedef struct {
    const sk_json *status_line; /* NULL when absent */
    bool has_window;
    double window;
    const char *env[SK_CLAUDE_COMPACT_KEYS]; /* NULL when absent */
} sk_claude_kept;

typedef struct {
    double window; /* compactWindow; 0 when none is configured */
    bool disabled;
} sk_claude_compaction;

/* compact.inputs(): sources lowest priority first, argv the child's
 * arguments, env the child's environment (may be NULL). */
void sk_claude_compaction_of(sk_claude_compaction *out, const sk_claude_kept *sources, size_t n,
                             char *const *argv, const sk_envp *env);
/* The window when no statusline reports it. */
double sk_claude_compact_window(const sk_claude_compaction *c, double used);
/* The auto-compact threshold, NAN when there is none. */
double sk_claude_compact_threshold(double window, const sk_claude_compaction *c);
/* "400000", "400k", "1M" as tokens; false when s is none of these. */
bool sk_claude_parse_tokens(sk_str s, double *out);

/* ---- Payloads to metrics (parse.c) ---- */

/* One metric update: a number, a string, a list or null. */
typedef struct {
    char *name;
    sk_val val;
} sk_claude_kv;
typedef SK_VEC(sk_claude_kv) sk_claude_kvs;

void sk_claude_kvs_free(sk_claude_kvs *v);
/* The update for name in v, or NULL. */
const sk_claude_kv *sk_claude_kvs_get(const sk_claude_kvs *v, const char *name);

/* 0 other, 1 read, 2 edit, 3 shell, 4 web, 5 agent. */
int sk_claude_tool_class(const sk_json *name);
/* A copy of body's metadata alone, in a: the fields of HOOK_FIELDS
 * whose values are not objects, arrays or null. */
const sk_json *sk_claude_hook_meta(sk_arena *a, const sk_json *body);
/* The bus operations of hook metadata m, into src. */
void sk_claude_from_hook(const sk_json *m, sk_source *src);
void sk_claude_from_statusline(const sk_json *j, const sk_claude_compaction *c, sk_claude_kvs *out);

/* An API usage block summed once (decision 0012). */
typedef struct {
    double uncached, write, read, output, used;
} sk_claude_usage;

void sk_claude_usage_of(const sk_json *u, sk_claude_usage *out);
/* The context family of cu (NULL: no data) in a window, with the
 * threshold NAN for none. */
void sk_claude_context_from(const sk_claude_usage *cu, double window, double threshold,
                            sk_claude_kvs *out);

/* The transcript's totals, each message.id counted once. */
typedef struct {
    sk_map seen; /* message id -> double[4] of its usage */
    double in, out, read, write;
    bool has_last;
    sk_claude_usage last;
    char *model;
} sk_claude_totals;

void sk_claude_totals_init(sk_claude_totals *t);
void sk_claude_totals_free(sk_claude_totals *t);
/* The output tokens the line added, or false when it is no main-chain
 * response with usage. */
bool sk_claude_totals_add(sk_claude_totals *t, const sk_json *obj, double *d_out);
void sk_claude_totals_metrics(const sk_claude_totals *t, sk_claude_kvs *out);
/* The cache hit ratio, NAN for none. */
double sk_claude_totals_hit_ratio(const sk_claude_totals *t);

/* The agent's task list (decision 0034). */
typedef struct sk_claude_item sk_claude_item;
typedef struct {
    sk_claude_item *it;
    char *tool_use_id;
} sk_claude_pending;

typedef struct {
    SK_VEC(sk_claude_item *) items;
    SK_VEC(sk_claude_pending) pending;
    double created;
} sk_claude_checklist;

void sk_claude_checklist_init(sk_claude_checklist *c);
void sk_claude_checklist_free(sk_claude_checklist *c);
/* One transcript line; true when the list changed. */
bool sk_claude_checklist_add(sk_claude_checklist *c, const sk_json *obj);
void sk_claude_checklist_metrics(const sk_claude_checklist *c, sk_claude_kvs *out);

/* ---- A custom title (title.c) ---- */

#define SK_CLAUDE_TITLE_MAX 120

/* The name a custom-title line sets for session, appended to out;
 * false when the line sets none. */
bool sk_claude_title_of(const sk_json *obj, const char *session, sk_buf *out);
/* The CLI's record of a rename: the transcript line, then
 * <dir>/<session>/custom-title.json. 0, or -1 with errno. */
int sk_claude_title_record(const char *transcript, const char *session, const char *name);

/* ---- The session core (core.c) ---- */

typedef struct {
    bool hooks, statusline; /* injected and not silenced */
    sk_claude_compaction compaction;
    sk_log_fn log;
    void *log_ud;
} sk_claude_facts;

typedef struct sk_claude_core sk_claude_core;

/* What the core's source declares. */
const sk_source_spec *sk_claude_source_spec(const sk_claude_facts *f);
sk_claude_core *sk_claude_core_new(sk_source *src, const sk_claude_facts *facts);
void sk_claude_core_free(sk_claude_core *c);
/* The session and transcript a payload places, as enter() does. */
bool sk_claude_core_enter(sk_claude_core *c, const sk_json *j);
bool sk_claude_core_enter_ids(sk_claude_core *c, const char *session, const char *transcript);
void sk_claude_core_hook(sk_claude_core *c, const sk_json *body);
void sk_claude_core_statusline(sk_claude_core *c, const sk_json *j);
void sk_claude_core_line(sk_claude_core *c, const char *file, const sk_json *obj, bool replayed);
/* Each NULL until known. */
const char *sk_claude_core_session(const sk_claude_core *c);
const char *sk_claude_core_transcript(const sk_claude_core *c);
const char *sk_claude_core_title(const sk_claude_core *c);

/* ---- The generated settings (settings.c) ---- */

#define SK_CLAUDE_HOOK_EVENTS 11
extern const char *const sk_claude_hook_events[SK_CLAUDE_HOOK_EVENTS];

/* Managed keys that silence what the app injects. */
enum {
    SK_CLAUDE_MANAGED_STATUSLINE = 1,
    SK_CLAUDE_MANAGED_DISABLE_HOOKS = 2,
    SK_CLAUDE_MANAGED_HOOKS_ONLY = 4,
};

typedef struct {
    const char *name; /* user, project, local or managed */
    char *file;       /* in the layers' arena */
    sk_claude_kept data;
    bool disable_all_hooks, allow_managed_hooks_only;
} sk_claude_layer;

/* Every value the layers keep lives in arena, copied from the parsed
 * file, which is dropped at once: a settings env block can hold
 * credentials. */
typedef struct {
    sk_arena arena;
    SK_VEC(sk_claude_layer) v;
} sk_claude_layers;

void sk_claude_layers_init(sk_claude_layers *ls);
void sk_claude_layers_free(sk_claude_layers *ls);
/* user, project and local, lowest first, honouring --setting-sources. */
void sk_claude_layers_read(sk_claude_layers *ls, const sk_envp *env, const char *cwd,
                           char *const *argv);
/* managed-settings.json, then the .json files of managed-settings.d by
 * name. */
void sk_claude_managed_read(sk_claude_layers *ls, const char *dir);
/* The value of --name V or --name=V in argv, else NULL. */
const char *sk_claude_arg_value(char *const *argv, const char *name);
/* The system's managed settings directory, or ctx's override. */
const char *sk_claude_managed_dir(const sk_agent_ctx *ctx);
/* The managed keys, by name, joined with ", ". */
void sk_claude_managed_names(unsigned managed, sk_buf *out);

typedef struct {
    sk_strv args;          /* --settings FILE, or nothing */
    const char *reason;    /* why nothing was injected, or NULL */
    sk_claude_compaction compaction;
    unsigned managed;      /* SK_CLAUDE_MANAGED_* */
    bool statusline, chain;
} sk_claude_prepared;

/* settings.prepare(): 0, or -1 with errno when the file could not be
 * written. */
int sk_claude_settings_prepare(const sk_agent_ctx *ctx, sk_claude_prepared *out);
void sk_claude_prepared_free(sk_claude_prepared *p);
/* The shell quoting of quote(). Appends to out. */
void sk_claude_quote(sk_buf *out, const char *s);

/* ---- The hook receiver (server.c) ---- */

/* A payload: statusline true for /statusline, else /hook. body is valid
 * during the call only. */
typedef void (*sk_claude_payload_fn)(void *ud, bool statusline, const sk_json *body);

typedef struct sk_claude_server sk_claude_server;

/* Listens on dir/hooks.sock through loop. NULL with errno when it
 * cannot. */
sk_claude_server *sk_claude_server_start(sk_loop *loop, const char *dir, const char *root,
                                         sk_claude_payload_fn fn, void *ud);
const char *sk_claude_server_socket(const sk_claude_server *s);
/* Points root/pids/<pid> at the socket. 0, or -1 with errno. */
int sk_claude_server_link(sk_claude_server *s, pid_t pid);
/* Drops connections older than 10 s at now, on sk_now_ms's clock. */
void sk_claude_server_sweep(sk_claude_server *s, uint64_t now);
/* Removes the link and the socket and stops. Idempotent. */
void sk_claude_server_close(sk_claude_server *s);
void sk_claude_server_free(sk_claude_server *s);

/* ---- The watch adapter (watch.c) ---- */

/* sk_claude_watch with a ud the part owns, freed by free_ud. */
sk_part sk_claude_watch_owned(const sk_agent_ctx *ctx, sk_claude_find_fn find, void *ud,
                              void (*free_ud)(void *));

/* ---- The provider (claude.c, carry.c) ---- */

/* What a new tab carries of argv, by Claude Code's options (0023). */
void sk_claude_carry(char *const *argv, sk_strv *args, sk_strv *dropped);

/* The Claude Code subcommand cmd runs, or NULL. */
const char *sk_claude_subcommand(char *const *cmd);
/* The transcript of `claude attach ID`: true with *file and *session
 * malloc'd, from <profile>/jobs/ID/state.json. */
bool sk_claude_attached_transcript(const sk_envp *env, const char *id, char **file, char **session);

#endif
