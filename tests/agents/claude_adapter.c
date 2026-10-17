/* The Claude adapter on temp dirs, as test/claude-adapter.test.js has
 * it: prepare, the three channels through the built skinned-relay, and
 * close (decisions 0009, 0030). Beside it the Claude rows of
 * test/agent-contract.test.js, on a fake clock that ticks the part as
 * the seam does, the subcommands of test/background.test.js, and the
 * rename of decision 0032. The hook socket is served by an sk_loop the
 * test runs while it waits. */
#include "../../src/agents/agent.h"
#include "../../src/agents/claude/internal.h"
#include "../../src/base/http.h"
#include "../../src/base/paths.h"
#include "../test.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

static uint64_t fake;
static uint64_t fake_now(void)
{
    return fake;
}

typedef struct {
    char *dir, *cfg, *run, *root, *store, *no_managed, *file, *target, *settings;
    sk_envp env;
    sk_loop *loop;
    sk_bus *bus;
    sk_agent_ctx ctx;
    sk_part part;
    bool open;
    uint64_t next_tick;
} harness;

static char *const CLAUDE[] = {"claude", NULL};

static void write_file(const char *path, const char *text, size_t n)
{
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f) {
        fwrite(text, 1, n, f);
        fclose(f);
    }
}

static void append_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "ab");
    CHECK(f != NULL);
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

/* Temp profile, project (its own git root), run dir, runtime root and
 * store, and a context for cmd. Nothing is prepared yet. */
static void harness_init(harness *h, char *const *cmd)
{
    memset(h, 0, sizeof *h);
    h->dir = sk_xstrdup(sk_test_tmpdir());
    h->cfg = sk_path_join(h->dir, "cfg");
    h->run = sk_path_join(h->dir, "run");
    h->root = sk_path_join(h->dir, "root");
    h->store = sk_path_join(h->dir, "store");
    h->no_managed = sk_path_join(h->dir, "no-managed");
    h->file = sk_path_join(h->dir, "session.jsonl");
    char *git = sk_path_join(h->dir, ".git");
    CHECK(sk_mkdirs(h->cfg, 0700) == 0 && sk_mkdirs(h->run, 0700) == 0 && sk_mkdirs(git, 0700) == 0);
    free(git);
    sk_buf t = {0};
    sk_buf_printf(&t, "%s/pids/%ld", h->root, (long)getpid());
    h->target = t.ptr;
    sk_envp_from(&h->env, NULL);
    sk_envp_set(&h->env, "CLAUDE_CONFIG_DIR", h->cfg);
    h->loop = sk_loop_new();
    h->bus = sk_bus_new(NULL, NULL);
    fake = 1000000;
    h->next_tick = fake + SK_AGENT_TICK_MS;
    sk_agent_ctx *c = &h->ctx;
    c->cmd = cmd ? cmd : CLAUDE;
    c->cwd = h->dir;
    c->env = &h->env;
    c->run_dir = h->run;
    c->root = h->root;
    c->store = h->store;
    const char *relay = getenv("SK_RELAY_BIN");
    c->relay = relay && *relay ? relay : "skinned-relay";
    c->statusline = SK_STATUSLINE_ALWAYS;
    c->bus = h->bus;
    c->loop = h->loop;
    c->managed_dir = h->no_managed;
    c->now = fake_now;
}

/* The part prepared and linked to this test's pid, as a tab links its
 * CLI's: a relay this test runs finds it by ancestry. */
static void harness_ready(harness *h)
{
    h->part = sk_provider_claude.create(&h->ctx);
    h->open = true;
    sk_strv args = {0};
    char *notice = NULL;
    CHECK(h->part.ops->prepare(h->part.self, &args, &notice) == 0);
    CHECK(args.len == 2 && !strcmp(args.ptr[0], "--settings"));
    if (args.len == 2)
        h->settings = sk_xstrdup(args.ptr[1]);
    h->part.ops->attach(h->part.self, getpid());
    write_file(h->file, "", 0);
    free(notice);
    sk_strv_free(&args);
}

static void harness_free(harness *h)
{
    if (h->open) {
        h->part.ops->close(h->part.self);
        h->part.ops->free(h->part.self);
    }
    sk_bus_free(h->bus);
    sk_loop_free(h->loop);
    char **all[] = {&h->dir, &h->cfg, &h->run, &h->root, &h->store, &h->no_managed, &h->file, &h->target,
                    &h->settings};
    for (size_t i = 0; i < SK_LEN(all); i++)
        free(*all[i]);
    sk_envp_free(&h->env);
}

/* The fake clock forward by ms: each 250 ms tick polls the part, then
 * the bus, as the seam's timer does. */
static void advance(harness *h, uint64_t ms)
{
    uint64_t end = fake + ms;
    while (h->next_tick <= end) {
        fake = h->next_tick;
        h->next_tick += SK_AGENT_TICK_MS;
        h->part.ops->poll(h->part.self, fake);
        sk_bus_tick(h->bus, fake);
    }
    fake = end;
}

/* Runs the loop until cond holds, at most 5 s of real time. */
static bool until(harness *h, bool (*cond)(harness *))
{
    uint64_t end = sk_now_ms() + 5000;
    while (!cond(h)) {
        if (sk_now_ms() > end)
            return false;
        sk_loop_once(h->loop, 10);
    }
    return true;
}

static void pump(harness *h, int ms)
{
    uint64_t end = sk_now_ms() + (uint64_t)ms;
    while (sk_now_ms() < end)
        sk_loop_once(h->loop, 10);
}

/* POSTs body to route at target, a socket or a link to one, as a relay
 * would, running the loop meanwhile. The answer's status, or -1. */
static int post(harness *h, const char *target, const char *route, const char *body)
{
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, target, SK_MIN(strlen(target), sizeof addr.sun_path - 1));
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0 || connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
        if (fd >= 0)
            close(fd);
        return -1;
    }
    sk_buf req = {0}, resp = {0}, path = {0};
    sk_buf_printf(&path, "/%s", route);
    sk_http_write_post(&req, path.ptr, strlen(body));
    sk_buf_append_c(&req, body);
    /* The server runs on this thread: write as it reads. */
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    size_t sent = 0;
    uint64_t end = sk_now_ms() + 5000;
    while (sk_now_ms() < end) {
        if (sent < req.len) {
            ssize_t w = write(fd, req.ptr + sent, req.len - sent);
            if (w > 0)
                sent += (size_t)w;
        }
        sk_loop_once(h->loop, 10);
        if (sent < req.len)
            continue;
        char buf[512];
        ssize_t r = read(fd, buf, sizeof buf);
        if (r > 0)
            sk_buf_append(&resp, buf, (size_t)r);
        else if (r == 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
            break;
    }
    close(fd);
    int status = -1;
    if (resp.len > 12 && !strncmp(resp.ptr, "HTTP/1.1 ", 9))
        status = atoi(resp.ptr + 9);
    sk_buf_free(&req);
    sk_buf_free(&resp);
    sk_buf_free(&path);
    return status;
}

/* True when nothing listens at target any more. */
static bool refused(const char *target)
{
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, target, SK_MIN(strlen(target), sizeof addr.sun_path - 1));
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    bool r = connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0 && (errno == ECONNREFUSED || errno == ENOENT);
    close(fd);
    return r;
}

static double num(const sk_bus *b, const char *name)
{
    const sk_val *v = sk_bus_get(b, name);
    return v && v->type == SK_VAL_NUM ? v->num : NAN;
}

static char *snapshot(const sk_bus *b)
{
    sk_buf out = {0};
    sk_jw w;
    sk_jw_init(&w, &out, 0);
    sk_bus_write(b, &w);
    return out.ptr;
}

static const sk_channel *channel(const sk_report *r, const char *name)
{
    for (size_t i = 0; i < r->n_channels; i++)
        if (!strcmp(r->channels[i].name, name))
            return &r->channels[i];
    return NULL;
}

static sk_report report(harness *h)
{
    sk_report r;
    h->part.ops->report(h->part.self, &r);
    return r;
}

/* ---- agent-contract.test.js, the Claude rows ---- */

static const char *const DEFAULTS[] = {"tools.total", "agents.active", "state.busy", "tools.rate", "tokens.out_rate"};

static bool first_landed(harness *h)
{
    return sk_bus_get(h->bus, "tools.total") && !sk_bus_get(h->bus, "tools.Read");
}

/* The first datum: a SessionStart hook naming the transcript. */
static void first(harness *h, const char *session)
{
    sk_buf body = {0};
    sk_buf_printf(&body,
                  "{\"hook_event_name\":\"SessionStart\",\"source\":\"startup\",\"session_id\":\"%s\","
                  "\"transcript_path\":\"%s\"}",
                  session, h->file);
    CHECK_INT(post(h, h->target, "hook", body.ptr), 204);
    CHECK(until(h, first_landed));
    sk_buf_free(&body);
}

static void live(harness *h, int n)
{
    sk_buf line = {0};
    sk_buf_printf(&line,
                  "{\"type\":\"assistant\",\"message\":{\"id\":\"msg_live_%d\",\"model\":\"claude-test\",\"usage\":"
                  "{\"input_tokens\":1,\"output_tokens\":5000,\"cache_creation_input_tokens\":0,"
                  "\"cache_read_input_tokens\":0}}}\n",
                  n);
    append_file(h->file, line.ptr);
    sk_buf_free(&line);
}

static void test_rows_1_and_2_defaults_absent_before_the_first_datum(void)
{
    harness h;
    harness_init(&h, NULL);
    harness_ready(&h);
    advance(&h, 1000);
    for (size_t i = 0; i < SK_LEN(DEFAULTS); i++)
        CHECK(sk_bus_get(h.bus, DEFAULTS[i]) == NULL);
    first(&h, "s1");
    for (size_t i = 0; i < SK_LEN(DEFAULTS); i++)
        CHECK(sk_bus_get(h.bus, DEFAULTS[i]) != NULL);
    harness_free(&h);
}

static void test_rows_1_and_2_channels_waiting_then_live_with_their_target(void)
{
    harness h;
    harness_init(&h, NULL);
    harness_ready(&h);
    advance(&h, 1000);
    sk_report r = report(&h);
    CHECK_INT(r.n_channels, 3);
    CHECK_INT(channel(&r, "hooks")->state, SK_CH_WAITING);
    CHECK_INT(channel(&r, "statusline")->state, SK_CH_WAITING);
    CHECK_INT(channel(&r, "transcript")->state, SK_CH_WAITING);
    first(&h, "s1");
    advance(&h, 1000);
    r = report(&h);
    CHECK_INT(channel(&r, "hooks")->state, SK_CH_LIVE);
    CHECK_INT(channel(&r, "transcript")->state, SK_CH_LIVE);
    CHECK_STR(channel(&r, "transcript")->target, h.file);
    CHECK_STR(channel(&r, "transcript")->how, "payload");
    CHECK_STR(r.session, "s1");
    harness_free(&h);
}

static void test_row_3_the_output_rate_rises_with_a_live_sample_and_falls_after_10_s(void)
{
    harness h;
    harness_init(&h, NULL);
    harness_ready(&h);
    first(&h, "s1");
    advance(&h, 500);
    live(&h, 1);
    advance(&h, 500);
    CHECK(num(h.bus, "tokens.out_rate") > 0);
    advance(&h, 10000);
    CHECK(num(h.bus, "tokens.out_rate") == 0);
    harness_free(&h);
}

static bool read_counted(harness *h)
{
    return num(h->bus, "tools.Read") == 1;
}

static void test_row_4_a_new_session_resets_its_names_and_reseeds_in_the_same_call(void)
{
    harness h;
    harness_init(&h, NULL);
    harness_ready(&h);
    first(&h, "s1");
    sk_buf body = {0};
    sk_buf_printf(&body,
                  "{\"hook_event_name\":\"PreToolUse\",\"tool_name\":\"Read\",\"tool_use_id\":\"t1\",\"session_id\":"
                  "\"s1\",\"transcript_path\":\"%s\"}",
                  h.file);
    CHECK_INT(post(&h, h.target, "hook", body.ptr), 204);
    CHECK(until(&h, read_counted));
    advance(&h, 250);
    CHECK(num(h.bus, "tools.rate") == 1);
    first(&h, "s2");
    CHECK(sk_bus_get(h.bus, "tools.Read") == NULL);
    for (size_t i = 0; i < SK_LEN(DEFAULTS); i++)
        CHECK(num(h.bus, DEFAULTS[i]) == 0);
    CHECK(num(h.bus, "event.tool") == 1);
    sk_buf_free(&body);
    harness_free(&h);
}

static void test_row_5_close_twice_nothing_reaches_the_bus_after_it(void)
{
    harness h;
    harness_init(&h, NULL);
    harness_ready(&h);
    first(&h, "s1");
    advance(&h, 500);
    h.part.ops->close(h.part.self);
    h.part.ops->close(h.part.self);
    char *before = snapshot(h.bus);
    live(&h, 1);
    advance(&h, 2000);
    pump(&h, 600);
    char *after = snapshot(h.bus);
    CHECK_STR(after, before);
    CHECK(refused(h.target));
    free(before);
    free(after);
    harness_free(&h);
}

static void test_row_6_nothing_moves_without_the_clock(void)
{
    harness h;
    harness_init(&h, NULL);
    harness_ready(&h);
    first(&h, "s1");
    advance(&h, 1000);
    CHECK(sk_bus_get(h.bus, "tokens.out") == NULL);
    live(&h, 1);
    pump(&h, 600);
    CHECK(sk_bus_get(h.bus, "tokens.out") == NULL);
    advance(&h, 500);
    CHECK(num(h.bus, "tokens.out") == 5000);
    harness_free(&h);
}

/* About 18 MiB of lines no adapter counts: three chunks of the tail. */
static void write_filler(const char *path)
{
    sk_buf line = {0};
    sk_buf_append_c(&line, "{\"type\":\"filler\",\"pad\":\"");
    for (int i = 0; i < 1000; i++)
        sk_buf_putc(&line, 'x');
    sk_buf_append_c(&line, "\"}\n");
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    for (size_t done = 0; f && done < 18u * 1024 * 1024; done += line.len)
        fwrite(line.ptr, 1, line.len, f);
    if (f)
        fclose(f);
    sk_buf_free(&line);
}

static void test_row_8_the_report_says_replaying_while_the_history_is_read(void)
{
    harness h;
    harness_init(&h, NULL);
    harness_ready(&h);
    CHECK(!report(&h).replaying);
    write_filler(h.file);
    first(&h, "s1");
    CHECK(report(&h).replaying);
    advance(&h, 500);
    CHECK(!report(&h).replaying);
    harness_free(&h);
}

static bool find_watched(void *ud, char **file, char **session)
{
    *file = sk_xstrdup(ud);
    *session = sk_xstrdup("w1");
    return true;
}

static void test_watch_row_8_replaying_while_the_transcript_history_is_read(void)
{
    harness h;
    harness_init(&h, NULL);
    char *file = sk_path_join(h.dir, "watched.jsonl");
    write_filler(file);
    h.part = sk_claude_watch(&h.ctx, find_watched, file);
    h.open = true;
    sk_strv args = {0};
    char *notice = NULL;
    CHECK(h.part.ops->prepare(h.part.self, &args, &notice) == 0);
    CHECK(args.len == 0 && notice == NULL);
    CHECK_STR(report(&h).notice, "meters waiting: no transcript found");
    advance(&h, 250);
    sk_report r = report(&h);
    CHECK_INT(r.n_channels, 1);
    CHECK_STR(r.channels[0].target, file);
    CHECK_STR(r.channels[0].how, "folder");
    CHECK(r.replaying);
    CHECK(r.notice == NULL);
    advance(&h, 500);
    CHECK(!report(&h).replaying);
    CHECK_STR(report(&h).session, "w1");
    free(file);
    sk_strv_free(&args);
    harness_free(&h);
}

static void test_with_settings_given_hooks_and_statusline_are_off_and_the_notice_says_why(void)
{
    harness h;
    char *cmd[] = {"claude", "--settings", "/x.json", NULL};
    harness_init(&h, cmd);
    h.ctx.now = NULL;
    sk_agent *a = sk_agent_open(&h.ctx);
    CHECK_STR(sk_agent_kind(a), "claude");
    sk_strv args = {0};
    char *notice = NULL;
    sk_agent_prepare(a, &args, &notice);
    sk_report r;
    sk_agent_report(a, &r);
    CHECK_INT(channel(&r, "hooks")->state, SK_CH_OFF);
    CHECK_INT(channel(&r, "statusline")->state, SK_CH_OFF);
    CHECK_INT(channel(&r, "transcript")->state, SK_CH_WAITING);
    CHECK_STR(r.notice, "meters limited: --settings given");
    sk_agent_free(a);
    free(notice);
    sk_strv_free(&args);
    harness_free(&h);
}

static void test_a_failed_prepare_keeps_its_notice_in_the_report(void)
{
    harness h;
    harness_init(&h, NULL);
    char *missing = sk_path_join(h.dir, "missing");
    h.ctx.run_dir = missing;
    h.ctx.now = NULL;
    sk_agent *a = sk_agent_open(&h.ctx);
    sk_strv args = {0};
    char *notice = NULL;
    sk_agent_prepare(a, &args, &notice);
    CHECK_STR(notice, SK_AGENT_FAILED);
    CHECK_INT(args.len, 0);
    sk_report r;
    sk_agent_report(a, &r);
    CHECK_STR(r.notice, SK_AGENT_FAILED);
    sk_agent_free(a);
    free(notice);
    free(missing);
    sk_strv_free(&args);
    harness_free(&h);
}

/* ---- background.test.js, the subcommands ---- */

static void test_claude_attach_follows_the_jobs_transcript_keeping_two_fields(void)
{
    harness h;
    harness_init(&h, NULL);
    char *job = sk_path_join(h.cfg, "jobs/5e3a91c0");
    CHECK(sk_mkdirs(job, 0700) == 0);
    char *state = sk_path_join(job, "state.json");
    const char *text = "{\"sessionId\":\"5e3a91c0-7b2d-4c1e-9f00-1a2b3c4d5e6f\",\"cwd\":\"/home/u/Repos\","
                       "\"bridgeOwnerAccountUuid\":\"x\",\"respawnFlags\":[\"--settings\",\"f\"]}";
    write_file(state, text, strlen(text));
    char *file = NULL, *session = NULL;
    CHECK(sk_claude_attached_transcript(&h.env, "5e3a91c0", &file, &session));
    sk_buf want = {0};
    sk_buf_printf(&want, "%s/projects/-home-u-Repos/5e3a91c0-7b2d-4c1e-9f00-1a2b3c4d5e6f.jsonl", h.cfg);
    CHECK_STR(file, want.ptr);
    CHECK_STR(session, "5e3a91c0-7b2d-4c1e-9f00-1a2b3c4d5e6f");
    free(file);
    free(session);
    CHECK(!sk_claude_attached_transcript(&h.env, "Sort", &file, &session));
    CHECK(!sk_claude_attached_transcript(&h.env, "../jobs", &file, &session));
    sk_buf_free(&want);
    free(state);
    free(job);
    harness_free(&h);
}

static void test_a_subcommand_gets_nothing_injected(void)
{
    harness h;
    char *attach[] = {"claude", "attach", "5e3a91c0", NULL}, *mcp[] = {"claude", "mcp", "list", NULL};
    harness_init(&h, attach);
    h.ctx.now = NULL;
    sk_agent *a = sk_agent_open(&h.ctx);
    sk_strv args = {0};
    char *notice = NULL;
    sk_agent_prepare(a, &args, &notice);
    CHECK(args.len == 0 && notice == NULL);
    sk_report r;
    sk_agent_report(a, &r);
    CHECK_INT(r.n_channels, 3); /* the transcript, then Generic's pty and proc */
    CHECK_STR(r.channels[0].name, "transcript");
    sk_agent_free(a);
    h.ctx.cmd = mcp;
    a = sk_agent_open(&h.ctx);
    sk_agent_prepare(a, &args, &notice);
    CHECK(args.len == 0 && notice == NULL);
    sk_agent_report(a, &r);
    CHECK_INT(r.n_channels, 2); /* Generic's alone */
    CHECK_STR(r.channels[0].name, "pty");
    sk_agent_free(a);
    CHECK(access(h.store, F_OK) != 0);
    sk_strv_free(&args);
    harness_free(&h);
}

/* ---- Rename (decision 0032) ---- */

static bool titled(harness *h)
{
    h->part.ops->poll(h->part.self, fake += 500);
    const char *t = report(h).title;
    return t && !strcmp(t, "the plan");
}

static void test_a_rename_goes_into_the_transcript_and_custom_title_json(void)
{
    harness h;
    harness_init(&h, NULL);
    harness_ready(&h);
    CHECK(!h.part.ops->rename(h.part.self, "too early")); /* no session yet */
    first(&h, "sess-1");
    CHECK(h.part.ops->rename(h.part.self, "the plan"));
    char *text = sk_test_read(h.file, NULL);
    CHECK_STR(text, "{\"type\":\"custom-title\",\"customTitle\":\"the plan\",\"sessionId\":\"sess-1\"}\n");
    free(text);
    char *json = sk_path_join(h.dir, "sess-1/custom-title.json");
    text = sk_test_read(json, NULL);
    CHECK_STR(text, "{\"customTitle\":\"the plan\"}");
    free(text);
    free(json);
    CHECK(until(&h, titled)); /* the tail reads the line back */
    h.part.ops->close(h.part.self);
    CHECK(!h.part.ops->rename(h.part.self, "after"));
    harness_free(&h);
}

/* ---- claude-adapter.test.js, through the built relay ---- */

typedef struct {
    int code;
    char *out, *err;
} ran;

/* Runs a relay command as Claude Code runs it, /bin/sh -c with the JSON
 * on stdin, serving the hook socket until it exits. */
static ran run_relay(harness *h, const char *command, const char *body)
{
    static int n;
    sk_buf in = {0}, out = {0}, err = {0};
    sk_buf_printf(&in, "%s/in-%d", h->dir, n);
    sk_buf_printf(&out, "%s/out-%d", h->dir, n);
    sk_buf_printf(&err, "%s/err-%d", h->dir, n++);
    write_file(in.ptr, body, strlen(body));
    ran r = {-1, NULL, NULL};
    pid_t pid = fork();
    if (pid == 0) {
        int i = open(in.ptr, O_RDONLY), o = open(out.ptr, O_WRONLY | O_CREAT | O_TRUNC, 0600),
            e = open(err.ptr, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (i < 0 || o < 0 || e < 0 || dup2(i, 0) < 0 || dup2(o, 1) < 0 || dup2(e, 2) < 0)
            _exit(126);
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        _exit(127);
    }
    uint64_t end = sk_now_ms() + 10000;
    int st = 0;
    while (pid > 0 && sk_now_ms() < end) {
        sk_loop_once(h->loop, 10);
        if (waitpid(pid, &st, WNOHANG) == pid) {
            r.code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
            break;
        }
    }
    r.out = sk_test_read(out.ptr, NULL);
    r.err = sk_test_read(err.ptr, NULL);
    sk_buf_free(&in);
    sk_buf_free(&out);
    sk_buf_free(&err);
    return r;
}

static void ran_free(ran *r)
{
    free(r->out);
    free(r->err);
}

static bool have_relay(void)
{
    const char *bin = getenv("SK_RELAY_BIN");
    if (!bin || access(bin, X_OK) != 0) {
        printf("    (skipped: SK_RELAY_BIN not set)\n");
        return false;
    }
    return true;
}

/* The settings' hook and statusline commands. */
static void commands(harness *h, sk_buf *hook, sk_buf *status)
{
    sk_arena a;
    sk_arena_init(&a, 0);
    char *text = sk_test_read(h->settings, NULL);
    const sk_json *s = text ? sk_json_parse(&a, text, strlen(text), NULL) : NULL;
    const sk_json *pre = sk_json_get(sk_json_get(s, "hooks"), "PreToolUse");
    const sk_json *entry = sk_json_at(sk_json_get(sk_json_at(pre, 0), "hooks"), 0);
    sk_str c;
    if (sk_json_as_string(sk_json_get(entry, "command"), &c))
        sk_buf_append_str(hook, c);
    if (sk_json_as_string(sk_json_dig(s, "statusLine", "command", NULL), &c))
        sk_buf_append_str(status, c);
    CHECK(hook->len && status->len);
    free(text);
    sk_arena_free(&a);
}

static bool every_channel_landed(harness *h)
{
    h->part.ops->poll(h->part.self, sk_now_ms());
    return num(h->bus, "tokens.out") > 0 && sk_bus_get(h->bus, "context.window");
}

static void test_s1_the_relays_fill_the_bus_the_transcript_follows_close_releases_all(void)
{
    if (!have_relay())
        return;
    harness h;
    harness_init(&h, NULL);
    h.ctx.now = NULL;
    harness_ready(&h);
    sk_buf hook = {0}, status = {0}, body = {0};
    commands(&h, &hook, &status);
    const char *events[] = {
        "\"hook_event_name\":\"SessionStart\",\"source\":\"startup\"",
        "\"hook_event_name\":\"PreToolUse\",\"tool_name\":\"Read\",\"tool_use_id\":\"t1\"",
    };
    for (size_t i = 0; i < SK_LEN(events); i++) {
        sk_buf_clear(&body);
        sk_buf_printf(&body, "{%s,\"session_id\":\"sess-1\",\"transcript_path\":\"%s\"}", events[i], h.file);
        ran r = run_relay(&h, hook.ptr, body.ptr);
        CHECK_INT(r.code, 0); /* a hook prints nothing and exits 0 */
        CHECK_STR(r.out, "");
        CHECK_STR(r.err, "");
        ran_free(&r);
    }
    char *fixture = sk_test_read("tests/fixtures/claude/statusline.json", NULL);
    CHECK(fixture != NULL);
    sk_buf_clear(&body);
    /* The fixture with this test's session and transcript, which a
     * repeated key's last value sets. */
    char *close_brace = fixture ? strrchr(fixture, '}') : NULL;
    if (close_brace)
        sk_buf_append(&body, fixture, (size_t)(close_brace - fixture));
    sk_buf_printf(&body, ",\"session_id\":\"sess-1\",\"transcript_path\":\"%s\"}", h.file);
    ran r = run_relay(&h, status.ptr, body.ptr);
    CHECK_INT(r.code, 0);
    CHECK_STR(r.out, ""); /* with no chain the statusline relay prints nothing */
    ran_free(&r);
    char *transcript = sk_test_read("tests/fixtures/claude/transcript-read.jsonl", NULL);
    CHECK(transcript != NULL);
    if (transcript)
        append_file(h.file, transcript);
    CHECK(until(&h, every_channel_landed));
    CHECK(num(h.bus, "event.tool") == 1);
    CHECK(num(h.bus, "tools.Read") == 1);
    CHECK(num(h.bus, "context.window") == 1000000);
    CHECK(num(h.bus, "cost.usd") == 0.3751829);

    h.part.ops->close(h.part.self);
    CHECK(access(h.target, F_OK) != 0 && errno == ENOENT); /* the link is removed */
    struct stat st;
    CHECK(lstat(h.target, &st) != 0);
    char *sock = sk_path_join(h.run, "hooks.sock");
    CHECK(refused(sock)); /* the socket refuses connections after close */
    char *before = snapshot(h.bus);
    append_file(h.file, "{\"type\":\"assistant\",\"message\":{\"id\":\"msg_after_stop\",\"model\":\"claude-late\",\"usage\":"
                        "{\"input_tokens\":1,\"output_tokens\":777,\"cache_creation_input_tokens\":0,"
                        "\"cache_read_input_tokens\":0}}}\n");
    sk_test_sleep_ms(700);
    h.part.ops->poll(h.part.self, sk_now_ms());
    char *after = snapshot(h.bus);
    CHECK_STR(after, before); /* a line appended after close writes nothing */
    free(before);
    free(after);
    free(sock);
    free(fixture);
    free(transcript);
    sk_buf_free(&hook);
    sk_buf_free(&status);
    sk_buf_free(&body);
    harness_free(&h);
}

/* A session sent to the background keeps --settings FILE and outlives
 * its window (decision 0030). */
static void test_s2_after_the_window_is_gone_a_hook_is_silent_and_the_settings_stay(void)
{
    if (!have_relay())
        return;
    harness h;
    harness_init(&h, NULL);
    harness_ready(&h);
    sk_buf hook = {0}, status = {0};
    commands(&h, &hook, &status);
    h.part.ops->close(h.part.self);
    const char *body = "{\"hook_event_name\":\"PreToolUse\",\"tool_name\":\"Read\",\"tool_use_id\":\"t1\","
                       "\"session_id\":\"bg\",\"transcript_path\":\"/nonexistent\"}";
    ran r = run_relay(&h, hook.ptr, body);
    CHECK(r.code == 0 && r.out && !*r.out && r.err && !*r.err);
    ran_free(&r);
    CHECK(access(h.settings, F_OK) == 0); /* the settings file outlives the window */
    /* A link left by a window that died: its socket is gone. */
    char *gone = sk_path_join(h.run, "gone.sock");
    CHECK(symlink(gone, h.target) == 0);
    r = run_relay(&h, hook.ptr, body);
    CHECK(r.code == 0 && r.out && !*r.out && r.err && !*r.err);
    ran_free(&r);
    free(gone);
    sk_buf_free(&hook);
    sk_buf_free(&status);
    harness_free(&h);
}

static void test_s3_the_statusline_relay_runs_the_profiles_statusline_with_no_window(void)
{
    if (!have_relay())
        return;
    harness h;
    harness_init(&h, NULL);
    sk_buf user = {0};
    sk_jw w;
    sk_jw_init(&w, &user, 0);
    sk_buf_append_c(&user, "{\"statusLine\":{\"type\":\"command\",\"command\":");
    sk_jw_string(&w, SK_STR("printf \"it's mine: %s\" \"$(head -c 1)\""));
    sk_buf_append_c(&user, "}}");
    char *settings = sk_path_join(h.cfg, "settings.json");
    write_file(settings, user.ptr, user.len);
    harness_ready(&h);
    char *text = sk_test_read(h.settings, NULL);
    CHECK(text && !strstr(text, h.run)); /* the settings name no tab directory */
    free(text);
    sk_buf hook = {0}, status = {0};
    commands(&h, &hook, &status);
    h.part.ops->close(h.part.self);
    ran r = run_relay(&h, status.ptr, "{\"session_id\":\"bg\"}");
    CHECK_INT(r.code, 0);
    CHECK_STR(r.out, "it's mine: {");
    ran_free(&r);
    free(settings);
    sk_buf_free(&user);
    sk_buf_free(&hook);
    sk_buf_free(&status);
    harness_free(&h);
}

static void test_s4_a_post_to_an_unknown_route_writes_nothing(void)
{
    harness h;
    harness_init(&h, NULL);
    harness_ready(&h);
    CHECK_INT(post(&h, h.target, "other",
                   "{\"hook_event_name\":\"PreToolUse\",\"tool_name\":\"Read\",\"tool_use_id\":\"t1\",\"session_id\":\"sess-1\"}"),
              204);
    pump(&h, 200);
    CHECK(sk_bus_get(h.bus, "event.tool") == NULL);
    CHECK(sk_bus_get(h.bus, "tools.Read") == NULL);
    /* Nor does a body that is not JSON, or one over 64 KiB. */
    CHECK_INT(post(&h, h.target, "hook", "{not json"), 204);
    sk_buf big = {0};
    sk_buf_append_c(&big, "{\"hook_event_name\":\"SessionStart\",\"session_id\":\"s\",\"pad\":\"");
    while (big.len <= SK_HTTP_MAX_BODY)
        sk_buf_append_c(&big, "xxxxxxxxxxxxxxxx");
    sk_buf_append_c(&big, "\"}");
    CHECK_INT(post(&h, h.target, "hook", big.ptr), 204);
    pump(&h, 200);
    CHECK(sk_bus_count(h.bus) == 0);
    sk_buf_free(&big);
    harness_free(&h);
}

const sk_test sk_suite_claude_adapter[] = {
    T(test_rows_1_and_2_defaults_absent_before_the_first_datum),
    T(test_rows_1_and_2_channels_waiting_then_live_with_their_target),
    T(test_row_3_the_output_rate_rises_with_a_live_sample_and_falls_after_10_s),
    T(test_row_4_a_new_session_resets_its_names_and_reseeds_in_the_same_call),
    T(test_row_5_close_twice_nothing_reaches_the_bus_after_it),
    T(test_row_6_nothing_moves_without_the_clock),
    T(test_row_8_the_report_says_replaying_while_the_history_is_read),
    T(test_watch_row_8_replaying_while_the_transcript_history_is_read),
    T(test_with_settings_given_hooks_and_statusline_are_off_and_the_notice_says_why),
    T(test_a_failed_prepare_keeps_its_notice_in_the_report),
    T(test_claude_attach_follows_the_jobs_transcript_keeping_two_fields),
    T(test_a_subcommand_gets_nothing_injected),
    T(test_a_rename_goes_into_the_transcript_and_custom_title_json),
    T(test_s1_the_relays_fill_the_bus_the_transcript_follows_close_releases_all),
    T(test_s2_after_the_window_is_gone_a_hook_is_silent_and_the_settings_stay),
    T(test_s3_the_statusline_relay_runs_the_profiles_statusline_with_no_window),
    T(test_s4_a_post_to_an_unknown_route_writes_nothing),
    {0},
};
