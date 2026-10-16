/* A CLI tab's session behind the tabs: made by sk_clitab_make in its own
 * tab-<id> dir, started by the window, its end the tabs' to report. */
#include "../../src/app/clitab.h"
#include "../../src/base/paths.h"
#include "../../src/core/command.h"
#include "../test.h"

#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

extern char **environ;

typedef struct {
    sk_loop *loop;
    sk_tabs *tabs;
    sk_clitab_config cli;
    sk_envp env;
    char *sh;
    sk_buf out;
    pid_t spawned;
    int ended, status;
} app;

static int make_tab(void *ud, sk_tab *tab, const sk_tab_spec *spec, char **err)
{
    return sk_clitab_make(&((app *)ud)->cli, tab, spec, err);
}

static void on_spawned(void *ud, sk_tab *tab, pid_t pid)
{
    (void)tab;
    ((app *)ud)->spawned = pid;
}

static void on_output(void *ud, sk_tab *tab, const char *bytes, size_t n)
{
    (void)tab;
    sk_buf_append(&((app *)ud)->out, bytes, n);
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

/* The app's lines come with the app's ud, from the session as from the
 * tabs. */
static app *the_app;
static int told;

static void tell_app(void *ud, const char *msg)
{
    (void)msg;
    CHECK(ud == the_app);
    told++;
}

static char *const window_cmd[] = {"sh", NULL};

static void app_init(app *a, const char *run_dir)
{
    memset(a, 0, sizeof *a);
    a->loop = sk_loop_new();
    sk_envp_from(&a->env, environ);
    a->sh = sk_resolve_command("sh", sk_envp_get(&a->env, "PATH"));
    a->cli.loop = a->loop;
    a->cli.run_dir = run_dir;
    a->cli.root = sk_test_tmpdir();
    a->cli.store = sk_test_tmpdir();
    a->cli.spawned = on_spawned;
    a->cli.output = on_output;
    a->cli.log = quiet;
    a->cli.tell = quiet;
    a->cli.ud = a;
    sk_tabs_config tc;
    memset(&tc, 0, sizeof tc);
    tc.make = make_tab;
    tc.tell = quiet;
    tc.log = quiet;
    tc.ended = on_ended;
    tc.ud = a;
    tc.cmd = window_cmd;
    tc.file = a->sh;
    tc.env = &a->env;
    tc.home = "/nonexistent";
    a->tabs = sk_tabs_new(&tc);
    a->cli.tabs = a->tabs;
}

static void app_free(app *a)
{
    sk_tabs_free(a->tabs);
    sk_loop_free(a->loop);
    sk_envp_free(&a->env);
    sk_buf_free(&a->out);
    free(a->sh);
}

/* <tmp>/<name>, made. */
static char *run_dir(const char *name)
{
    char *dir = sk_path_join(sk_test_tmpdir(), name);
    mkdir(dir, 0700);
    return dir;
}

static sk_tab *add(app *a, const char *script, char **err)
{
    char *cmd[] = {"sh", "-c", (char *)script, NULL};
    sk_tab_spec spec;
    memset(&spec, 0, sizeof spec);
    spec.kind = SK_TAB_CLI;
    spec.cmd = cmd;
    spec.cwd = sk_test_tmpdir();
    spec.note = "not carried: --x";
    return sk_tabs_add(a->tabs, &spec, -1, err);
}

static bool wait_ended(app *a, int ms)
{
    uint64_t until = sk_now_ms() + (uint64_t)ms;
    while (!a->ended && sk_now_ms() < until)
        sk_loop_once(a->loop, 50);
    return a->ended > 0;
}

static void test_a_tab_runs_its_command_and_its_end_ends_the_app(void)
{
    char *dir = run_dir("run-one");
    app a;
    app_init(&a, dir);
    sk_tab *tab = add(&a, "echo hello; exit 7", NULL);
    CHECK(tab != NULL);
    if (!tab)
        goto done;
    CHECK(sk_clitab_session(tab) != NULL);
    CHECK(sk_clitab_bus(tab) != NULL);
    CHECK_STR(sk_agent_kind(sk_clitab_agent(tab)), "plain");
    CHECK_STR(sk_clitab_note(tab), "not carried: --x");
    char *tab_dir = sk_path_join(dir, "tab-1");
    struct stat st;
    CHECK(stat(tab_dir, &st) == 0 && S_ISDIR(st.st_mode));
    free(tab_dir);
    const sk_tab_session *s = &tab->session;
    CHECK_INT(s->ops->state(s->self), SK_TAB_SESSION_IDLE);
    CHECK_INT(s->ops->pid(s->self), 0);
    sk_session_start(sk_clitab_session(tab), 80, 24);
    CHECK(wait_ended(&a, 5000));
    CHECK_INT(a.status, 7);
    CHECK_INT(sk_tabs_exit_status(a.tabs), 7);
    CHECK(strstr(sk_buf_cstr(&a.out), "hello") != NULL);
    /* The last tab is kept, and its session with it. */
    sk_tab *last = sk_tabs_last(a.tabs);
    CHECK(last == tab);
    CHECK_INT(s->ops->state(s->self), SK_TAB_SESSION_ENDED);
    CHECK(a.spawned > 0);
    CHECK_INT(s->ops->pid(s->self), a.spawned);
    /* Plain keeps no record of the session. */
    CHECK(s->ops->session_id(s->self) == NULL);
    CHECK(s->ops->title(s->self) == NULL);
    CHECK(!s->ops->rename(s->self, "a name"));
    CHECK(s->ops->last_screen(s->self) == NULL);
done:
    app_free(&a);
    free(dir);
}

/* A tab ended goes, and the app ends with the last, by its status. */
static void test_the_app_ends_with_its_last_tab(void)
{
    char *dir = run_dir("run-two");
    app a;
    app_init(&a, dir);
    sk_tab *first = add(&a, "exit 3", NULL), *second = add(&a, "sleep 0.3; exit 4", NULL);
    CHECK(first && second);
    if (!first || !second)
        goto done;
    int second_id = second->id;
    sk_session_start(sk_clitab_session(first), 80, 24);
    sk_session_start(sk_clitab_session(second), 80, 24);
    CHECK(wait_ended(&a, 5000));
    CHECK_INT(a.ended, 1);
    CHECK_INT(a.status, 4);
    CHECK_INT(sk_tabs_count(a.tabs), 0);
    CHECK(sk_tabs_last(a.tabs) && sk_tabs_last(a.tabs)->id == second_id);
done:
    app_free(&a);
    free(dir);
}

/* Closed before the window started it: no spawn, and the end comes from
 * the loop. */
static void test_a_tab_closed_before_its_start_ends_without_a_child(void)
{
    char *dir = run_dir("run-closed");
    app a;
    app_init(&a, dir);
    sk_tab *tab = add(&a, "exit 0", NULL);
    CHECK(tab != NULL);
    if (tab) {
        sk_tabs_close(a.tabs, tab->id);
        CHECK(wait_ended(&a, 2000));
        CHECK_INT(a.status, SK_EXIT_BEFORE_SPAWN);
        CHECK_INT(a.spawned, 0);
    }
    app_free(&a);
    free(dir);
}

static void test_the_sessions_lines_reach_the_app_with_its_ud(void)
{
    char *dir = run_dir("run-tell");
    app a;
    app_init(&a, dir);
    the_app = &a;
    told = 0;
    a.cli.log = tell_app;
    a.cli.tell = tell_app;
    char *cmd[] = {"no-such-program", NULL};
    sk_tab_spec spec;
    memset(&spec, 0, sizeof spec);
    spec.kind = SK_TAB_CLI;
    spec.cmd = cmd;
    spec.cwd = sk_test_tmpdir();
    spec.file = "/nonexistent/no-such-program";
    sk_tab *tab = sk_tabs_add(a.tabs, &spec, -1, NULL);
    CHECK(tab != NULL);
    if (tab) {
        sk_session_start(sk_clitab_session(tab), 80, 24);
        CHECK(wait_ended(&a, 5000));
        CHECK_INT(a.status, SK_EXIT_NOT_FOUND);
        CHECK(told > 0); /* "cannot start" */
    }
    the_app = NULL;
    app_free(&a);
    free(dir);
}

static void test_a_tab_with_no_dir_for_it_is_not_made(void)
{
    char *dir = sk_path_join(sk_test_tmpdir(), "no/such/run");
    app a;
    app_init(&a, dir);
    char *err = NULL;
    CHECK(add(&a, "exit 0", &err) == NULL);
    CHECK(err && strstr(err, "cannot make") && strstr(err, "tab-1"));
    CHECK_INT(sk_tabs_count(a.tabs), 0);
    free(err);
    app_free(&a);
    free(dir);
}

static void noop(void *self)
{
    (void)self;
}

static sk_tab_session_state ended_state(void *self)
{
    (void)self;
    return SK_TAB_SESSION_ENDED;
}

/* A monitor's or the dashboard's tab has no CLI parts. */
static void test_other_tabs_have_no_cli_parts(void)
{
    static const sk_tab_session_ops other = {noop, ended_state, NULL, NULL, NULL, NULL, NULL, noop};
    sk_tab tab;
    memset(&tab, 0, sizeof tab);
    tab.kind = SK_TAB_MONITOR;
    tab.session.ops = &other;
    CHECK(sk_clitab_session(&tab) == NULL);
    CHECK(sk_clitab_agent(&tab) == NULL);
    CHECK(sk_clitab_bus(&tab) == NULL);
    CHECK(sk_clitab_note(&tab) == NULL);
    CHECK(sk_clitab_session(NULL) == NULL);
}

const sk_test sk_suite_clitab[] = {
    T(test_a_tab_runs_its_command_and_its_end_ends_the_app),
    T(test_the_app_ends_with_its_last_tab),
    T(test_a_tab_closed_before_its_start_ends_without_a_child),
    T(test_the_sessions_lines_reach_the_app_with_its_ud),
    T(test_a_tab_with_no_dir_for_it_is_not_made),
    T(test_other_tabs_have_no_cli_parts),
    {0},
};
