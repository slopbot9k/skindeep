/* A monitor tab (decision 0027) on a fake watch part and a fake liveness:
 * what it tells its pane and strip, its polls, its end with the session
 * or the tab, and that it never has a pid of its own. */
#include "../../src/app/monitor.h"
#include "../test.h"
#include "fakes.h"

#include <stdlib.h>

typedef struct {
    sk_loop *loop;
    sk_tabs *tabs;
    sk_monitor_config mon;
    fake_part part;
    sk_foreign_session w;
    bool alive, refuse;
    int watched, strips, ended, status;
    pid_t asked_pid;
    double asked_started;
    sk_buf pane, label, cmdline, cwd, notice;
} app;

static int make_tab(void *ud, sk_tab *tab, const sk_tab_spec *spec, char **err)
{
    return sk_monitor_make(&((app *)ud)->mon, tab, spec, err);
}

static sk_agent *watcher(void *ud, const sk_foreign_session *w, sk_bus *bus)
{
    app *a = ud;
    a->watched++;
    CHECK(w == &a->w);
    return a->refuse ? NULL : fake_agent(&a->part, bus);
}

static bool same(void *ud, pid_t pid, double started)
{
    app *a = ud;
    a->asked_pid = pid;
    a->asked_started = started;
    return a->alive;
}

static void on_strip(void *ud, sk_tab *tab, const sk_tab_strip *s)
{
    (void)tab;
    app *a = ud;
    a->strips++;
    sk_buf_clear(&a->label);
    sk_buf_clear(&a->cmdline);
    sk_buf_clear(&a->cwd);
    sk_buf_clear(&a->notice);
    sk_buf_append_c(&a->label, s->label);
    sk_buf_append_c(&a->cmdline, s->cmdline);
    sk_buf_append_c(&a->cwd, s->cwd);
    sk_buf_append_c(&a->notice, s->notice ? s->notice : "(none)");
}

static void on_pane(void *ud, sk_tab *tab, const char *text)
{
    (void)tab;
    sk_buf_append_c(&((app *)ud)->pane, text);
}

static void on_ended(void *ud, int status)
{
    app *a = ud;
    a->ended++;
    a->status = status;
}

static void quiet(void *ud, const char *msg)
{
    (void)ud, (void)msg;
}

static void app_init(app *a)
{
    memset(a, 0, sizeof *a);
    a->loop = sk_loop_new();
    a->alive = true;
    char *argv[] = {"claude", "--model", "opus 4", NULL};
    fake_session(&a->w, 4242, 1700000000000.0, SK_PROFILE_CLAUDE, "/home/u/proj", argv);
    a->mon.loop = a->loop;
    a->mon.home = "/home/u";
    a->mon.watcher = watcher;
    a->mon.same = same;
    a->mon.strip = on_strip;
    a->mon.pane = on_pane;
    a->mon.log = quiet;
    a->mon.ud = a;
    sk_tabs_config tc;
    memset(&tc, 0, sizeof tc);
    tc.make = make_tab;
    tc.tell = quiet;
    tc.log = quiet;
    tc.ended = on_ended;
    tc.ud = a;
    tc.home = "/home/u";
    a->tabs = sk_tabs_new(&tc);
    a->mon.tabs = a->tabs;
}

static void app_free(app *a)
{
    sk_tabs_free(a->tabs);
    sk_loop_free(a->loop);
    fake_session_free(&a->w);
    sk_buf_free(&a->pane);
    sk_buf_free(&a->label);
    sk_buf_free(&a->cmdline);
    sk_buf_free(&a->cwd);
    sk_buf_free(&a->notice);
}

static sk_tab *add(app *a, char **err)
{
    sk_tab_spec spec = {SK_TAB_MONITOR, a->w.s.argv.ptr, a->w.s.cwd, NULL, NULL, "claude [work]", NULL,
                        &a->w};
    return sk_tabs_add(a->tabs, &spec, -1, err);
}

static sk_tab_session_state state_of(const sk_tab *tab)
{
    return tab->session.ops->state(tab->session.self);
}

static void test_a_monitor_watches_until_the_session_ends(void)
{
    app a;
    app_init(&a);
    sk_tab *tab = add(&a, NULL);
    CHECK(tab != NULL);
    if (!tab)
        goto done;
    const sk_tab_session *s = &tab->session;
    CHECK_INT(a.watched, 1); /* the agent is made with the tab */
    CHECK_INT(state_of(tab), SK_TAB_SESSION_IDLE);
    CHECK_INT(s->ops->pid(s->self), 0);
    CHECK(sk_monitor_agent(tab) != NULL && sk_monitor_bus(tab) != NULL);
    pid_t pid = 0;
    double started = 0;
    CHECK(sk_monitor_watched(tab, &pid, &started));
    CHECK_INT(pid, 4242);
    CHECK(started == 1700000000000.0);
    CHECK_INT(a.part.prepared, 0);

    sk_monitor_start(tab);
    CHECK_INT(state_of(tab), SK_TAB_SESSION_RUNNING);
    CHECK_INT(a.asked_pid, 4242);
    CHECK(a.asked_started == 1700000000000.0);
    CHECK_INT(a.part.prepared, 1);
    CHECK_INT(a.part.attached, 1);
    CHECK_INT(a.part.pid, 4242); /* attached to the watched process, never a child */
    CHECK_STR(sk_buf_cstr(&a.pane),
              "\x1b[1mRead-only monitor\x1b[0m of claude [work], pid 4242, in ~/proj.\r\n"
              "The session runs in another terminal. This tab shows its meters and\r\n"
              "never types into it. Closing this tab ends the monitor, not the session.\r\n");
    CHECK_INT(a.strips, 1);
    CHECK_STR(sk_buf_cstr(&a.label), "claude [work]");
    CHECK_STR(sk_buf_cstr(&a.cmdline), "claude --model 'opus 4'");
    CHECK_STR(sk_buf_cstr(&a.cwd), "~/proj");
    CHECK_STR(sk_buf_cstr(&a.notice), "read-only monitor");

    /* A second start changes nothing. */
    sk_monitor_start(tab);
    CHECK_INT(a.part.prepared, 1);

    /* The agent's notice goes to the strip, once. */
    a.part.notice = "meters waiting: no transcript found";
    sk_monitor_poll(tab);
    CHECK_INT(a.strips, 2);
    CHECK_STR(sk_buf_cstr(&a.notice), "meters waiting: no transcript found  read-only monitor");
    sk_monitor_poll(tab);
    CHECK_INT(a.strips, 2);
    sk_tab_strip strip;
    CHECK(sk_monitor_strip(tab, &strip));
    CHECK_STR(strip.notice, "meters waiting: no transcript found  read-only monitor");
    CHECK_STR(strip.cwd, "~/proj");
    a.part.notice = NULL;
    sk_monitor_poll(tab);
    CHECK_INT(a.strips, 3);
    CHECK_STR(sk_buf_cstr(&a.notice), "read-only monitor");

    /* What it follows is the agent's; it keeps no name of its own. */
    a.part.session = "s1";
    a.part.title = "fix the bug";
    CHECK_STR(s->ops->session_id(s->self), "s1");
    CHECK_STR(sk_tab_name(tab), "fix the bug");
    CHECK(!s->ops->rename(s->self, "another"));
    CHECK(s->ops->last_screen(s->self) == NULL);

    /* The session's end ends the tab, with no status of its own, from
     * the loop. */
    a.alive = false;
    sk_monitor_poll(tab);
    CHECK_INT(state_of(tab), SK_TAB_SESSION_ENDED);
    CHECK_INT(a.part.closed, 1);
    CHECK_INT(sk_tabs_count(a.tabs), 1);
    CHECK_INT(a.ended, 0);
    sk_loop_once(a.loop, 0);
    CHECK_INT(a.ended, 1);
    CHECK_INT(a.status, 0);
    CHECK_INT(sk_tabs_count(a.tabs), 0);
done:
    app_free(&a);
    CHECK_INT(a.part.freed, 1);
}

/* The polls run on the loop, every poll_ms. */
static void test_the_polls_run_on_the_loop(void)
{
    app a;
    app_init(&a);
    a.mon.poll_ms = 5;
    sk_tab *tab = add(&a, NULL);
    CHECK(tab != NULL);
    if (tab) {
        sk_monitor_start(tab);
        a.part.notice = "meters limited: adapter failed";
        uint64_t until = sk_now_ms() + 2000;
        while (a.strips < 2 && sk_now_ms() < until)
            sk_loop_once(a.loop, 20);
        CHECK_STR(sk_buf_cstr(&a.notice), "meters limited: adapter failed  read-only monitor");
        a.alive = false;
        until = sk_now_ms() + 2000;
        while (!a.ended && sk_now_ms() < until)
            sk_loop_once(a.loop, 20);
        CHECK_INT(a.ended, 1);
    }
    app_free(&a);
}

/* A session that ended before the pane was ready ends the tab: nothing
 * is attached and the pane is not told. */
static void test_a_session_gone_at_the_start_ends_the_tab(void)
{
    app a;
    app_init(&a);
    a.alive = false;
    sk_tab *tab = add(&a, NULL);
    CHECK(tab != NULL);
    if (tab) {
        sk_monitor_start(tab);
        CHECK_INT(state_of(tab), SK_TAB_SESSION_ENDED);
        CHECK_INT(a.part.prepared, 1);
        CHECK_INT(a.part.attached, 0);
        CHECK_INT(a.part.closed, 1);
        CHECK_INT(a.pane.len, 0);
        CHECK_INT(a.strips, 0);
        sk_loop_once(a.loop, 0);
        CHECK_INT(a.ended, 1);
        CHECK_INT(a.status, 0);
    }
    app_free(&a);
}

/* Closing the tab ends the monitor, not the session: the agent is
 * closed, nothing is signalled, and the end is 0. Closed before its
 * start, it never prepares. */
static void test_closing_the_tab_ends_the_monitor(void)
{
    app a;
    app_init(&a);
    sk_tab *first = add(&a, NULL), *second = add(&a, NULL);
    CHECK(first && second);
    if (first && second) {
        sk_monitor_start(first);
        sk_tabs_close(a.tabs, first->id);
        CHECK_INT(state_of(first), SK_TAB_SESSION_ENDED);
        sk_tabs_close(a.tabs, second->id);
        CHECK_INT(state_of(second), SK_TAB_SESSION_ENDED);
        sk_monitor_start(second); /* too late: nothing starts */
        CHECK_INT(a.part.prepared, 1);
        CHECK_INT(a.part.attached, 1);
        CHECK_INT(a.part.closed, 2);
        sk_loop_once(a.loop, 0);
        CHECK_INT(a.ended, 1);
        CHECK_INT(a.status, 0);
        CHECK_INT(sk_tabs_count(a.tabs), 0);
    }
    app_free(&a);
    CHECK_INT(a.part.freed, 2);
}

/* The app's end frees a monitor that still runs. */
static void test_a_running_monitor_is_freed_with_the_tabs(void)
{
    app a;
    app_init(&a);
    sk_tab *tab = add(&a, NULL);
    CHECK(tab != NULL);
    if (tab)
        sk_monitor_start(tab);
    app_free(&a);
    CHECK_INT(a.part.closed, 1);
    CHECK_INT(a.part.freed, 1);
}

static void test_a_monitor_needs_a_session_and_an_agent(void)
{
    app a;
    app_init(&a);
    char *err = NULL;
    sk_tab_spec spec = {SK_TAB_MONITOR, a.w.s.argv.ptr, "/", NULL, NULL, "claude", NULL, NULL};
    CHECK(sk_tabs_add(a.tabs, &spec, -1, &err) == NULL);
    CHECK_STR(err, "the app has no session to watch");
    free(err);
    err = NULL;
    a.refuse = true;
    CHECK(add(&a, &err) == NULL);
    CHECK_STR(err, "the session cannot be watched");
    free(err);
    CHECK_INT(sk_tabs_count(a.tabs), 0);
    app_free(&a);
}

static void noop(void *self)
{
    (void)self;
}

/* Another kind of tab has no monitor parts. */
static void test_other_tabs_have_no_monitor_parts(void)
{
    static const sk_tab_session_ops other = {noop, NULL, NULL, NULL, NULL, NULL, NULL, noop};
    sk_tab tab;
    memset(&tab, 0, sizeof tab);
    tab.session.ops = &other;
    pid_t pid;
    double started;
    sk_tab_strip strip;
    CHECK(sk_monitor_agent(&tab) == NULL);
    CHECK(sk_monitor_bus(&tab) == NULL);
    CHECK(!sk_monitor_watched(&tab, &pid, &started));
    CHECK(!sk_monitor_strip(&tab, &strip));
    CHECK(sk_monitor_agent(NULL) == NULL);
    sk_monitor_start(&tab);
    sk_monitor_poll(&tab);
}

const sk_test sk_suite_monitor[] = {
    T(test_a_monitor_watches_until_the_session_ends),
    T(test_the_polls_run_on_the_loop),
    T(test_a_session_gone_at_the_start_ends_the_tab),
    T(test_closing_the_tab_ends_the_monitor),
    T(test_a_running_monitor_is_freed_with_the_tabs),
    T(test_a_monitor_needs_a_session_and_an_agent),
    T(test_other_tabs_have_no_monitor_parts),
    {0},
};
