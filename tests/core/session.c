/* The session with a real pty, as test/session.test.js has it, case by
 * case, with an adapter of the test's own behind the seam. Every test
 * runs the loop until the end, with a timeout, then kills the child's
 * group: a child that ignores SIGHUP outlives the test otherwise. The
 * strip, view and last-screen cases (T1 to T5, S14, S15) are the UI's;
 * S4 and P2 are app/tabs.js's; S13 has no python3 to miss. */
#include "../../src/agents/agent.h"
#include "../../src/core/command.h"
#include "../../src/core/session.h"
#include "../test.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

extern char **environ;

/* The grace is timed from outside on the monotonic clock, in whole ms. */
#define SLACK_MS 5

/* What happened, in order: p prepare, a attach, s spawn, o output, c
 * close, e end. */
static char order[64];

static void mark(char c)
{
    size_t n = strlen(order);
    if (n + 1 < sizeof order) {
        order[n] = c;
        order[n + 1] = 0;
    }
}

typedef struct {
    sk_loop *loop;
    sk_envp env;
    char *file;
    sk_session *s;
    sk_buf out;
    sk_strv told, logged;
    int ends, status;
    pid_t spawned;
    bool hangup_at_spawn;
} rec;

static void on_output(void *ud, const char *bytes, size_t n)
{
    rec *r = ud;
    if (!r->out.len)
        mark('o');
    sk_buf_append(&r->out, bytes, n);
}

static void on_end(void *ud, int status)
{
    rec *r = ud;
    r->ends++;
    r->status = status;
    mark('e');
}

static void on_spawn(void *ud, pid_t pid)
{
    rec *r = ud;
    r->spawned = pid;
    mark('s');
    if (r->hangup_at_spawn)
        sk_session_hangup(r->s);
}

static void on_tell(void *ud, const char *msg)
{
    sk_strv_push(&((rec *)ud)->told, msg);
}

static void on_log(void *ud, const char *msg)
{
    sk_strv_push(&((rec *)ud)->logged, msg);
}

static void rec_init(rec *r)
{
    memset(r, 0, sizeof *r);
    r->loop = sk_loop_new();
    sk_envp_from(&r->env, environ);
    order[0] = 0;
}

static sk_session_opts opts(rec *r, char *const *argv)
{
    sk_session_opts o;
    memset(&o, 0, sizeof o);
    free(r->file);
    r->file = sk_resolve_command(argv[0], getenv("PATH"));
    o.loop = r->loop;
    o.file = r->file;
    o.argv = argv;
    o.env = &r->env;
    o.label = argv[0];
    o.on_spawn = on_spawn;
    o.on_output = on_output;
    o.on_end = on_end;
    o.log = on_log;
    o.tell = on_tell;
    o.ud = r;
    return o;
}

static sk_session *make(rec *r, const sk_session_opts *o)
{
    r->s = sk_session_new(o);
    return r->s;
}

/* Rounds of the loop until the end, or false after ms. */
static bool wait_end(rec *r, int ms)
{
    uint64_t until = sk_now_ms() + (uint64_t)ms;
    while (!r->ends && sk_now_ms() < until)
        sk_loop_once(r->loop, 50);
    return r->ends > 0;
}

static bool wait_output(rec *r, const char *text, int ms)
{
    uint64_t until = sk_now_ms() + (uint64_t)ms;
    while (!strstr(sk_buf_cstr(&r->out), text) && sk_now_ms() < until)
        sk_loop_once(r->loop, 50);
    return strstr(sk_buf_cstr(&r->out), text) != NULL;
}

static void run_for(rec *r, int ms)
{
    uint64_t until = sk_now_ms() + (uint64_t)ms;
    for (uint64_t now; (now = sk_now_ms()) < until;)
        sk_loop_once(r->loop, (int)(until - now));
}

/* No process is left in pid's group. */
static bool gone(pid_t pid)
{
    return kill(-pid, 0) != 0 && errno == ESRCH;
}

static bool wait_gone(pid_t pid, int ms)
{
    uint64_t until = sk_now_ms() + (uint64_t)ms;
    while (!gone(pid) && sk_now_ms() < until)
        sk_test_sleep_ms(20);
    return gone(pid);
}

static void rec_free(rec *r)
{
    if (r->s) {
        pid_t pid = sk_session_pid(r->s);
        if (pid > 0)
            kill(-pid, SIGKILL);
        sk_session_free(r->s);
    }
    sk_loop_free(r->loop);
    sk_envp_free(&r->env);
    free(r->file);
    sk_buf_free(&r->out);
    sk_strv_free(&r->told);
    sk_strv_free(&r->logged);
}

static bool has_line(const rec *r, const char *line)
{
    size_t n = strlen(line);
    for (const char *p = sk_buf_cstr(&r->out); (p = strstr(p, line)); p += n)
        if ((p == r->out.ptr || p[-1] == '\n') && (p[n] == '\r' || p[n] == '\n'))
            return true;
    return false;
}

static void escape(sk_buf *b, const char *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)p[i];
        if (c >= 0x20 && c < 0x7f && c != '\\')
            sk_buf_putc(b, (char)c);
        else
            sk_buf_printf(b, "\\x%02x", c);
    }
}

/* The output begins with prefix; a miss shows the output. */
static void starts_with(const rec *r, const char *prefix, int line)
{
    if (!strncmp(sk_buf_cstr(&r->out), prefix, strlen(prefix)))
        return;
    sk_buf want = {0}, got = {0};
    escape(&want, prefix, strlen(prefix));
    escape(&got, sk_buf_cstr(&r->out), r->out.len);
    sk_test_fail(__FILE__, line, "output begins \"%s\": \"%s\"", sk_buf_cstr(&want), sk_buf_cstr(&got));
    sk_buf_free(&want);
    sk_buf_free(&got);
}

#define CHECK_STARTS(r, prefix) starts_with(r, prefix, __LINE__)

static bool any_line_starts(const rec *r, const char *prefix)
{
    size_t n = strlen(prefix);
    for (const char *p = sk_buf_cstr(&r->out); (p = strstr(p, prefix)); p += n)
        if (p == r->out.ptr || p[-1] == '\n')
            return true;
    return false;
}

/* An adapter that records the calls made to it. */
typedef struct {
    const char *args[3];
    const char *notice;
    bool fail, hangup;
    int closed;
    pid_t attached;
} fake;

static fake the_fake;
static sk_session *preparing; /* hung up by a prepare that asks for it */

static int f_prepare(void *self, sk_strv *args, char **notice)
{
    fake *f = self;
    mark('p');
    if (f->hangup)
        sk_session_hangup(preparing);
    if (f->fail)
        return -1;
    for (size_t i = 0; i < SK_LEN(f->args) && f->args[i]; i++)
        sk_strv_push(args, f->args[i]);
    if (f->notice)
        *notice = sk_xstrdup(f->notice);
    return 0;
}
static void f_attach(void *self, pid_t pid)
{
    ((fake *)self)->attached = pid;
    mark('a');
}
static void f_poll(void *self, uint64_t now)
{
    (void)self, (void)now;
}
static void f_report(void *self, sk_report *out)
{
    (void)self;
    memset(out, 0, sizeof *out);
}
static void f_close(void *self)
{
    ((fake *)self)->closed++;
    mark('c');
}
static void f_free(void *self)
{
    (void)self;
}
static const sk_part_ops FAKE_OPS = {f_prepare, f_attach, f_poll, f_report, NULL, f_close, f_free, NULL};
static bool f_matches(const char *base)
{
    (void)base;
    return true;
}
static sk_part f_create(const sk_agent_ctx *ctx)
{
    (void)ctx;
    return (sk_part){&FAKE_OPS, &the_fake};
}
static void f_carry(char *const *argv, sk_strv *args, sk_strv *dropped)
{
    (void)argv, (void)args, (void)dropped;
}
static const sk_provider FAKE = {"fake", f_matches, f_create, f_carry};

static uint64_t clock_zero(void)
{
    return 0;
}

/* The fake behind the seam, beside Generic, on a clock stopped at 0. */
static sk_agent *fake_agent(sk_bus *bus, char *const *cmd, rec *r)
{
    memset(&the_fake, 0, sizeof the_fake);
    sk_agent_ctx c;
    memset(&c, 0, sizeof c);
    c.cmd = cmd;
    c.bus = bus;
    c.now = clock_zero;
    c.log = on_log;
    c.log_ud = r;
    return sk_agent_open_with(&c, &FAKE);
}

/* S1 */
static void test_exit_7_ends_7(void)
{
    rec r;
    rec_init(&r);
    char *argv[] = {"sh", "-c", "exit 7", NULL};
    sk_session_opts o = opts(&r, argv);
    sk_session *s = make(&r, &o);
    CHECK_INT(sk_session_status(s), -1);
    sk_session_start(s, 80, 24);
    CHECK_INT(sk_session_get_state(s), SK_SESSION_RUNNING);
    CHECK(wait_end(&r, 5000));
    CHECK_INT(r.status, 7);
    CHECK_INT(r.ends, 1);
    CHECK_INT(sk_session_status(s), 7);
    CHECK_INT(sk_session_get_state(s), SK_SESSION_ENDED);
    CHECK_INT(r.told.len, 0);
    CHECK(sk_session_failure(s) == NULL);
    rec_free(&r);
}

/* S2 */
static void test_death_by_signal_n_gives_128_plus_n(void)
{
    const struct {
        const char *script;
        int want;
    } cases[] = {{"kill -TERM $$", 143}, {"kill -KILL $$", 137}, {"kill -HUP $$", 129}};
    for (size_t i = 0; i < SK_LEN(cases); i++) {
        rec r;
        rec_init(&r);
        char *argv[] = {"sh", "-c", (char *)cases[i].script, NULL};
        sk_session_opts o = opts(&r, argv);
        sk_session_start(make(&r, &o), 80, 24);
        CHECK(wait_end(&r, 5000));
        CHECK_INT(r.status, cases[i].want);
        rec_free(&r);
    }
}

/* S3 */
static void test_hup_ignoring_child_is_killed_after_the_grace(void)
{
    rec r;
    rec_init(&r);
    char *argv[] = {"sh", "-c", "trap \"\" HUP; echo ready; sleep 20", NULL};
    sk_session_opts o = opts(&r, argv);
    sk_session *s = make(&r, &o);
    sk_session_start(s, 80, 24);
    CHECK(wait_output(&r, "ready", 5000));
    uint64_t t0 = sk_now_ms();
    sk_session_hangup(s);
    CHECK_INT(sk_session_get_state(s), SK_SESSION_ENDING);
    CHECK(wait_end(&r, 5000));
    CHECK_INT(r.status, 137);
    uint64_t took = sk_now_ms() - t0;
    CHECK(took >= 2000 - SLACK_MS);
    /* A killed grandchild lingers as a zombie until its new parent
     * reaps it. */
    CHECK(wait_gone(sk_session_pid(s), 1000));
    rec_free(&r);
}

/* S5 */
static void test_default_disposition_child_ends_129_inside_the_grace(void)
{
    rec r;
    rec_init(&r);
    char *argv[] = {"sleep", "30", NULL};
    sk_session_opts o = opts(&r, argv);
    sk_session *s = make(&r, &o);
    sk_session_start(s, 80, 24);
    CHECK(r.spawned > 0);
    uint64_t t0 = sk_now_ms();
    sk_session_hangup(s);
    CHECK(wait_end(&r, 5000));
    CHECK_INT(r.status, 129);
    CHECK(sk_now_ms() - t0 < 1500);
    rec_free(&r);
}

/* S6 */
static void test_second_hangup_does_not_restart_the_grace(void)
{
    rec r;
    rec_init(&r);
    char *argv[] = {"sh", "-c", "trap \"\" HUP; echo ready; sleep 20", NULL};
    sk_session_opts o = opts(&r, argv);
    sk_session *s = make(&r, &o);
    sk_session_start(s, 80, 24);
    CHECK(wait_output(&r, "ready", 5000));
    uint64_t t0 = sk_now_ms();
    sk_session_hangup(s);
    run_for(&r, 1000);
    sk_session_hangup(s);
    CHECK(wait_end(&r, 5000));
    CHECK_INT(r.status, 137);
    uint64_t took = sk_now_ms() - t0;
    CHECK(took >= 2000 - SLACK_MS && took < 2800);
    rec_free(&r);
}

static double bus_num(const sk_bus *b, const char *name)
{
    const sk_val *v = sk_bus_get(b, name);
    return v && v->type == SK_VAL_NUM ? v->num : -1;
}

/* S7 */
static void test_pty_bytes_arrive_unchanged(void)
{
    rec r;
    rec_init(&r);
    /* a, one CJK character as its three UTF-8 bytes, b */
    char *argv[] = {"printf", "a\\344\\270\\255b", NULL};
    sk_bus *bus = sk_bus_new(NULL, NULL);
    sk_agent *a = fake_agent(bus, argv, &r);
    sk_session_opts o = opts(&r, argv);
    o.agent = a;
    o.now = clock_zero;
    sk_session_start(make(&r, &o), 80, 24);
    CHECK(wait_end(&r, 5000));
    CHECK_INT(r.status, 0);
    CHECK_MEM(sk_buf_str(&r.out), "a\xe4\xb8\xad" "b");
    /* The output reached Generic: one burst on a stopped clock. */
    CHECK(bus_num(bus, "event.output") == 1);
    rec_free(&r);
    sk_agent_free(a);
    sk_bus_free(bus);
}

/* S8 */
static void test_input_reaches_the_child(void)
{
    rec r;
    rec_init(&r);
    char *argv[] = {"sh", "-c", "read x && echo got:$x", NULL};
    sk_session_opts o = opts(&r, argv);
    sk_session *s = make(&r, &o);
    sk_session_start(s, 80, 24);
    sk_session_input(s, "hi\r", 3);
    CHECK(wait_end(&r, 5000));
    CHECK_INT(r.status, 0);
    CHECK(strstr(sk_buf_cstr(&r.out), "got:hi") != NULL);
    rec_free(&r);
}

/* S9 */
static void test_child_environment(void)
{
    rec r;
    rec_init(&r);
    /* Every SKINNED_ variable: SKINNED_CONTROL names the control socket. */
    const char *dropped[] = {"SKINNED_SKIN",    "SKINNED_RUNTIME_DIR", "CHROME_DESKTOP", "FC_FONTATIONS",
                             "SKINNED_MUTE",    "SKINNED_CONTROL",     "SKINNED_HIDDEN", "SKINNED_SHOT",
                             "SKINNED_ANYTHING"};
    for (size_t i = 0; i < SK_LEN(dropped); i++)
        sk_envp_set(&r.env, dropped[i], "x");
    sk_envp_set(&r.env, "TERM", "dumb");
    sk_envp_set(&r.env, "SK_SESSION_KEPT", "yes");
    char *argv[] = {"env", NULL};
    sk_session_opts o = opts(&r, argv);
    sk_session_start(make(&r, &o), 200, 24);
    CHECK(wait_end(&r, 5000));
    CHECK_INT(r.status, 0);
    CHECK(has_line(&r, "TERM=xterm-256color"));
    CHECK(has_line(&r, "COLORTERM=truecolor"));
    CHECK(has_line(&r, "SK_SESSION_KEPT=yes"));
    for (size_t i = 0; i < SK_LEN(dropped); i++) {
        char prefix[64];
        snprintf(prefix, sizeof prefix, "%s=", dropped[i]);
        CHECK(!any_line_starts(&r, prefix));
    }
    /* The function itself, and the tab's environment left as it was. */
    sk_envp child;
    sk_session_child_env(&r.env, &child);
    CHECK_STR(sk_envp_get(&child, "TERM"), "xterm-256color");
    CHECK(sk_envp_get(&child, "SKINNED_SKIN") == NULL);
    CHECK_STR(sk_envp_get(&r.env, "TERM"), "dumb");
    sk_envp_free(&child);
    /* A name twice in the environment: the app's value alone, once. */
    sk_envp twice;
    sk_envp_from(&twice, NULL);
    sk_strv_push(&twice.vars, "TERM=dumb");
    sk_strv_push(&twice.vars, "TERM=vt100");
    sk_strv_push(&twice.vars, "SKINNED_CONTROL=/x");
    sk_strv_push(&twice.vars, "SKINNED_CONTROL=/y");
    sk_session_child_env(&twice, &child);
    size_t terms = 0;
    for (size_t i = 0; i < child.vars.len; i++) {
        terms += !strncmp(child.vars.ptr[i], "TERM=", 5);
        CHECK(strncmp(child.vars.ptr[i], "SKINNED_", 8) != 0);
    }
    CHECK_INT(terms, 1);
    CHECK_STR(sk_envp_get(&child, "TERM"), "xterm-256color");
    sk_envp_free(&child);
    sk_envp_free(&twice);
    sk_session_child_env(NULL, &child);
    CHECK_INT(child.vars.len, 2);
    sk_envp_free(&child);
    rec_free(&r);
}

/* P1, in the shape a synchronous prepare has: the hang-up comes from
 * inside it, or before the start. */
static void test_close_during_prepare_spawns_nothing_and_ends_129(void)
{
    rec r;
    rec_init(&r);
    char mark_path[600], script[700];
    snprintf(mark_path, sizeof mark_path, "%s/MARK", sk_test_tmpdir());
    snprintf(script, sizeof script, "echo x > %s", mark_path);
    char *argv[] = {"sh", "-c", script, NULL};
    sk_bus *bus = sk_bus_new(NULL, NULL);
    sk_agent *a = fake_agent(bus, argv, &r);
    the_fake.hangup = true;
    sk_session_opts o = opts(&r, argv);
    o.agent = a;
    sk_session *s = make(&r, &o);
    preparing = s;
    sk_session_start(s, 80, 24);
    CHECK_INT(sk_session_get_state(s), SK_SESSION_ENDED);
    CHECK_INT(sk_session_status(s), 129);
    CHECK(wait_end(&r, 5000));
    CHECK_INT(r.status, 129);
    run_for(&r, 500);
    CHECK_INT(r.ends, 1);
    CHECK_INT(sk_session_pid(s), 0);
    CHECK_INT(the_fake.closed, 1);
    struct stat st;
    CHECK(stat(mark_path, &st) != 0); /* the child never ran */
    rec_free(&r);
    sk_agent_free(a);

    rec_init(&r);
    a = fake_agent(bus, argv, &r);
    o = opts(&r, argv);
    o.agent = a;
    s = make(&r, &o);
    sk_session_hangup(s);
    sk_session_start(s, 80, 24);
    CHECK(wait_end(&r, 5000));
    CHECK_INT(r.status, 129);
    CHECK_INT(sk_session_pid(s), 0);
    CHECK_INT(the_fake.closed, 1);
    CHECK_STR(order, "ce"); /* no prepare */
    CHECK(stat(mark_path, &st) != 0);
    rec_free(&r);
    sk_agent_free(a);
    sk_bus_free(bus);
    preparing = NULL;
}

/* P3 */
static void test_failed_prepare_spawns_without_its_arguments_and_closes(void)
{
    rec r;
    rec_init(&r);
    char *argv[] = {"echo", "u1", NULL};
    sk_bus *bus = sk_bus_new(NULL, NULL);
    sk_agent *a = fake_agent(bus, argv, &r);
    the_fake.fail = true;
    sk_session_opts o = opts(&r, argv);
    o.agent = a;
    sk_session *s = make(&r, &o);
    sk_session_start(s, 80, 24);
    CHECK(wait_end(&r, 5000));
    CHECK_INT(r.status, 0);
    CHECK_STARTS(&r, "u1\r\n");
    CHECK_STR(sk_session_notice(s), SK_AGENT_FAILED);
    CHECK(the_fake.closed >= 1);
    bool logged = false;
    for (size_t i = 0; i < r.logged.len; i++)
        logged |= strstr(r.logged.ptr[i], "adapter failed") != NULL;
    CHECK(logged);
    rec_free(&r);
    sk_agent_free(a);
    sk_bus_free(bus);
}

/* P4: node-pty refused a cols of '80'; here a size out of range. */
static void test_spawn_failure_ends_125(void)
{
    rec r;
    rec_init(&r);
    char *argv[] = {"sh", "-c", "exit 0", NULL};
    sk_bus *bus = sk_bus_new(NULL, NULL);
    sk_agent *a = fake_agent(bus, argv, &r);
    sk_session_opts o = opts(&r, argv);
    o.agent = a;
    sk_session *s = make(&r, &o);
    sk_session_start(s, 0, 24);
    CHECK(wait_end(&r, 5000));
    CHECK_INT(r.status, 125);
    CHECK_INT(sk_session_pid(s), 0);
    CHECK_INT(r.told.len, 1);
    if (r.told.len) {
        CHECK(!strncmp(r.told.ptr[0], "cannot start sh: ", 17));
        CHECK(strstr(r.told.ptr[0], "cols") != NULL);
    }
    CHECK(sk_session_failure(s) != NULL);
    CHECK_INT(the_fake.closed, 1);
    rec_free(&r);
    sk_agent_free(a);
    sk_bus_free(bus);
}

/* P5 */
static void test_prepare_spawn_attach_output_close_end(void)
{
    rec r;
    rec_init(&r);
    char *argv[] = {"echo", "hi", NULL};
    sk_bus *bus = sk_bus_new(NULL, NULL);
    sk_agent *a = fake_agent(bus, argv, &r);
    sk_session_opts o = opts(&r, argv);
    o.agent = a;
    sk_session *s = make(&r, &o);
    sk_session_start(s, 80, 24);
    CHECK(wait_end(&r, 5000));
    CHECK_STR(order, "pasoce");
    CHECK(r.spawned > 0);
    CHECK_INT(the_fake.attached, r.spawned);
    CHECK_INT(sk_session_pid(s), r.spawned);
    rec_free(&r);
    sk_agent_free(a);
    sk_bus_free(bus);
}

/* P6, with what T1 asks of the session: the notice and the argv. */
static void test_adapter_arguments_go_after_argv0(void)
{
    rec r;
    rec_init(&r);
    char *argv[] = {"echo", "u1", NULL};
    sk_bus *bus = sk_bus_new(NULL, NULL);
    sk_agent *a = fake_agent(bus, argv, &r);
    the_fake.args[0] = "x1";
    the_fake.args[1] = "x2";
    the_fake.notice = "meters limited: test";
    sk_session_opts o = opts(&r, argv);
    o.agent = a;
    o.label = "echo [x]";
    sk_session *s = make(&r, &o);
    CHECK(sk_session_argv(s) == NULL);
    sk_session_start(s, 80, 24);
    CHECK(wait_end(&r, 5000));
    CHECK_INT(r.status, 0);
    CHECK_STARTS(&r, "x1 x2 u1\r\n");
    char *const *exec = sk_session_argv(s);
    CHECK(exec != NULL);
    if (exec) {
        CHECK_STR(exec[0], "echo");
        CHECK_STR(exec[1], "x1");
        CHECK_STR(exec[2], "x2");
        CHECK_STR(exec[3], "u1");
        CHECK(exec[4] == NULL);
    }
    CHECK_STR(sk_session_notice(s), "meters limited: test");
    CHECK_STR(sk_session_label(s), "echo [x]");
    rec_free(&r);
    sk_agent_free(a);
    sk_bus_free(bus);
}

/* S10 */
static void test_hangup_at_the_instant_of_spawn_reaches_the_child(void)
{
    for (int i = 0; i < 20; i++) {
        rec r;
        rec_init(&r);
        r.hangup_at_spawn = true;
        char *argv[] = {"sleep", "30", NULL};
        sk_session_opts o = opts(&r, argv);
        sk_session_start(make(&r, &o), 80, 24);
        CHECK(wait_end(&r, 5000));
        if (r.status != 129)
            sk_test_fail(__FILE__, __LINE__, "run %d: status %d", i, r.status);
        rec_free(&r);
    }
}

/* An executable file whose exec fails after it resolved. */
static char *failing(const char *name, const char *bytes, size_t n)
{
    sk_buf p = {0};
    sk_buf_printf(&p, "%s/%s", sk_test_tmpdir(), name);
    FILE *f = fopen(p.ptr, "wb");
    CHECK(f != NULL);
    if (f) {
        fwrite(bytes, 1, n, f);
        fclose(f);
    }
    chmod(p.ptr, 0755);
    return p.ptr;
}

/* S11 */
static void test_missing_interpreter_ends_127_with_the_reason(void)
{
    rec r;
    rec_init(&r);
    const char *script = "#!/nonexistent/interp\necho hi\n";
    char *file = failing("nointerp", script, strlen(script));
    char *argv[] = {"nointerp", NULL};
    sk_session_opts o = opts(&r, argv);
    o.file = file;
    sk_session *s = make(&r, &o);
    sk_session_start(s, 80, 24);
    CHECK(wait_end(&r, 5000));
    CHECK_INT(r.status, 127);
    CHECK_INT(r.told.len, 1);
    if (r.told.len)
        CHECK_STR(r.told.ptr[0], "cannot start nointerp: required file not found");
    CHECK_STR(sk_session_failure(s), "required file not found");
    /* exec-clean.py's line on the child's stderr */
    CHECK(has_line(&r, "nointerp: required file not found"));
    rec_free(&r);
    free(file);
}

/* S12 */
static void test_no_executable_format_ends_126_with_the_reason(void)
{
    rec r;
    rec_init(&r);
    char *file = failing("badelf", "\x7f" "ELFgarbage", 11);
    char *argv[] = {"badelf", NULL};
    sk_session_opts o = opts(&r, argv);
    o.file = file;
    sk_session_start(make(&r, &o), 80, 24);
    CHECK(wait_end(&r, 5000));
    CHECK_INT(r.status, 126);
    CHECK_INT(r.told.len, 1);
    if (r.told.len)
        CHECK_STR(r.told.ptr[0], "cannot start badelf: Exec format error");
    rec_free(&r);
    free(file);
}

/* The working directory, and one that cannot be entered. */
static void test_working_directory(void)
{
    rec r;
    rec_init(&r);
    char real[PATH_MAX]; /* glibc's fortified realpath wants no less */
    CHECK(realpath(sk_test_tmpdir(), real) != NULL);
    char *argv[] = {"pwd", "-P", NULL};
    sk_session_opts o = opts(&r, argv);
    o.cwd = sk_test_tmpdir();
    sk_session_start(make(&r, &o), 80, 24);
    CHECK(wait_end(&r, 5000));
    CHECK_INT(r.status, 0);
    CHECK(has_line(&r, real));
    rec_free(&r);

    rec_init(&r);
    o = opts(&r, argv);
    o.cwd = "/nonexistent/dir";
    sk_session *s = make(&r, &o);
    sk_session_start(s, 80, 24);
    CHECK(wait_end(&r, 5000));
    CHECK_INT(r.status, 125);
    CHECK_STR(sk_session_failure(s), "cannot change to /nonexistent/dir: No such file or directory");
    CHECK_INT(r.told.len, 1);
    rec_free(&r);
}

/* exec-clean.py's closerange: the CLI holds fds 0 to 2, all three the
 * terminal. */
static void test_child_holds_only_the_terminal(void)
{
    rec r;
    rec_init(&r);
    int p[2];
    CHECK(pipe(p) == 0);
    int high = fcntl(p[1], F_DUPFD, 50); /* inheritable */
    CHECK(high >= 50);
    char script[300];
    snprintf(script, sizeof script,
             "for fd in 0 1 2; do [ -t $fd ] || echo notty$fd; done; "
             "[ -e /dev/fd/2 ] && echo has2; [ -e /dev/fd/%d ] && echo leaked; echo done",
             high);
    char *argv[] = {"sh", "-c", script, NULL};
    sk_session_opts o = opts(&r, argv);
    sk_session_start(make(&r, &o), 80, 24);
    CHECK(wait_end(&r, 5000));
    CHECK_INT(r.status, 0);
    CHECK(has_line(&r, "has2"));
    CHECK(has_line(&r, "done"));
    CHECK(strstr(sk_buf_cstr(&r.out), "notty") == NULL);
    CHECK(strstr(sk_buf_cstr(&r.out), "leaked") == NULL);
    close(high);
    close(p[0]);
    close(p[1]);
    rec_free(&r);
}

/* The pty is the child's controlling terminal, its group in front: ^C
 * interrupts it, as in a terminal. */
static void test_control_c_interrupts_the_child(void)
{
    rec r;
    rec_init(&r);
    /* stty's report goes first, so a failure shows the terminal's modes. */
    /* exec: dash (Linux's sh) starts a command by vfork, and a SIGINT in
     * that window is lost to the child and held by the shell until the
     * child ends. */
    char *argv[] = {"sh", "-c", "stty -a; echo ready; exec sleep 20", NULL};
    sk_session_opts o = opts(&r, argv);
    sk_session *s = make(&r, &o);
    sk_session_start(s, 80, 24);
    CHECK(wait_output(&r, "ready", 5000));
    sk_session_input(s, "\x03", 1);
    bool ended = wait_end(&r, 5000);
    if (!ended) {
        /* The session's processes, which group has the terminal, and
         * their signals, where /proc tells them. */
        sk_buf ps = {0};
        sk_buf_printf(&ps,
                      "ps -A -o pid=,ppid=,pgid=,tpgid=,sess=,stat=,comm= | awk '$5 == %ld'; "
                      "for p in $(ps -A -o pid=,sess= | awk '$2 == %ld {print $1}'); do "
                      "echo \"$p $(grep -E '^Sig(Blk|Ign|Cgt)' /proc/$p/status 2>/dev/null | tr '\\n' ' ')\"; "
                      "done",
                      (long)r.spawned, (long)r.spawned);
        fflush(stdout);
        int rc = system(ps.ptr);
        (void)rc;
        sk_buf_free(&ps);
    }
    CHECK(ended);
    if (r.status != 128 + SIGINT)
        sk_test_fail(__FILE__, __LINE__, "status %d, output: %.600s", r.status, sk_buf_cstr(&r.out));
    rec_free(&r);
}

/* Signals the app ignores or blocks are the CLI's to have. */
static void test_child_signals_start_at_their_defaults(void)
{
    sigset_t usr1, old;
    sigemptyset(&usr1);
    sigaddset(&usr1, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &usr1, &old);
    void (*was)(int) = signal(SIGPIPE, SIG_IGN);
    const struct {
        const char *script;
        int want;
    } cases[] = {{"kill -PIPE $$", 128 + SIGPIPE}, {"kill -USR1 $$", 128 + SIGUSR1}};
    for (size_t i = 0; i < SK_LEN(cases); i++) {
        rec r;
        rec_init(&r);
        char *argv[] = {"sh", "-c", (char *)cases[i].script, NULL};
        sk_session_opts o = opts(&r, argv);
        sk_session_start(make(&r, &o), 80, 24);
        CHECK(wait_end(&r, 5000));
        CHECK_INT(r.status, cases[i].want);
        rec_free(&r);
    }
    signal(SIGPIPE, was);
    pthread_sigmask(SIG_SETMASK, &old, NULL);
}

static void test_resize(void)
{
    rec r;
    rec_init(&r);
    char *argv[] = {"sh", "-c", "stty size; read x; stty size", NULL};
    sk_session_opts o = opts(&r, argv);
    sk_session *s = make(&r, &o);
    CHECK_INT(sk_session_resize(s, 120, 40), -1); /* no child yet */
    sk_session_start(s, 100, 30);
    CHECK(wait_output(&r, "30 100", 5000));
    CHECK_INT(sk_session_resize(s, 0, 40), -1);
    CHECK_INT(sk_session_resize(s, 120, 40), 0);
    sk_session_input(s, "\r", 1);
    CHECK(wait_end(&r, 5000));
    CHECK_INT(r.status, 0);
    CHECK(has_line(&r, "40 120"));
    CHECK_INT(sk_session_resize(s, 120, 40), -1);
    rec_free(&r);
}

/* More input than the pty holds is queued: the call returns at once
 * and the rest follows as the child reads. */
static void test_input_is_queued_without_blocking(void)
{
    rec r;
    rec_init(&r);
    char *argv[] = {"sh", "-c", "stty raw -echo; echo ready; head -c 200000 >/dev/null; echo done", NULL};
    sk_session_opts o = opts(&r, argv);
    sk_session *s = make(&r, &o);
    sk_session_start(s, 80, 24);
    CHECK(wait_output(&r, "ready", 5000));
    char *big = sk_xmalloc(200000);
    memset(big, 'x', 200000);
    uint64_t t0 = sk_now_ms();
    sk_session_input(s, big, 200000);
    CHECK(sk_now_ms() - t0 < 500);
    free(big);
    CHECK(wait_end(&r, 10000));
    CHECK_INT(r.status, 0);
    CHECK(strstr(sk_buf_cstr(&r.out), "done") != NULL);
    rec_free(&r);
}

/* One SIGCHLD path reaps the children of every session. */
static void test_sessions_side_by_side(void)
{
    rec a, b;
    rec_init(&a);
    rec_init(&b);
    sk_loop_free(b.loop);
    b.loop = a.loop;
    char *argv_a[] = {"sh", "-c", "sleep 0.2; exit 3", NULL};
    char *argv_b[] = {"sh", "-c", "exit 4", NULL};
    sk_session_opts oa = opts(&a, argv_a), ob = opts(&b, argv_b);
    sk_session_start(make(&a, &oa), 80, 24);
    sk_session_start(make(&b, &ob), 80, 24);
    CHECK(wait_end(&b, 5000));
    CHECK(wait_end(&a, 5000));
    CHECK_INT(a.status, 3);
    CHECK_INT(b.status, 4);
    b.loop = sk_loop_new();
    rec_free(&b);
    rec_free(&a);
}

/* A grandchild that keeps the pty open does not hold the end: it comes
 * with the child's exit, its output read first. */
static void test_end_follows_the_child_not_the_pty(void)
{
    rec r;
    rec_init(&r);
    char *argv[] = {"sh", "-c", "sleep 5 & echo bye; exit 3", NULL};
    sk_session_opts o = opts(&r, argv);
    sk_session_start(make(&r, &o), 80, 24);
    uint64_t t0 = sk_now_ms();
    CHECK(wait_end(&r, 5000));
    CHECK(sk_now_ms() - t0 < 1500);
    CHECK_INT(r.status, 3);
    CHECK(has_line(&r, "bye"));
    rec_free(&r);
}

/* The app leaves no child behind: a session freed while its child runs
 * kills and reaps it. */
static void test_free_kills_a_running_child(void)
{
    rec r;
    rec_init(&r);
    char *argv[] = {"sleep", "30", NULL};
    sk_session_opts o = opts(&r, argv);
    sk_session *s = make(&r, &o);
    sk_session_start(s, 80, 24);
    pid_t pid = sk_session_pid(s);
    CHECK(pid > 0);
    sk_session_free(s);
    r.s = NULL;
    CHECK(kill(pid, 0) != 0 && errno == ESRCH);
    CHECK_INT(r.ends, 0);
    rec_free(&r);
}

const sk_test sk_suite_session[] = {
    T(test_exit_7_ends_7),
    T(test_death_by_signal_n_gives_128_plus_n),
    T(test_hup_ignoring_child_is_killed_after_the_grace),
    T(test_default_disposition_child_ends_129_inside_the_grace),
    T(test_second_hangup_does_not_restart_the_grace),
    T(test_pty_bytes_arrive_unchanged),
    T(test_input_reaches_the_child),
    T(test_child_environment),
    T(test_close_during_prepare_spawns_nothing_and_ends_129),
    T(test_failed_prepare_spawns_without_its_arguments_and_closes),
    T(test_spawn_failure_ends_125),
    T(test_prepare_spawn_attach_output_close_end),
    T(test_adapter_arguments_go_after_argv0),
    T(test_hangup_at_the_instant_of_spawn_reaches_the_child),
    T(test_missing_interpreter_ends_127_with_the_reason),
    T(test_no_executable_format_ends_126_with_the_reason),
    T(test_working_directory),
    T(test_child_holds_only_the_terminal),
    T(test_control_c_interrupts_the_child),
    T(test_child_signals_start_at_their_defaults),
    T(test_resize),
    T(test_input_is_queued_without_blocking),
    T(test_sessions_side_by_side),
    T(test_end_follows_the_child_not_the_pty),
    T(test_free_kills_a_running_child),
    {0},
};
