/* The Claude Code adapter, as app/metrics/claude/index.js has it:
 * statusline JSON, command hooks and the transcript tail (decisions
 * 0009, 0030), dispatched for `claude` and `claude_<suffix>` (decision
 * 0021). Prepare starts the receiver and writes the settings, the
 * payloads feed the session core, and the core names the transcript to
 * follow. A Claude Code subcommand gets nothing injected (decision
 * 0030): `attach ID` follows the attached session's transcript, read
 * only, and the others run on Generic alone. A rename made in the tab
 * goes into the CLI's own record (decision 0032). */
#include "internal.h"

#include "../../base/paths.h"
#include "../../core/agentdirs.h"
#include "../../core/tail.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    sk_agent_ctx ctx;
    sk_claude_server *server;
    sk_source *src;
    sk_claude_core *core;
    sk_tail *tail;
    char *tail_file;
    char *notice;
    bool closed;
    /* Which channels were injected and which have delivered. */
    bool armed_hooks, armed_statusline, seen_hooks, seen_statusline;
} adapter;

static void on_line(void *ud, const sk_json *obj, bool history)
{
    adapter *a = ud;
    sk_claude_core_line(a->core, a->tail_file, obj, history);
}

/* Where Claude Code keeps a transcript: an absolute path to a .jsonl
 * file, with no . or .. in it. A payload naming anything else is not
 * followed, and no title is appended to what it names. */
static bool transcript_ok(const char *file)
{
    size_t n = file ? strlen(file) : 0;
    return n > 6 && !strcmp(file + n - 6, ".jsonl") && sk_path_clean_absolute(file);
}

/* The tail is swapped only after the core has taken the payload, so a
 * history replay never runs inside one. */
static void follow(adapter *a)
{
    const char *file = sk_claude_core_transcript(a->core);
    if (!file || !transcript_ok(file) || (a->tail && !strcmp(a->tail_file, file)))
        return;
    if (a->tail) {
        sk_tail_stop(a->tail);
        sk_tail_free(a->tail);
    }
    free(a->tail_file);
    a->tail_file = sk_xstrdup(file);
    a->tail = sk_tail_new(a->tail_file, on_line, a, -1);
}

/* A payload on one of the injected channels. */
static void on_payload(void *ud, bool statusline, const sk_json *body)
{
    adapter *a = ud;
    if (a->closed || !a->core)
        return;
    if (statusline) {
        a->seen_statusline = true;
        sk_claude_core_statusline(a->core, body);
    } else {
        a->seen_hooks = true;
        sk_claude_core_hook(a->core, body);
    }
    follow(a);
}

static int prepare(void *self, sk_strv *args, char **notice)
{
    adapter *a = self;
    const sk_agent_ctx *ctx = &a->ctx;
    a->server = sk_claude_server_start(ctx->loop, ctx->run_dir, ctx->root, on_payload, a);
    if (!a->server) {
        sk_claude_logf(ctx->log, ctx->log_ud, "claude adapter: the hook socket did not start: %s",
                       strerror(errno));
        return -1;
    }
    sk_claude_prepared r;
    if (sk_claude_settings_prepare(ctx, &r) != 0) {
        sk_claude_logf(ctx->log, ctx->log_ud, "claude adapter: the settings were not written: %s",
                       strerror(errno));
        sk_claude_prepared_free(&r);
        return -1;
    }
    /* Managed settings silence what is injected: hooks that never fire
     * give no hook meters, as with nothing injected (decision 0010). */
    bool hooks_off = r.managed & (SK_CLAUDE_MANAGED_DISABLE_HOOKS | SK_CLAUDE_MANAGED_HOOKS_ONLY);
    a->armed_hooks = !r.reason && !hooks_off;
    a->armed_statusline = r.statusline && !(r.managed & SK_CLAUDE_MANAGED_STATUSLINE);
    sk_buf n = {0};
    if (r.reason)
        sk_buf_printf(&n, "meters limited: %s", r.reason);
    else if (r.managed)
        sk_buf_append_c(&n, "meters limited: managed settings");
    a->notice = n.ptr;
    sk_claude_facts facts = {a->armed_hooks, a->armed_statusline, r.compaction, ctx->log, ctx->log_ud};
    a->src = sk_bus_source(ctx->bus, sk_claude_source_spec(&facts));
    a->core = sk_claude_core_new(a->src, &facts);
    sk_claude_logf(ctx->log, ctx->log_ud, "claude adapter: injected=%s statusline=%s chain=%s",
                   r.reason ? "false" : "true", r.statusline ? "true" : "false", r.chain ? "true" : "false");
    if (r.managed) {
        sk_buf keys = {0};
        sk_claude_managed_names(r.managed, &keys);
        sk_claude_logf(ctx->log, ctx->log_ud, "claude adapter: managed settings: %s override what the app injects",
                       keys.ptr);
        sk_buf_free(&keys);
    }
    sk_strv_extend(args, r.args.ptr);
    *notice = a->notice ? sk_xstrdup(a->notice) : NULL;
    sk_claude_prepared_free(&r);
    return 0;
}

/* The relays find this tab by its CLI's pid (decision 0030). */
static void attach(void *self, pid_t pid)
{
    adapter *a = self;
    if (!a->server || a->closed)
        return;
    if (sk_claude_server_link(a->server, pid) != 0)
        sk_claude_logf(a->ctx.log, a->ctx.log_ud, "claude adapter: no link for pid %ld: %s", (long)pid,
                       strerror(errno));
}

/* Each tick: the transcript's appended lines. */
static void poll_part(void *self, uint64_t now)
{
    adapter *a = self;
    if (a->closed)
        return;
    if (a->server)
        sk_claude_server_sweep(a->server, sk_now_ms());
    if (a->tail)
        sk_tail_poll(a->tail, now);
}

static sk_ch_state state_of(bool armed, bool seen)
{
    return !armed ? SK_CH_OFF : seen ? SK_CH_LIVE : SK_CH_WAITING;
}

static void report(void *self, sk_report *out)
{
    adapter *a = self;
    memset(out, 0, sizeof *out);
    const char *transcript = a->core ? sk_claude_core_transcript(a->core) : NULL;
    out->session = a->core ? sk_claude_core_session(a->core) : NULL;
    out->title = a->core ? sk_claude_core_title(a->core) : NULL;
    out->channels[0] = (sk_channel){"hooks", state_of(a->armed_hooks, a->seen_hooks), NULL, NULL};
    out->channels[1] = (sk_channel){"statusline", state_of(a->armed_statusline, a->seen_statusline), NULL, NULL};
    out->channels[2] = (sk_channel){"transcript", a->tail ? SK_CH_LIVE : SK_CH_WAITING, transcript,
                                    transcript ? "payload" : NULL};
    out->n_channels = 3;
    out->notice = a->notice;
    out->replaying = a->tail && sk_tail_replaying(a->tail);
}

/* Recorded once the session and its transcript are known. */
static bool rename_part(void *self, const char *name)
{
    adapter *a = self;
    if (a->closed || !a->core || !sk_claude_core_session(a->core) || !sk_claude_core_transcript(a->core) ||
        !transcript_ok(sk_claude_core_transcript(a->core)))
        return false;
    if (sk_claude_title_record(sk_claude_core_transcript(a->core), sk_claude_core_session(a->core), name) != 0) {
        sk_claude_logf(a->ctx.log, a->ctx.log_ud, "claude adapter: rename not recorded: %s", strerror(errno));
        return false;
    }
    return true;
}

static void close_part(void *self)
{
    adapter *a = self;
    a->closed = true;
    if (a->tail)
        sk_tail_stop(a->tail);
    sk_claude_server_close(a->server);
    if (a->src)
        sk_source_close(a->src);
}

static void free_part(void *self)
{
    adapter *a = self;
    close_part(a);
    sk_tail_free(a->tail);
    sk_claude_server_free(a->server);
    sk_claude_core_free(a->core);
    free(a->tail_file);
    free(a->notice);
    free(a);
}

static const sk_part_ops OPS = {prepare, attach, poll_part, report, rename_part, close_part, free_part, NULL};

/* ---- Subcommands ---- */

/* The top-level commands of Claude Code 2.1.296: those `claude --help`
 * lists, their aliases, and the hidden daemon, design-login, project,
 * remote-control and sandbox. Claude Code takes a subcommand only as the
 * first argument (measured 2026-10-12), so an injected option in front
 * would turn it into a prompt. */
static const char *const SUBCOMMANDS[] = {
    "agents", "attach", "auth", "auto-mode", "daemon", "design-login", "doctor", "gateway",
    "import", "install", "kill", "logs", "mcp", "plugin", "plugins", "project", "purge", "rc",
    "remote-control", "respawn", "rm", "sandbox", "setup-token", "stop", "ultrareview",
    "update", "upgrade",
};

const char *sk_claude_subcommand(char *const *cmd)
{
    if (!cmd || !cmd[0] || !cmd[1])
        return NULL;
    for (size_t i = 0; i < SK_LEN(SUBCOMMANDS); i++)
        if (!strcmp(cmd[1], SUBCOMMANDS[i]))
            return SUBCOMMANDS[i];
    return NULL;
}

/* /^[A-Za-z0-9-]+$/ */
static bool plain_id(sk_str s)
{
    if (!s.len)
        return false;
    for (size_t i = 0; i < s.len; i++) {
        char c = s.ptr[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
            return false;
    }
    return true;
}

/* The session id and folder of <profile>/jobs/ID/state.json, every
 * other field dropped at once. False when ID names no job, such as a
 * name. */
bool sk_claude_attached_transcript(const sk_envp *env, const char *id, char **file, char **session)
{
    if (!id || !plain_id(sk_str_c(id)))
        return false;
    char *dir = sk_claude_config_dir(env);
    if (!dir)
        return false;
    sk_buf path = {0}, text = {0};
    sk_buf_printf(&path, "%s/jobs/%s/state.json", dir, id);
    bool ok = false;
    int fd = open(path.ptr, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        char chunk[4096];
        ssize_t r;
        while ((r = read(fd, chunk, sizeof chunk)) > 0 || (r < 0 && errno == EINTR))
            if (r > 0)
                sk_buf_append(&text, chunk, (size_t)r);
        close(fd);
        sk_arena arena;
        sk_arena_init(&arena, 0);
        const sk_json *d = sk_json_parse(&arena, text.ptr ? text.ptr : "", text.len, NULL);
        sk_str sid, cwd;
        if (sk_json_as_string(sk_json_get(d, "sessionId"), &sid) && plain_id(sid) &&
            sk_json_as_string(sk_json_get(d, "cwd"), &cwd)) {
            sk_buf f = {0};
            sk_buf_printf(&f, "%s/projects/", dir);
            sk_claude_slug(&f, cwd);
            sk_buf_printf(&f, "/%.*s.jsonl", (int)sid.len, sid.ptr);
            *file = f.ptr;
            *session = sk_xstrndup(sid.ptr, sid.len);
            ok = true;
        }
        sk_arena_free(&arena);
    }
    sk_buf_free(&path);
    sk_buf_free(&text);
    free(dir);
    return ok;
}

typedef struct {
    const sk_envp *env;
    char *id;
} attached;

/* The attached session's transcript once it exists. */
static bool find_attached(void *ud, char **file, char **session)
{
    attached *t = ud;
    if (!sk_claude_attached_transcript(t->env, t->id, file, session))
        return false;
    struct stat st;
    if (stat(*file, &st) == 0)
        return true;
    free(*file);
    free(*session);
    *file = *session = NULL;
    return false;
}

static void free_attached(void *ud)
{
    attached *t = ud;
    free(t->id);
    free(t);
}

/* ---- The provider ---- */

static bool matches(const char *base)
{
    size_t n = strlen("claude");
    return !strncmp(base, "claude", n) && (base[n] == 0 || (base[n] == '_' && base[n + 1]));
}

static sk_part create(const sk_agent_ctx *ctx)
{
    const char *sub = sk_claude_subcommand(ctx->cmd);
    if (sub && !strcmp(sub, "attach")) {
        attached *t = sk_xcalloc(1, sizeof *t);
        t->env = ctx->env;
        t->id = ctx->cmd[2] ? sk_xstrdup(ctx->cmd[2]) : NULL;
        return sk_claude_watch_owned(ctx, find_attached, t, free_attached);
    }
    if (sub)
        return sk_provider_plain.create(ctx);
    adapter *a = sk_xcalloc(1, sizeof *a);
    a->ctx = *ctx;
    return (sk_part){&OPS, a};
}

const sk_provider sk_provider_claude = {"claude", matches, create, sk_claude_carry};
