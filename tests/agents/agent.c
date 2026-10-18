/* The adapter seam (decision 0021), as test/agent.test.js has it, with a
 * fake provider, and Generic on a fake clock, as test/writer.test.js
 * tests it. */
#include "../../src/agents/agent.h"
#include "../../src/agents/generic.h"
#include "../test.h"

#include <math.h>
#include <unistd.h>

static uint64_t fake_now;
static uint64_t clock_now(void)
{
    return fake_now;
}

typedef struct {
    int prepared, attached, polled, closed, freed;
    bool fail;
    pid_t pid;
    char order[32];
} fake;

static fake the_fake;

static int f_prepare(void *self, sk_strv *args, char **notice)
{
    fake *f = self;
    f->prepared++;
    strcat(f->order, "p");
    if (f->fail) {
        sk_strv_push(args, "--half-done"); /* must not survive the failure */
        return -1;
    }
    sk_strv_push(args, "--settings");
    sk_strv_push(args, "/x.json");
    *notice = sk_xstrdup("meters limited: test");
    return 0;
}
static void f_attach(void *self, pid_t pid)
{
    fake *f = self;
    f->attached++;
    f->pid = pid;
    strcat(f->order, "a");
}
static void f_poll(void *self, uint64_t now)
{
    (void)now;
    ((fake *)self)->polled++;
}
static void f_report(void *self, sk_report *out)
{
    (void)self;
    memset(out, 0, sizeof *out);
    out->session = "s1";
    out->notice = "meters limited: test";
    out->channels[0] = (sk_channel){"hooks", SK_CH_WAITING, NULL, NULL};
    out->n_channels = 1;
}
static bool f_rename(void *self, const char *name)
{
    (void)self;
    return !strcmp(name, "ok");
}
static void f_close(void *self)
{
    fake *f = self;
    f->closed++;
    strcat(f->order, "c");
}
static void f_free(void *self)
{
    ((fake *)self)->freed++;
}

static const sk_part_ops FAKE_OPS = {f_prepare, f_attach, f_poll, f_report, f_rename, f_close, f_free, NULL};

static bool f_matches(const char *base)
{
    return !strcmp(base, "fake");
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

static sk_agent_ctx ctx_for(sk_bus *bus, char *const *cmd)
{
    sk_agent_ctx c;
    memset(&c, 0, sizeof c);
    c.cmd = cmd;
    c.bus = bus;
    c.now = clock_now;
    return c;
}

static double num(const sk_bus *b, const char *name)
{
    const sk_val *v = sk_bus_get(b, name);
    return v && v->type == SK_VAL_NUM ? v->num : NAN;
}

static void test_dispatch_by_basename(void)
{
    char *claude[] = {"/usr/local/bin/claude", NULL}, *claude_work[] = {"claude_work", NULL};
    char *codex[] = {"codex", NULL}, *bash[] = {"bash", NULL}, *claudette[] = {"claudette", NULL};
    char *trailing[] = {"claude_", NULL}, *none[] = {NULL};
    CHECK_STR(sk_agent_provider(claude)->kind, "claude");
    CHECK_STR(sk_agent_provider(claude_work)->kind, "claude");
    CHECK_STR(sk_agent_provider(codex)->kind, "codex");
    CHECK_STR(sk_agent_provider(bash)->kind, "plain");
    CHECK_STR(sk_agent_provider(claudette)->kind, "plain");
    CHECK_STR(sk_agent_provider(trailing)->kind, "plain");
    CHECK_STR(sk_agent_provider(none)->kind, "plain");
}

static void test_order_of_operations(void)
{
    memset(&the_fake, 0, sizeof the_fake);
    sk_bus *bus = sk_bus_new(NULL, NULL);
    char *cmd[] = {"fake", NULL};
    sk_agent_ctx c = ctx_for(bus, cmd);
    sk_agent *a = sk_agent_open_with(&c, &FAKE);
    CHECK_STR(sk_agent_kind(a), "fake");
    CHECK_INT(sk_bus_count(bus), 0); /* nothing written before prepare */
    sk_strv args = {0};
    char *notice = NULL;
    sk_agent_prepare(a, &args, &notice);
    CHECK_INT(args.len, 2);
    CHECK_STR(notice, "meters limited: test");
    sk_agent_attach(a, 4242);
    CHECK_INT(the_fake.pid, 4242);
    sk_agent_tick(a, 1000);
    CHECK_INT(the_fake.polled, 1);
    sk_report r;
    sk_agent_report(a, &r);
    CHECK_STR(r.session, "s1");
    CHECK_INT(r.n_channels, 3); /* the adapter's, then Generic's pty and proc */
    CHECK_STR(r.channels[0].name, "hooks");
    CHECK_STR(r.channels[1].name, "pty");
    CHECK_STR(r.channels[2].name, "proc");
    CHECK(sk_agent_rename(a, "ok"));
    CHECK(!sk_agent_rename(a, "no"));
    sk_agent_close(a);
    sk_agent_close(a);
    CHECK_INT(the_fake.closed, 1);
    CHECK(!sk_agent_rename(a, "ok"));
    /* A closed part still reports its session, for Reopen closed tab. */
    sk_agent_report(a, &r);
    CHECK_STR(r.session, "s1");
    CHECK_INT(r.n_channels, 3);
    sk_agent_tick(a, 2000);
    CHECK_INT(the_fake.polled, 1); /* no tick after close */
    CHECK_STR(the_fake.order, "pac");
    sk_agent_free(a);
    CHECK_INT(the_fake.freed, 1);
    free(notice);
    sk_strv_free(&args);
    sk_bus_free(bus);
}

static void test_failed_prepare(void)
{
    memset(&the_fake, 0, sizeof the_fake);
    the_fake.fail = true;
    sk_bus *bus = sk_bus_new(NULL, NULL);
    char *cmd[] = {"fake", NULL};
    sk_agent_ctx c = ctx_for(bus, cmd);
    sk_agent *a = sk_agent_open_with(&c, &FAKE);
    sk_strv args = {0};
    char *notice = NULL;
    sk_agent_prepare(a, &args, &notice);
    CHECK_INT(args.len, 0);
    CHECK_STR(notice, SK_AGENT_FAILED);
    CHECK_INT(the_fake.closed, 1); /* closed at once */
    sk_agent_attach(a, 1);
    CHECK_INT(the_fake.attached, 0); /* a closed part is not attached */
    sk_report r;
    sk_agent_report(a, &r);
    CHECK_STR(r.notice, SK_AGENT_FAILED);
    CHECK_INT(r.n_channels, 3); /* the closed adapter's, then Generic's, as agent.js */
    sk_agent_free(a);
    free(notice);
    sk_strv_free(&args);
    sk_bus_free(bus);
}

/* writer.test.js: no pty name before attach, output per second after. */
static void test_generic_on_a_fake_clock(void)
{
    fake_now = 0;
    sk_bus *bus = sk_bus_new(NULL, NULL);
    char *cmd[] = {"bash", NULL};
    sk_agent_ctx c = ctx_for(bus, cmd);
    sk_agent *a = sk_agent_open(&c);
    sk_strv args = {0};
    char *notice = NULL;
    sk_agent_prepare(a, &args, &notice);
    CHECK(notice == NULL);
    sk_agent_tick(a, 0);
    CHECK_INT(sk_bus_count(bus), 0);
    sk_agent_attach(a, getpid());
    sk_agent_on_output(a, 1000);
    sk_agent_on_output(a, 2000);
    sk_agent_on_input(a);
    sk_bus_tick(bus, 1000);
    CHECK(num(bus, "pty.out_bps") == 3000);
    CHECK(num(bus, "pty.in_kps") == 1);
    CHECK(num(bus, "event.output") == 1); /* two writes in one burst */
    sk_bus_tick(bus, 2000);
    CHECK(num(bus, "pty.out_bps") == 0);
    CHECK(num(bus, "pty.in_kps") == 0);
    /* idle and the session clock follow the clock */
    sk_agent_tick(a, 5500);
    CHECK(num(bus, "pty.idle_s") == 5.5);
    CHECK(num(bus, "session.clock_s") == 5);
    fake_now = 5600;
    sk_agent_on_output(a, 10); /* after silence: a new burst */
    CHECK(num(bus, "event.output") == 2);
    int ev[8];
    CHECK_INT(sk_bus_drain_events(bus, ev, 8), 4);
    CHECK(sk_bus_get(bus, "proc.mem_mb") != NULL);
    sk_agent_free(a);
    free(notice);
    sk_strv_free(&args);
    sk_bus_free(bus);
}

/* agent.js watch(): a given part beside Generic, which counts its clock
 * from the session's own start. With manual_ticks no timer runs even on
 * a loop. */
static void test_watch_and_manual_ticks(void)
{
    memset(&the_fake, 0, sizeof the_fake);
    fake_now = 10000;
    sk_bus *bus = sk_bus_new(NULL, NULL);
    sk_loop *loop = sk_loop_new();
    char *cmd[] = {"fake", NULL};
    sk_agent_ctx c = ctx_for(bus, cmd);
    c.loop = loop;
    c.manual_ticks = true;
    c.launched = 4000;
    sk_agent *a = sk_agent_watch(&c, "claude", (sk_part){&FAKE_OPS, &the_fake});
    CHECK_STR(sk_agent_kind(a), "claude");
    sk_strv args = {0};
    char *notice = NULL;
    sk_agent_prepare(a, &args, &notice);
    sk_agent_attach(a, getpid());
    sk_loop_once(loop, 300);
    CHECK_INT(the_fake.polled, 0); /* no timer ticked it */
    sk_agent_tick(a, 10000);
    CHECK_INT(the_fake.polled, 1);
    CHECK(num(bus, "session.clock_s") == 6); /* from launched, not from now */
    sk_agent_free(a);
    sk_loop_free(loop);
    free(notice);
    sk_strv_free(&args);
    sk_bus_free(bus);
}

const sk_test sk_suite_agent[] = {
    T(test_watch_and_manual_ticks),
    T(test_dispatch_by_basename),
    T(test_order_of_operations),
    T(test_failed_prepare),
    T(test_generic_on_a_fake_clock),
    {0},
};
