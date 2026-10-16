/* The sessions dashboard (decision 0028) on a fake list of sessions and
 * fake watch parts, as test/dashboard.test.js has it: a replayed history
 * is no rise, nor is another session's (/resume), and the dials
 * (decision 0033). Then the watchers of sessions elsewhere, the rows'
 * boards (decision 0029), its end and openDashboard. */
#include "../../src/app/dashboard.h"
#include "../test.h"
#include "fakes.h"

#include <stdlib.h>

#define ROWS 4

/* A session the fake list gives: in a tab when tab is set, with the
 * test's agent and bus, else elsewhere. */
typedef struct {
    pid_t pid;
    const char *key, *cwd;
    sk_tab *tab;
    sk_agent *agent;
    sk_bus *bus;
} row;

typedef struct {
    sk_loop *loop;
    sk_tabs *tabs;
    sk_dashboard_config dash;
    row rows[ROWS];
    size_t n_rows;
    bool list_fails;
    fake_part parts[ROWS]; /* the watchers', in the order made */
    pid_t watched[ROWS];
    size_t n_watched;
    sk_board board;
    sk_strv boards_asked;
    sk_strv views;
    int strips, ended, status;
    char *strip_label;
} world;

static double fake_now = 1700000000000.0;

static double now(void)
{
    return fake_now;
}

static void fake_list(void *ud, sk_foreign_entries *out)
{
    world *w = ud;
    for (size_t i = 0; i < w->n_rows; i++) {
        const row *r = &w->rows[i];
        sk_foreign_entry e;
        memset(&e, 0, sizeof e);
        char *argv[] = {"claude", NULL};
        fake_session(&e.s, r->pid, 1000, SK_PROFILE_CLAUDE, r->cwd, argv);
        e.tab = r->tab;
        e.agent = r->agent;
        e.bus = r->bus;
        e.key = sk_xstrdup(r->key);
        e.folder = sk_xstrdup("~/x");
        e.label = sk_xstrdup("claude");
        e.tab_id = r->tab ? r->tab->id : 0;
        e.where = r->tab ? "tab" : "";
        e.state = "working";
        sk_vec_push(out, e);
    }
}

static sk_agent *watcher(void *ud, const sk_foreign_session *s, sk_bus *bus)
{
    world *w = ud;
    if (w->n_watched >= ROWS)
        return NULL;
    w->watched[w->n_watched] = s->s.pid;
    return fake_agent(&w->parts[w->n_watched++], bus);
}

static void board(void *ud, const char *folder, sk_board *out)
{
    world *w = ud;
    sk_strv_push(&w->boards_asked, folder);
    *out = w->board;
}

static void on_strip(void *ud, sk_tab *tab, const sk_tab_strip *s)
{
    (void)tab;
    world *w = ud;
    w->strips++;
    free(w->strip_label);
    w->strip_label = sk_xstrdup(s->label);
    CHECK_STR(s->cmdline, "every live agent session, sampled every 5 s");
    CHECK_STR(s->cwd, "");
    CHECK_STR(s->notice, "read-only dashboard");
}

static void on_send(void *ud, sk_tab *tab, const char *view)
{
    (void)tab;
    sk_strv_push(&((world *)ud)->views, view);
}

static void on_ended(void *ud, int status)
{
    world *w = ud;
    w->ended++;
    w->status = status;
}

static void quiet(void *ud, const char *msg)
{
    (void)ud, (void)msg;
}

static void fake_hangup(void *self)
{
    (void)self;
}

static void fake_free_session(void *self)
{
    (void)self;
}

/* A tab of another kind: it never ends by itself. */
static const sk_tab_session_ops OTHER = {fake_hangup, NULL, NULL, NULL, NULL, NULL, NULL, fake_free_session};

static int make_tab(void *ud, sk_tab *tab, const sk_tab_spec *spec, char **err)
{
    world *w = ud;
    if (spec->kind == SK_TAB_DASHBOARD)
        return sk_dashboard_make(&w->dash, tab, spec, err);
    tab->session = (sk_tab_session){&OTHER, NULL};
    return 0;
}

static void world_init(world *w)
{
    memset(w, 0, sizeof *w);
    w->loop = sk_loop_new();
    w->dash.loop = w->loop;
    w->dash.list = fake_list;
    w->dash.watcher = watcher;
    w->dash.now = now;
    w->dash.strip = on_strip;
    w->dash.send = on_send;
    w->dash.log = quiet;
    w->dash.ud = w;
    sk_tabs_config tc;
    memset(&tc, 0, sizeof tc);
    tc.make = make_tab;
    tc.tell = quiet;
    tc.log = quiet;
    tc.ended = on_ended;
    tc.ud = w;
    tc.home = "/nonexistent";
    w->tabs = sk_tabs_new(&tc);
    w->dash.tabs = w->tabs;
}

static void world_free(world *w)
{
    sk_tabs_free(w->tabs);
    sk_loop_free(w->loop);
    sk_strv_free(&w->boards_asked);
    sk_strv_free(&w->views);
    free(w->strip_label);
}

static sk_tab *open_dashboard(world *w)
{
    char *err = NULL;
    sk_tab *tab = sk_dashboard_open(w->tabs, "/app", &err);
    CHECK(tab != NULL);
    CHECK(err == NULL);
    free(err);
    return tab;
}

static const char *last_view(const world *w)
{
    return w->views.len ? w->views.ptr[w->views.len - 1] : NULL;
}

/* The compact JSON at path of the last view, compared with want. */
#define CHECK_VIEW(w, want, ...)                                             \
    do {                                                                     \
        const char *const path_[] = {__VA_ARGS__, NULL};                     \
        char *got_ = json_at(last_view(w), path_);                           \
        CHECK_STR(got_, want);                                               \
        free(got_);                                                          \
    } while (0)

/* A session in a tab: its agent on the test's part, and a source on its
 * bus the test writes. */
typedef struct {
    sk_tab tab;
    fake_part part;
    sk_bus *bus;
    sk_agent *agent;
    sk_source *src;
} tabbed;

static void tabbed_init(tabbed *t, int id)
{
    memset(t, 0, sizeof *t);
    t->tab.id = id;
    t->bus = sk_bus_new(NULL, NULL);
    t->agent = fake_agent(&t->part, t->bus);
    static const sk_source_spec none = {NULL, 0, NULL, 0};
    t->src = sk_bus_source(t->bus, &none);
}

static void tabbed_free(tabbed *t)
{
    sk_agent_free(t->agent);
    sk_bus_free(t->bus);
}

static void in_tab(world *w, tabbed *t, const char *key)
{
    w->rows[0] = (row){100, key, "/x", &t->tab, t->agent, t->bus};
    w->n_rows = 1;
}

/* dashboard.test.js: a session whose history is still being read gives
 * no values, so its replayed totals are no rise. */
static void test_a_replayed_history_is_no_rise(void)
{
    world w;
    world_init(&w);
    tabbed t;
    tabbed_init(&t, 7);
    in_tab(&w, &t, "k");
    t.part.replaying = true;
    sk_source_set_num(t.src, "tokens.out", 400000);
    sk_tab *d = open_dashboard(&w);
    sk_dashboard_start(d);
    t.part.replaying = false;
    sk_source_set_num(t.src, "tokens.out", 1000000);
    sk_dashboard_sample(d);
    sk_source_set_num(t.src, "tokens.out", 1000600);
    sk_dashboard_sample(d);
    CHECK_VIEW(&w, "[null,1000000,1000600]", "rows", "#0", "series", "tokens");
    CHECK_VIEW(&w, "[0,0,600]", "across", "tokensPerMin");
    CHECK_INT(w.n_watched, 0); /* a tab needs no watcher */
    world_free(&w);
    tabbed_free(&t);
}

/* dashboard.test.js: a tab whose agent begins another session (/resume)
 * shows that session's totals as no rise. */
static void test_another_session_of_a_tab_is_no_rise(void)
{
    world w;
    world_init(&w);
    tabbed t;
    tabbed_init(&t, 7);
    in_tab(&w, &t, "k");
    t.part.session = "s1";
    sk_source_set_num(t.src, "tokens.out", 100);
    sk_tab *d = open_dashboard(&w);
    sk_dashboard_start(d);
    t.part.session = "s2";
    sk_source_set_num(t.src, "tokens.out", 2000000);
    sk_dashboard_sample(d);
    sk_source_set_num(t.src, "tokens.out", 2000300);
    sk_dashboard_sample(d);
    CHECK_VIEW(&w, "[0,0,300]", "across", "tokensPerMin");
    world_free(&w);
    tabbed_free(&t);
}

/* dashboard.test.js: a dial set on the dashboard is sampled from each
 * session's bus and sent at once. */
static void test_a_dial_is_sampled_and_sent_at_once(void)
{
    world w;
    world_init(&w);
    tabbed t;
    tabbed_init(&t, 7);
    in_tab(&w, &t, "k");
    t.part.session = "s1";
    sk_source_set_num(t.src, "tokens.out", 100);
    sk_source_set_num(t.src, "tools.Read", 4);
    sk_tab *d = open_dashboard(&w);
    sk_dashboard_start(d);
    size_t before = w.views.len;
    char *dials[] = {"tools.Read"};
    sk_dashboard_set_dials(d, dials, 1);
    CHECK_INT(w.views.len, before + 1);
    CHECK_VIEW(&w, "[\"tools.Read\"]", "dials");
    sk_dashboard_sample(d);
    CHECK_VIEW(&w, "[null,4]", "rows", "#0", "series", "tools.Read");
    sk_buf report = {0};
    sk_jw jw;
    sk_jw_init(&jw, &report, 0);
    CHECK(sk_dashboard_report(d, &jw));
    const char *const dials_at[] = {"dials", NULL};
    char *got = json_at(report.ptr, dials_at);
    CHECK_STR(got, "[\"tools.Read\"]");
    free(got);
    sk_buf_free(&report);
    world_free(&w);
    tabbed_free(&t);
}

/* The app's dials when it is made are its first, and nothing is sent
 * before it starts. */
static void test_the_apps_dials_come_with_it(void)
{
    world w;
    world_init(&w);
    sk_strv dials = {0};
    sk_strv_push(&dials, "tools.Bash");
    w.dash.dials = &dials;
    sk_tab *d = open_dashboard(&w);
    char *more[] = {"tools.Bash", "tools.Edit"};
    sk_dashboard_set_dials(d, more, 2);
    CHECK_INT(w.views.len, 0);
    sk_dashboard_start(d);
    CHECK_INT(w.strips, 1);
    CHECK_STR(w.strip_label, "sessions");
    CHECK_VIEW(&w, "[\"tools.Bash\",\"tools.Edit\"]", "dials");
    CHECK_VIEW(&w, "5000", "intervalMs");
    CHECK_VIEW(&w, "[1700000000000]", "ticks");
    sk_tab_strip strip;
    CHECK(sk_dashboard_strip(d, &strip));
    CHECK_STR(strip.label, "sessions");
    CHECK(sk_dashboard_bus(d) != NULL);
    world_free(&w);
    sk_strv_free(&dials);
}

static char *report_of(sk_tab *d)
{
    sk_buf out = {0};
    sk_jw jw;
    sk_jw_init(&jw, &out, 0);
    CHECK(sk_dashboard_report(d, &jw));
    return out.ptr;
}

/* A session elsewhere gets one watcher, attached to its process, closed
 * when a tab takes it; another session gets its own, closed when it
 * leaves the list. */
static void test_a_session_elsewhere_is_watched_while_it_is_listed(void)
{
    world w;
    world_init(&w);
    tabbed t;
    tabbed_init(&t, 9);
    w.rows[0] = (row){4242, "4242:1000", "/x", NULL, NULL, NULL};
    w.rows[1] = (row){5151, "5151:1000", "/y", NULL, NULL, NULL};
    w.n_rows = 2;
    sk_tab *d = open_dashboard(&w);
    sk_dashboard_start(d);
    CHECK_INT(w.n_watched, 2);
    CHECK_INT(w.watched[0], 4242);
    CHECK_INT(w.parts[0].prepared, 1);
    CHECK_INT(w.parts[0].attached, 1);
    CHECK_INT(w.parts[0].pid, 4242);
    w.parts[0].state = SK_CH_LIVE;
    sk_dashboard_sample(d);
    CHECK_INT(w.n_watched, 2); /* made once */
    CHECK_INT(w.parts[0].attached, 1);
    char *r = report_of(d);
    CHECK_STR(r, "{\"ticks\":2,\"rows\":[\"4242:1000\",\"5151:1000\"],\"dials\":[],"
                 "\"watching\":[\"4242:1000\",\"5151:1000\"],\"made\":["
                 "{\"key\":\"4242:1000\",\"closed\":false,\"tailing\":true},"
                 "{\"key\":\"5151:1000\",\"closed\":false,\"tailing\":false}]}");
    free(r);
    /* A monitor tab takes the first; the second ends. */
    w.rows[0] = (row){4242, "4242:1000", "/x", &t.tab, t.agent, t.bus};
    w.n_rows = 1;
    sk_dashboard_sample(d);
    CHECK_INT(w.parts[0].closed, 1);
    CHECK_INT(w.parts[0].freed, 1);
    CHECK_INT(w.parts[1].closed, 1);
    CHECK_INT(w.parts[1].freed, 1);
    CHECK_VIEW(&w, "false", "rows", "#0", "ended");
    CHECK_VIEW(&w, "true", "rows", "#1", "ended");
    /* The tab leaves it again: a new watcher. */
    w.rows[0] = (row){4242, "4242:1000", "/x", NULL, NULL, NULL};
    sk_dashboard_sample(d);
    CHECK_INT(w.n_watched, 3);
    r = report_of(d);
    CHECK_STR(r, "{\"ticks\":4,\"rows\":[\"4242:1000\",\"5151:1000\"],\"dials\":[],"
                 "\"watching\":[\"4242:1000\"],\"made\":["
                 "{\"key\":\"4242:1000\",\"closed\":true,\"tailing\":false},"
                 "{\"key\":\"5151:1000\",\"closed\":true,\"tailing\":false},"
                 "{\"key\":\"4242:1000\",\"closed\":false,\"tailing\":false}]}");
    free(r);
    /* Its end closes every watcher, and reaches the tabs from the loop. */
    sk_tabs_close(w.tabs, d->id);
    CHECK_INT(w.parts[2].closed, 1);
    CHECK_INT(w.parts[2].freed, 1);
    size_t sent = w.views.len;
    sk_dashboard_sample(d);
    CHECK_INT(w.views.len, sent); /* nothing after the end */
    CHECK_INT(w.ended, 0);
    sk_loop_once(w.loop, 0);
    CHECK_INT(w.ended, 1);
    CHECK_INT(w.status, 0);
    world_free(&w);
    tabbed_free(&t);
}

/* Each row carries what the menu shows of it and its project's board,
 * read for its folder. */
static void test_a_row_carries_its_meta_and_board(void)
{
    world w;
    world_init(&w);
    w.dash.board = board;
    tabbed t;
    tabbed_init(&t, 9);
    w.rows[0] = (row){4242, "4242:1000", "/x/proj", &t.tab, t.agent, t.bus};
    w.rows[1] = (row){5151, "5151:1000", "/y", NULL, NULL, NULL};
    w.n_rows = 2;
    w.board.state = SK_BOARD_OK;
    for (int i = 0; i < SK_BOARD_DAYS; i++) {
        w.board.series.open[i] = 3;
        w.board.series.closed[i] = i == SK_BOARD_DAYS - 1;
    }
    w.board.series.open_now = 3;
    w.board.series.closed14 = 1;
    sk_tab *d = open_dashboard(&w);
    sk_dashboard_start(d);
    CHECK_INT(w.boards_asked.len, 2);
    CHECK_STR(w.boards_asked.ptr[0], "/x/proj");
    CHECK_VIEW(&w, "\"claude\"", "rows", "#0", "label");
    CHECK_VIEW(&w, "\"~/x\"", "rows", "#0", "folder");
    CHECK_VIEW(&w, "\"working\"", "rows", "#0", "state");
    CHECK_VIEW(&w, "\"tab\"", "rows", "#0", "where");
    CHECK_VIEW(&w, "9", "rows", "#0", "tab");
    CHECK_VIEW(&w, "null", "rows", "#1", "tab");
    CHECK_VIEW(&w, "{\"state\":\"ok\",\"open\":[3,3,3,3,3,3,3,3,3,3,3,3,3,3],"
                   "\"closed\":[0,0,0,0,0,0,0,0,0,0,0,0,0,1],\"openNow\":3,\"closed14\":1}",
               "rows", "#0", "board");
    w.board.state = SK_BOARD_LOADING;
    sk_dashboard_sample(d);
    CHECK_VIEW(&w, "{\"state\":\"loading\"}", "rows", "#0", "board");
    w.board.state = SK_BOARD_NO_ACCESS;
    sk_dashboard_sample(d);
    CHECK_VIEW(&w, "{\"state\":\"no access\"}", "rows", "#0", "board");
    w.board.state = SK_BOARD_NONE;
    sk_dashboard_sample(d);
    CHECK_VIEW(&w, "null", "rows", "#0", "board");
    /* With no board callback, no project. */
    w.dash.board = NULL;
    sk_dashboard_sample(d);
    CHECK_VIEW(&w, "null", "rows", "#1", "board");
    world_free(&w);
    tabbed_free(&t);
}

/* The timer samples every interval. */
static void test_the_timer_samples_every_interval(void)
{
    world w;
    world_init(&w);
    w.dash.interval_ms = 5;
    sk_tab *d = open_dashboard(&w);
    sk_dashboard_start(d);
    uint64_t until = sk_now_ms() + 2000;
    while (w.views.len < 3 && sk_now_ms() < until)
        sk_loop_once(w.loop, 20);
    CHECK(w.views.len >= 3);
    world_free(&w);
}

/* openDashboard: one dashboard, made active when asked again. */
static void test_one_dashboard_is_opened(void)
{
    world w;
    world_init(&w);
    sk_tab *d = open_dashboard(&w);
    CHECK(d && d->kind == SK_TAB_DASHBOARD);
    CHECK_STR(d->label, "sessions");
    CHECK_STR(d->cwd, "/app");
    CHECK_STR(d->cmd.ptr[0], "sessions");
    CHECK(d->file == NULL);
    const sk_tab_session *s = &d->session;
    CHECK_INT(s->ops->pid(s->self), 0);
    CHECK_INT(s->ops->state(s->self), SK_TAB_SESSION_IDLE);
    CHECK(s->ops->session_id == NULL && s->ops->title == NULL && s->ops->rename == NULL);
    char *cmd[] = {"other", NULL};
    sk_tab_spec spec = {SK_TAB_CLI, cmd, "/", NULL, NULL, NULL, NULL, NULL};
    sk_tab *other = sk_tabs_add(w.tabs, &spec, -1, NULL);
    CHECK(sk_tabs_active(w.tabs) == other);
    CHECK(open_dashboard(&w) == d);
    CHECK(sk_tabs_active(w.tabs) == d);
    CHECK_INT(sk_tabs_count(w.tabs), 2);
    /* Another tab has no dashboard parts. */
    sk_tab_strip strip;
    CHECK(!sk_dashboard_strip(other, &strip));
    CHECK(sk_dashboard_bus(other) == NULL);
    sk_buf out = {0};
    sk_jw jw;
    sk_jw_init(&jw, &out, 0);
    CHECK(!sk_dashboard_report(other, &jw));
    CHECK_INT(out.len, 0);
    sk_dashboard_start(other);
    sk_dashboard_sample(other);
    world_free(&w);
}

const sk_test sk_suite_dashboard[] = {
    T(test_a_replayed_history_is_no_rise),
    T(test_another_session_of_a_tab_is_no_rise),
    T(test_a_dial_is_sampled_and_sent_at_once),
    T(test_the_apps_dials_come_with_it),
    T(test_a_session_elsewhere_is_watched_while_it_is_listed),
    T(test_a_row_carries_its_meta_and_board),
    T(test_the_timer_samples_every_interval),
    T(test_one_dashboard_is_opened),
    {0},
};
