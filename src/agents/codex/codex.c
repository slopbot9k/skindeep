/* The Codex adapter (decision 0013), as app/metrics/codex/index.js:
 * follow this session's rollout JSONL, which discovery finds, after the
 * rollouts its history continues (decision 0026), and count its subagent
 * threads. Reads no other file and no environment of other processes.
 * Dispatched for `codex` and `codex_<suffix>` (decision 0021). A session
 * the app did not start is cut at its own start (decision 0027). */
#include "codex.h"

#include "../../core/agentdirs.h"
#include "../../core/carry.h"
#include "../../core/tail.h"

#include <stdarg.h>
#include <string.h>

/* Discovery and the subagent count run at most once a second, the
 * thread's name every 5 s. */
#define POLL_MS 1000
#define NAME_MS 5000

/* The writer source: the defaults appear once a rollout is followed. */
static const sk_metric_default DEFAULTS[] = {
    {"tools.total", {SK_VAL_NUM, 0, NULL}},
    {"agents.active", {SK_VAL_NUM, 0, NULL}},
    {"state.busy", {SK_VAL_NUM, 0, NULL}},
};
static const char *const RATES[] = {"tokens.out_rate", "tools.rate"};
static const sk_source_spec SPEC = {DEFAULTS, SK_LEN(DEFAULTS), RATES, SK_LEN(RATES)};

typedef struct {
    sk_source *src;
    sk_log_fn log;
    void *log_ud;
    uint64_t (*now)(void);
    uint64_t launched; /* the seam's clock; 0: the session is the app's */
    sk_codex_disc *disc;
    sk_codex_names names;
    char *title;
    bool named;
    uint64_t last_name;
    char *file, *thread;
    const char *how;
    bool missed;
    sk_codex_state state;
    pid_t pid;
    sk_tail *tail;
    sk_codex_bases pending; /* the rollouts to follow, the one after tail first */
    size_t next;
    bool prepared, closed;
    bool polled;
    uint64_t last_poll;
} codex;

static void logf_(codex *c, const char *fmt, ...) SK_PRINTF(2, 3);
static void logf_(codex *c, const char *fmt, ...)
{
    if (!c->log)
        return;
    sk_buf b = {0};
    va_list ap;
    va_start(ap, fmt);
    sk_buf_vprintf(&b, fmt, ap);
    va_end(ap);
    c->log(c->log_ud, b.ptr);
    sk_buf_free(&b);
}

static uint64_t clock_now(const codex *c)
{
    return c->now ? c->now() : sk_now_ms();
}

/* Discovery cuts on the wall clock, the clock of mtimes and stamps; a
 * launch on the seam's clock is moved onto it. */
static int prepare(void *self, sk_strv *args, char **notice)
{
    (void)args;
    codex *c = self;
    double launched = sk_codex_wall_ms();
    if (c->launched) {
        uint64_t now = clock_now(c);
        launched -= (double)(now > c->launched ? now - c->launched : 0);
    }
    sk_codex_disc_set_launched(c->disc, launched);
    c->prepared = true;
    *notice = NULL;
    return 0;
}

static void attach(void *self, pid_t pid)
{
    codex *c = self;
    c->pid = pid;
}

void sk_codex_on_line(void *self, const sk_json *obj, bool history)
{
    codex *c = self;
    sk_codex_state_add(&c->state, obj, c->src, history);
}

/* The next rollout of the chain: a source's referenced history, then the
 * session's own rollout. */
static void next_rollout(codex *c)
{
    const sk_codex_base *b = &c->pending.ptr[c->next++];
    sk_tail_free(c->tail);
    c->tail = sk_tail_new(b->file, sk_codex_on_line, c, b->end);
}

/* Follows the rollout once discovery finds it. The first miss is logged. */
static void follow(codex *c)
{
    if (c->closed)
        return;
    const sk_codex_found *r = sk_codex_disc_find(c->disc, c->pid);
    if (!r->file) {
        if (!c->missed) {
            sk_buf tried = {0};
            for (size_t i = 0; i < r->n_tried; i++)
                sk_buf_printf(&tried, "%s%s", i ? ", " : "", r->tried[i]);
            logf_(c, "codex adapter: no rollout found yet (tried %s)", sk_buf_cstr(&tried));
            sk_buf_free(&tried);
        }
        c->missed = true;
        return;
    }
    c->file = sk_xstrdup(r->file);
    c->thread = r->thread ? sk_xstrdup(r->thread) : NULL;
    c->how = r->how;
    logf_(c, "codex adapter: following %s (found by %s, %s)", c->file, c->how,
          r->exact ? "exact" : "inexact");
    sk_source_begin(c->src);
    sk_codex_disc_bases(c->disc, c->file, &c->pending);
    sk_codex_base own = {sk_xstrdup(c->file), -1};
    sk_vec_push(&c->pending, own);
    if (c->pending.len > 1)
        logf_(c, "codex adapter: %s continues %zu rollout(s), read first as history", c->file,
              c->pending.len - 1);
    next_rollout(c);
}

/* Subagent threads of this session: a pulse per new one, and the count
 * written in the last 20 s. */
static void count_subagents(codex *c)
{
    if (!c->thread)
        return;
    int spawned, active;
    sk_codex_disc_subagents(c->disc, c->thread, sk_codex_wall_ms(), &spawned, &active);
    for (int i = 0; i < spawned; i++) {
        sk_source_pulse(c->src, "event.agent");
        sk_source_event(c->src, SK_EV_AGENT, 0, false);
    }
    sk_source_set_num(c->src, "agents.active", active);
}

/* now - last >= every, with no last as long ago. */
static bool due(bool *done, uint64_t *last, uint64_t now, uint64_t every)
{
    if (*done && (now < *last || now - *last < every))
        return false;
    *done = true;
    *last = now;
    return true;
}

static void poll_part(void *self, uint64_t now)
{
    codex *c = self;
    if (!c->prepared || c->closed)
        return;
    if (due(&c->polled, &c->last_poll, now, POLL_MS)) {
        if (!c->file)
            follow(c);
        count_subagents(c);
    }
    if (due(&c->named, &c->last_name, now, NAME_MS)) {
        const char *name = sk_codex_names_of(&c->names, c->thread);
        free(c->title);
        c->title = name ? sk_xstrdup(name) : NULL;
    }
    if (c->tail) {
        sk_tail_poll(c->tail, now);
        while (c->next < c->pending.len && !sk_tail_replaying(c->tail))
            next_rollout(c);
    }
}

/* The rollout waiting or live. A new session has none until its first
 * prompt (decision 0013), and the strip says so. */
static void report(void *self, sk_report *out)
{
    codex *c = self;
    memset(out, 0, sizeof *out);
    out->session = c->thread;
    out->title = c->title;
    out->channels[0] = (sk_channel){"rollout", c->file ? SK_CH_LIVE : SK_CH_WAITING, c->file, c->how};
    out->n_channels = 1;
    out->notice = c->prepared && !c->file ? "meters waiting: no rollout found" : NULL;
    out->replaying = c->tail && (sk_tail_replaying(c->tail) || c->next < c->pending.len);
}

static void close_part(void *self)
{
    codex *c = self;
    c->closed = true;
    if (c->tail)
        sk_tail_stop(c->tail);
    sk_source_close(c->src);
}

static void free_part(void *self)
{
    codex *c = self;
    sk_tail_free(c->tail);
    sk_codex_bases_free(&c->pending);
    sk_codex_disc_free(c->disc);
    sk_codex_names_free(&c->names);
    sk_codex_state_free(&c->state);
    free(c->title);
    free(c->file);
    free(c->thread);
    free(c);
}

static const sk_part_ops OPS = {prepare, attach, poll_part, report, NULL, close_part, free_part};

/* /^codex(_.+)?$/: codex, or codex_ and a suffix with no line break. */
static bool matches(const char *base)
{
    if (strncmp(base, "codex", 5))
        return false;
    const char *s = base + 5;
    if (!*s)
        return true;
    if (*s != '_' || !s[1])
        return false;
    for (s++; *s; s++) {
        const unsigned char *u = (const unsigned char *)s;
        if (*u == '\n' || *u == '\r' || (u[0] == 0xE2 && u[1] == 0x80 && (u[2] == 0xA8 || u[2] == 0xA9)))
            return false;
    }
    return true;
}

static sk_part create(const sk_agent_ctx *ctx)
{
    codex *c = sk_xcalloc(1, sizeof *c);
    c->src = sk_bus_source(ctx->bus, &SPEC);
    c->log = ctx->log;
    c->log_ud = ctx->log_ud;
    c->now = ctx->now;
    c->launched = ctx->launched;
    char *home = sk_codex_home(ctx->env);
    char *const *argv = ctx->cmd && ctx->cmd[0] ? ctx->cmd + 1 : NULL;
    c->disc = sk_codex_disc_new(home, argv, ctx->cwd, sk_codex_wall_ms());
    free(home);
    sk_codex_names_init(&c->names, ctx->env);
    return (sk_part){&OPS, c};
}

/* The options of Codex CLI 0.162.1 (`codex --help`) a new tab carries
 * (decision 0023), as app/metrics/codex/carry.js: those that set up any
 * session the same way, the restricting ones above all. Not carried:
 * subcommands such as resume, fork and exec with their operands, --image
 * (it belongs to the first prompt), --cd (the tab has its folder),
 * --worktree, and any option missing here. */
static const sk_option OPTIONS[] = {
    {"--strict-config", SK_ARITY_FLAG},
    {"--oss", SK_ARITY_FLAG},
    {"--approve-for-me", SK_ARITY_FLAG},
    {"--dangerously-bypass-approvals-and-sandbox", SK_ARITY_FLAG},
    {"--dangerously-bypass-hook-trust", SK_ARITY_FLAG},
    {"--search", SK_ARITY_FLAG},
    {"--no-alt-screen", SK_ARITY_FLAG},
    {"--no-daemon", SK_ARITY_FLAG},
    {"-c", SK_ARITY_ONE},
    {"--config", SK_ARITY_ONE},
    {"--enable", SK_ARITY_ONE},
    {"--disable", SK_ARITY_ONE},
    {"--remote", SK_ARITY_ONE},
    {"--remote-auth-token-env", SK_ARITY_ONE},
    {"-m", SK_ARITY_ONE},
    {"--model", SK_ARITY_ONE},
    {"--local-provider", SK_ARITY_ONE},
    {"-p", SK_ARITY_ONE},
    {"--profile", SK_ARITY_ONE},
    {"-s", SK_ARITY_ONE},
    {"--sandbox", SK_ARITY_ONE},
    {"--add-dir", SK_ARITY_ONE},
    {"-a", SK_ARITY_ONE},
    {"--ask-for-approval", SK_ARITY_ONE},
};

static void carry(char *const *argv, sk_strv *args, sk_strv *dropped)
{
    sk_carry_pick(argv, OPTIONS, SK_LEN(OPTIONS), args, dropped);
}

const sk_provider sk_provider_codex = {"codex", matches, create, carry};
