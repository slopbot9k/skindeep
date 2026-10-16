/* The tabs of a launch (decision 0023) over fake sessions, as
 * test/tabs.test.js has them: the app's status, the 125 latch, the
 * active tab after a close, the order, successors and windows. Then
 * what app/main.js kept beside them: reopen and duplicate (decision
 * 0026), names (decision 0032), the strip's folders, pop out, and the
 * follow of decision 0030. */
#include "../../src/app/tabs.h"
#include "../test.h"

#include <stdlib.h>

typedef struct {
    int id, code;
} pending_end;

typedef struct world world;

/* A session that ends with the world's hangup code when hung up, or
 * when ends() is called, as a CLI exiting on its own. Its end reaches
 * the tabs at the next tick(), as a promise's would, or at once when
 * the world says sync. */
typedef struct {
    world *w;
    int id;
    bool exited;
    const char *session, *title, *screen;
    bool keeps_record;
} fake;

struct world {
    sk_tabs *tabs;
    int hup_code, makes, fail_from;
    bool sync;
    int hups[64];
    SK_VEC(pending_end) pending;
    sk_strv told, logged, notes, renamed;
    SK_VEC(int) actives, empties;
    sk_buf gone;     /* "id:code:closed:next " for each gone tab */
    sk_buf followed; /* "id:code " for each follow asked */
    int ended, n_ended;
    bool follow_first; /* follow tab 1 into /next */
    bool follow_all;   /* follow every tab into /next */
};

static void end_of(fake *f, int code)
{
    if (f->exited)
        return;
    f->exited = true;
    if (f->w->sync)
        sk_tabs_ended(f->w->tabs, f->id, code);
    else
        sk_vec_push(&f->w->pending, ((pending_end){f->id, code}));
}

static void fake_hangup(void *self)
{
    fake *f = self;
    f->w->hups[f->id]++;
    end_of(f, f->w->hup_code);
}

static const char *fake_session_id(void *self)
{
    return ((fake *)self)->session;
}

static const char *fake_title(void *self)
{
    return ((fake *)self)->title;
}

static bool fake_rename(void *self, const char *name)
{
    fake *f = self;
    sk_strv_push(&f->w->renamed, name);
    return f->keeps_record;
}

static const char *fake_screen(void *self)
{
    return ((fake *)self)->screen;
}

static void fake_free(void *self)
{
    free(self);
}

static const sk_tab_session_ops FAKE = {
    fake_hangup, NULL, NULL, fake_session_id, fake_title, fake_rename, fake_screen, fake_free,
};

static int make(void *ud, sk_tab *tab, const sk_tab_spec *spec, char **err)
{
    world *w = ud;
    if (w->fail_from && ++w->makes >= w->fail_from) {
        *err = sk_xstrdup("no");
        return -1;
    }
    sk_strv_push(&w->notes, spec->note ? spec->note : "");
    fake *f = sk_xcalloc(1, sizeof *f);
    f->w = w;
    f->id = tab->id;
    tab->session = (sk_tab_session){&FAKE, f};
    return 0;
}

static bool follow(void *ud, const sk_tab *tab, int code, sk_tab_spec *spec, sk_strv *cmd)
{
    world *w = ud;
    (void)cmd;
    sk_buf_printf(&w->followed, "%d:%d ", tab->id, code);
    if (!w->follow_all && !(w->follow_first && tab->id == 1))
        return false;
    spec->cwd = "/next";
    return true;
}

static void on_tell(void *ud, const char *msg)
{
    sk_strv_push(&((world *)ud)->told, msg);
}

static void on_log(void *ud, const char *msg)
{
    sk_strv_push(&((world *)ud)->logged, msg);
}

static void on_active(void *ud, sk_tab *tab)
{
    sk_vec_push(&((world *)ud)->actives, tab->id);
}

static void on_gone(void *ud, sk_tab *tab, int code, sk_tab *next)
{
    sk_buf_printf(&((world *)ud)->gone, "%d:%d:%d:%d ", tab->id, code, tab->closed, next ? next->id : 0);
}

static void on_empty(void *ud, int win)
{
    sk_vec_push(&((world *)ud)->empties, win);
}

static void on_ended(void *ud, int status)
{
    world *w = ud;
    w->ended = status;
    w->n_ended++;
}

static char *const WINDOW_CMD[] = {"claude", NULL};

static void setup(world *w, bool with_follow)
{
    memset(w, 0, sizeof *w);
    w->hup_code = 129;
    w->ended = -1;
    sk_tabs_config cfg = {
        .make = make,
        .follow = with_follow ? follow : NULL,
        .tell = on_tell,
        .log = on_log,
        .active = on_active,
        .gone = on_gone,
        .empty = on_empty,
        .ended = on_ended,
        .ud = w,
        .cmd = WINDOW_CMD,
        .file = "/usr/bin/claude",
        .home = "/home/u",
    };
    w->tabs = sk_tabs_new(&cfg);
}

static void teardown(world *w)
{
    sk_tabs_free(w->tabs);
    sk_vec_free(&w->pending);
    sk_strv_free(&w->told);
    sk_strv_free(&w->logged);
    sk_strv_free(&w->notes);
    sk_strv_free(&w->renamed);
    sk_vec_free(&w->actives);
    sk_vec_free(&w->empties);
    sk_buf_free(&w->gone);
    sk_buf_free(&w->followed);
}

/* Delivers the ends that are due, in the order they came. */
static void tick(world *w)
{
    for (size_t i = 0; i < w->pending.len; i++)
        sk_tabs_ended(w->tabs, w->pending.ptr[i].id, w->pending.ptr[i].code);
    w->pending.len = 0;
}

static sk_tab *add_cmd(world *w, char *const *cmd, const char *cwd, int win)
{
    sk_tab_spec spec = {SK_TAB_CLI, cmd, cwd, NULL, NULL, NULL, NULL, NULL};
    return sk_tabs_add(w->tabs, &spec, win, NULL);
}

static sk_tab *add(world *w, const char *cwd)
{
    return add_cmd(w, WINDOW_CMD, cwd, -1);
}

static fake *session_of(sk_tab *tab)
{
    return tab->session.self;
}

static void ends(sk_tab *tab, int code)
{
    end_of(session_of(tab), code);
}

/* The ids of the list, or of win's tabs when win >= 0, as "1,2,3". */
static char *ids(world *w, int win)
{
    sk_buf b = {0};
    sk_buf_append_c(&b, "");
    for (size_t i = 0; i < sk_tabs_count(w->tabs); i++) {
        sk_tab *t = sk_tabs_at(w->tabs, i);
        if (win < 0 || t->win == win)
            sk_buf_printf(&b, "%s%d", b.len ? "," : "", t->id);
    }
    return b.ptr;
}

#define CHECK_IDS(w, win, want)                                              \
    do {                                                                     \
        char *ids_ = ids(w, win);                                            \
        CHECK_STR(ids_, want);                                               \
        free(ids_);                                                          \
    } while (0)

static bool logged(world *w, const char *part)
{
    for (size_t i = 0; i < w->logged.len; i++)
        if (strstr(w->logged.ptr[i], part))
            return true;
    return false;
}

static char *joined(const sk_strv *v)
{
    sk_buf b = {0};
    sk_buf_append_c(&b, "");
    for (size_t i = 0; i < v->len; i++)
        sk_buf_printf(&b, "%s%s", i ? " " : "", v->ptr[i]);
    return b.ptr;
}

#define CHECK_CMD(tab, want)                                                 \
    do {                                                                     \
        char *c_ = joined(&(tab)->cmd);                                      \
        CHECK_STR(c_, want);                                                 \
        free(c_);                                                            \
    } while (0)

static void test_the_app_ends_with_the_last_tab_with_its_status(void)
{
    world w;
    setup(&w, false);
    sk_tab *a = add(&w, "/a");
    sk_tab *b = add(&w, "/b");
    ends(a, 0);
    tick(&w);
    CHECK_IDS(&w, -1, "2");
    CHECK(!sk_tabs_done(w.tabs));
    ends(b, 7);
    tick(&w);
    CHECK(sk_tabs_done(w.tabs));
    CHECK_INT(w.ended, 7);
    CHECK_INT(w.n_ended, 1);
    CHECK_INT(sk_tabs_exit_status(w.tabs), 7);
    teardown(&w);
}

static void test_the_first_tab_ending_last_gives_its_status_not_the_first_ended_one(void)
{
    world w;
    setup(&w, false);
    sk_tab *a = add(&w, "/a");
    sk_tab *b = add(&w, "/b");
    ends(b, 3);
    tick(&w);
    ends(a, 5);
    tick(&w);
    CHECK_INT(w.ended, 5);
    teardown(&w);
}

static void test_an_app_error_decides_125_over_the_last_tab_and_is_told_once(void)
{
    world w;
    setup(&w, false);
    sk_tab *a = add(&w, "/a");
    sk_tab *b = add(&w, "/b");
    int ida = a->id, idb = b->id;
    sk_tabs_fail(w.tabs, "boom");
    sk_tabs_fail(w.tabs, "again");
    tick(&w);
    CHECK_INT(w.ended, 125);
    CHECK_INT(w.told.len, 1);
    CHECK_STR(w.told.ptr[0], "internal error: boom");
    CHECK(logged(&w, "again"));
    CHECK_INT(w.hups[ida], 1);
    CHECK_INT(w.hups[idb], 1);
    teardown(&w);
}

static void test_an_app_error_after_the_last_tab_is_logged_the_status_stands(void)
{
    world w;
    setup(&w, false);
    ends(add(&w, "/a"), 4);
    tick(&w);
    CHECK_INT(w.ended, 4);
    sk_tabs_fail(w.tabs, "late");
    CHECK_INT(w.told.len, 0);
    CHECK(logged(&w, "late"));
    CHECK_INT(sk_tabs_exit_status(w.tabs), 4);
    teardown(&w);
}

static void test_closing_the_active_tab_activates_its_right_neighbour_else_its_left(void)
{
    world w;
    setup(&w, false);
    sk_tab *a = add(&w, "/a");
    sk_tab *b = add(&w, "/b");
    sk_tab *c = add(&w, "/c");
    CHECK(sk_tabs_active(w.tabs) == c);
    sk_tabs_activate(w.tabs, b->id);
    sk_tabs_close(w.tabs, b->id);
    tick(&w);
    CHECK(sk_tabs_active(w.tabs) == c);
    sk_tabs_close(w.tabs, c->id);
    tick(&w);
    CHECK(sk_tabs_active(w.tabs) == a);
    CHECK(!sk_tabs_done(w.tabs));
    teardown(&w);
}

static void test_closing_a_background_tab_keeps_the_active_one(void)
{
    world w;
    setup(&w, false);
    sk_tab *a = add(&w, "/a");
    sk_tab *b = add(&w, "/b");
    w.actives.len = 0;
    sk_tabs_close(w.tabs, a->id);
    tick(&w);
    CHECK(sk_tabs_active(w.tabs) == b);
    CHECK_INT(w.actives.len, 0);
    teardown(&w);
}

static void test_step_wraps_move_reorders_a_gone_tab_reports_its_code_and_whether_it_was_closed(void)
{
    world w;
    setup(&w, false);
    sk_tab *a = add(&w, "/a");
    sk_tab *b = add(&w, "/b");
    sk_tab *c = add(&w, "/c");
    sk_tabs_step(w.tabs, 1, -1);
    CHECK(sk_tabs_active(w.tabs) == a);
    sk_tabs_step(w.tabs, -1, -1);
    CHECK(sk_tabs_active(w.tabs) == c);
    sk_tabs_move(w.tabs, c->id, 0);
    CHECK_IDS(&w, -1, "3,1,2");
    sk_tabs_move(w.tabs, c->id, 99);
    CHECK_IDS(&w, -1, "1,2,3");
    ends(b, 2);
    sk_tabs_close(w.tabs, a->id);
    tick(&w);
    CHECK_STR(sk_buf_cstr(&w.gone), "2:2:0:0 1:129:1:0 ");
    teardown(&w);
}

static void test_no_tab_is_added_once_the_app_is_ending(void)
{
    world w;
    setup(&w, false);
    add(&w, "/a");
    sk_tabs_hangup_all(w.tabs);
    CHECK(add(&w, "/b") == NULL);
    tick(&w);
    CHECK_INT(w.ended, 129);
    teardown(&w);
}

static void test_a_monitor_tab_ending_last_ends_the_app_with_the_last_cli_tabs_status(void)
{
    world w;
    setup(&w, false);
    w.hup_code = 0;
    sk_tab *a = add(&w, "/a");
    char *const watched[] = {"claude", NULL};
    sk_tab_spec spec = {SK_TAB_MONITOR, watched, "/w", NULL, NULL, "claude [?]", NULL, NULL};
    sk_tab *m = sk_tabs_add(w.tabs, &spec, -1, NULL);
    CHECK_STR(m->label, "claude [?]");
    CHECK(m->file == NULL);
    ends(a, 4);
    tick(&w);
    CHECK(!sk_tabs_done(w.tabs));
    ends(m, 0);
    tick(&w);
    CHECK_INT(w.ended, 4);
    teardown(&w);
}

static void test_a_successor_takes_the_place_and_the_focus_of_a_tab_that_ended_by_itself_and_the_app_goes_on(void)
{
    world w;
    setup(&w, true);
    w.follow_first = true;
    sk_tab *a = add(&w, "/a");
    sk_tab *b = add(&w, "/b");
    sk_tabs_activate(w.tabs, a->id);
    ends(a, 0);
    tick(&w);
    CHECK_IDS(&w, -1, "3,2");
    CHECK_STR(sk_tabs_at(w.tabs, 0)->cwd, "/next");
    CHECK_STR(sk_tabs_at(w.tabs, 1)->cwd, "/b");
    CHECK_INT(sk_tabs_active(w.tabs)->id, 3);
    CHECK_STR(sk_buf_cstr(&w.gone), "1:0:0:3 ");
    CHECK(!sk_tabs_done(w.tabs));
    sk_tabs_close(w.tabs, b->id);
    tick(&w);
    ends(sk_tabs_at(w.tabs, 0), 4);
    tick(&w);
    CHECK_INT(w.ended, 4);
    CHECK_INT(sk_tabs_last(w.tabs)->id, 3);
    CHECK_STR(sk_buf_cstr(&w.followed), "1:0 3:4 "); /* a closed tab is never followed */
    teardown(&w);
}

static void test_a_successor_whose_make_fails_is_logged_and_the_tab_just_ends(void)
{
    world w;
    setup(&w, true);
    w.follow_all = true;
    w.fail_from = 2;
    ends(add(&w, "/a"), 0);
    tick(&w);
    CHECK_INT(w.ended, 0);
    CHECK(logged(&w, "follow: no"));
    teardown(&w);
}

/* Tabs in several windows of one launch. */
static void test_a_detached_tab_is_active_in_its_new_window_and_its_old_window_activates_a_neighbour(void)
{
    world w;
    setup(&w, false);
    sk_tab *a = add(&w, "/a");
    sk_tab *b = add(&w, "/b");
    sk_tab *c = add(&w, "/c");
    (void)a;
    sk_tabs_activate(w.tabs, b->id);
    sk_tabs_detach(w.tabs, b->id, 1);
    CHECK_INT(b->win, 1);
    CHECK(sk_tabs_active_of(w.tabs, 1) == b);
    CHECK(sk_tabs_active_of(w.tabs, 0) == c);
    CHECK_IDS(&w, 0, "1,3");
    int wins[4];
    CHECK_INT(sk_tabs_wins(w.tabs, wins, 4), 2);
    CHECK(wins[0] == 0 && wins[1] == 1);
    CHECK_INT(w.empties.len, 0);
    teardown(&w);
}

static void test_step_and_move_stay_inside_a_window_and_a_window_that_loses_its_last_tab_is_empty(void)
{
    world w;
    setup(&w, false);
    sk_tab *a = add(&w, "/a");
    sk_tab *b = add(&w, "/b");
    sk_tab *c = add_cmd(&w, WINDOW_CMD, "/c", 1);
    sk_tab *d = add_cmd(&w, WINDOW_CMD, "/d", 1);
    sk_tabs_set_current(w.tabs, 0);
    sk_tabs_activate(w.tabs, a->id);
    sk_tabs_step(w.tabs, 1, -1);
    CHECK(sk_tabs_active_of(w.tabs, 0) == b);
    sk_tabs_step(w.tabs, 1, -1);
    CHECK(sk_tabs_active_of(w.tabs, 0) == a);
    sk_tabs_move(w.tabs, d->id, 0);
    CHECK_IDS(&w, 1, "4,3");
    CHECK_IDS(&w, 0, "1,2");
    ends(c, 0);
    tick(&w);
    CHECK(sk_tabs_active_of(w.tabs, 1) == d);
    ends(d, 0);
    tick(&w);
    CHECK_INT(w.empties.len, 1);
    CHECK_INT(w.empties.ptr[0], 1);
    CHECK(!sk_tabs_done(w.tabs));
    sk_tabs_close_win(w.tabs, 0);
    tick(&w);
    CHECK_INT(w.ended, 129);
    teardown(&w);
}

/* A session may report its end from inside hangup: nothing it held is
 * freed under it, and the walk over the tabs stays whole. */
static void test_a_session_that_ends_inside_hangup(void)
{
    world w;
    setup(&w, false);
    w.sync = true;
    sk_tab *a = add(&w, "/a");
    add(&w, "/b");
    add_cmd(&w, WINDOW_CMD, "/c", 1);
    sk_tabs_close(w.tabs, a->id);
    CHECK_IDS(&w, -1, "2,3");
    sk_tabs_fail(w.tabs, "boom");
    CHECK(sk_tabs_done(w.tabs));
    CHECK_INT(w.ended, 125);
    CHECK_INT(w.hups[2] + w.hups[3], 2);
    teardown(&w);
}

static void test_reopen_resumes_the_newest_closed_tabs_session(void)
{
    world w;
    setup(&w, false);
    sk_envp env = {0};
    sk_envp_set(&env, "CLAUDE_CONFIG_DIR", "/home/u/.claude-work");
    char *const cmd[] = {"claude", "fix the bug", NULL};
    sk_tab_spec spec = {SK_TAB_CLI, cmd, "/r", &env, "/opt/claude", NULL, NULL, NULL};
    sk_tab *a = sk_tabs_add(w.tabs, &spec, -1, NULL);
    CHECK_STR(a->label, "claude [work]");
    session_of(a)->session = "s-1";
    sk_tab *b = add(&w, "/b");
    sk_tabs_menu menu;
    sk_tabs_menu_state(w.tabs, &menu);
    CHECK(!menu.reopen && menu.duplicate && menu.duplicate_fresh);
    sk_tabs_close(w.tabs, a->id);
    tick(&w);
    ends(b, 1); /* exited by itself: remembered too, once reaped */
    sk_tabs_menu_state(w.tabs, &menu);
    CHECK(menu.reopen && !menu.reopen_fresh);
    add(&w, "/c");
    tick(&w);
    sk_tabs_menu_state(w.tabs, &menu);
    CHECK(menu.reopen && menu.reopen_fresh); /* b's, which had no session */
    sk_tab *rb = sk_tabs_reopen(w.tabs, NULL);
    CHECK_CMD(rb, "claude");
    CHECK_STR(rb->cwd, "/b");
    sk_tabs_menu_state(w.tabs, &menu);
    CHECK(menu.reopen && !menu.reopen_fresh);
    sk_tab *ra = sk_tabs_reopen(w.tabs, NULL);
    CHECK_CMD(ra, "claude --resume s-1");
    CHECK_STR(ra->cwd, "/r");
    CHECK_STR(ra->file, "/opt/claude");
    CHECK_STR(sk_envp_get(&ra->env, "CLAUDE_CONFIG_DIR"), "/home/u/.claude-work");
    CHECK_STR(ra->label, "claude [work]");
    CHECK(sk_tabs_active(w.tabs) == ra);
    CHECK(sk_tabs_reopen(w.tabs, NULL) == NULL);
    sk_tabs_menu_state(w.tabs, &menu);
    CHECK(!menu.reopen);
    sk_envp_free(&env);
    teardown(&w);
}

static void test_a_session_another_tab_runs_is_made_active_not_reopened(void)
{
    world w;
    setup(&w, false);
    sk_tab *a = add(&w, "/a");
    session_of(a)->session = "s-1";
    sk_tab *b = add(&w, "/b");
    session_of(b)->session = "s-1";
    sk_tab *c = add(&w, "/c");
    sk_tabs_close(w.tabs, a->id);
    tick(&w);
    CHECK(sk_tabs_active(w.tabs) == c);
    CHECK(sk_tabs_reopen(w.tabs, NULL) == b);
    CHECK(sk_tabs_active(w.tabs) == b);
    CHECK_INT(sk_tabs_count(w.tabs), 2);
    teardown(&w);
}

static void test_reopen_and_duplicate_speak_each_agents_resume(void)
{
    world w;
    setup(&w, false);
    char *const codex[] = {"codex", NULL};
    char *const bash[] = {"/bin/bash", "-l", NULL};
    sk_tab *x = add_cmd(&w, codex, "/x", -1);
    session_of(x)->session = "t-9";
    sk_tab *y = add_cmd(&w, bash, "/y", -1);
    session_of(y)->session = "ignored";
    sk_tabs_close(w.tabs, x->id);
    sk_tabs_close(w.tabs, y->id);
    add(&w, "/keep");
    tick(&w);
    CHECK_CMD(sk_tabs_reopen(w.tabs, NULL), "/bin/bash");
    sk_tab *rx = sk_tabs_reopen(w.tabs, NULL);
    CHECK_CMD(rx, "codex resume t-9");
    session_of(rx)->session = "t-9";
    sk_tab *fx = sk_tabs_duplicate(w.tabs, NULL);
    CHECK_CMD(fx, "codex fork t-9");
    teardown(&w);
}

static void test_a_reopen_that_fails_keeps_its_entry(void)
{
    world w;
    setup(&w, false);
    sk_tab *a = add(&w, "/a");
    add(&w, "/b");
    sk_tabs_close(w.tabs, a->id);
    tick(&w);
    w.fail_from = 1;
    char *err = NULL;
    CHECK(sk_tabs_reopen(w.tabs, &err) == NULL);
    CHECK_STR(err, "no");
    free(err);
    CHECK(logged(&w, "reopen: no"));
    sk_tabs_menu menu;
    sk_tabs_menu_state(w.tabs, &menu);
    CHECK(menu.reopen);
    w.fail_from = 0;
    CHECK(sk_tabs_reopen(w.tabs, NULL) != NULL);
    teardown(&w);
}

static void test_tabs_hung_up_as_the_app_ends_are_not_remembered(void)
{
    world w;
    setup(&w, false);
    add(&w, "/a");
    add(&w, "/b");
    sk_tabs_hangup_all(w.tabs);
    tick(&w);
    sk_tabs_menu menu;
    sk_tabs_menu_state(w.tabs, &menu);
    CHECK(!menu.reopen);
    teardown(&w);
}

static void test_duplicate_forks_the_active_tabs_session_beside_it(void)
{
    world w;
    setup(&w, false);
    sk_tab *a = add(&w, "/a");
    session_of(a)->session = "s-1";
    add(&w, "/b");
    sk_tabs_activate(w.tabs, a->id);
    sk_tabs_menu menu;
    sk_tabs_menu_state(w.tabs, &menu);
    CHECK(menu.duplicate && !menu.duplicate_fresh);
    sk_tab *d = sk_tabs_duplicate(w.tabs, NULL);
    CHECK_CMD(d, "claude --resume s-1 --fork-session");
    CHECK_STR(d->cwd, "/a");
    CHECK_IDS(&w, -1, "1,3,2");
    CHECK(sk_tabs_active(w.tabs) == d);
    char *const watched[] = {"claude", NULL};
    sk_tab_spec spec = {SK_TAB_DASHBOARD, watched, "/", NULL, NULL, "sessions", NULL, NULL};
    sk_tabs_add(w.tabs, &spec, -1, NULL);
    sk_tabs_menu_state(w.tabs, &menu);
    CHECK(!menu.duplicate);
    CHECK(sk_tabs_duplicate(w.tabs, NULL) == NULL); /* no CLI to fork */
    teardown(&w);
}

static void test_duplicate_places_the_copy_in_its_own_windows_strip(void)
{
    world w;
    setup(&w, false);
    add_cmd(&w, WINDOW_CMD, "/x", 1);
    sk_tab *a = add(&w, "/a");
    add(&w, "/b");
    sk_tabs_activate(w.tabs, a->id);
    sk_tab *d = sk_tabs_duplicate(w.tabs, NULL);
    CHECK_INT(d->win, 0);
    CHECK_IDS(&w, 0, "2,4,3");
    teardown(&w);
}

static void test_new_tab_carries_the_windows_command_and_names_what_it_dropped(void)
{
    world w;
    setup(&w, false);
    sk_tab *t = sk_tabs_new_tab(w.tabs, "/live", NULL);
    CHECK_CMD(t, "claude");
    CHECK_STR(t->cwd, "/live");
    CHECK_STR(t->file, "/usr/bin/claude");
    CHECK_STR(w.notes.ptr[0], "");
    teardown(&w);

    char *const long_cmd[] = {"claude", "fix the bug in the parser",
                              "and then the other bug in the lexer please", NULL};
    setup(&w, false);
    sk_tabs_free(w.tabs);
    sk_tabs_config cfg = {.make = make, .ud = &w, .cmd = long_cmd, .home = "/home/u"};
    w.tabs = sk_tabs_new(&cfg);
    t = sk_tabs_new_tab(w.tabs, "/live", NULL);
    CHECK_CMD(t, "claude");
    CHECK_STR(w.notes.ptr[0],
              "not carried: 'fix the bug in the parser' 'and then the other bug in th...");
    teardown(&w);
}

static void test_the_name_is_the_newer_of_the_record_and_a_rename(void)
{
    world w;
    setup(&w, false);
    sk_tab *a = add(&w, "/a");
    fake *f = session_of(a);
    CHECK_STR(sk_tab_name(a), "claude");
    f->title = "Sort the photos";
    CHECK_STR(sk_tab_name(a), "Sort the photos");
    CHECK(sk_tabs_rename(w.tabs, a->id, "  Mine\t"));
    CHECK_STR(sk_tab_name(a), "Mine"); /* given after the record */
    CHECK(logged(&w, "tab 1: renamed \"Mine\""));
    CHECK(!logged(&w, "in the CLI's record"));
    f->title = "Later";
    CHECK_STR(sk_tab_name(a), "Later"); /* a /rename made later wins */
    f->title = "";
    CHECK_STR(sk_tab_name(a), "Mine");
    f->keeps_record = true;
    CHECK(sk_tabs_rename(w.tabs, a->id, "Kept"));
    CHECK(logged(&w, "tab 1: renamed \"Kept\" in the CLI's record"));
    CHECK_STR(w.renamed.ptr[w.renamed.len - 1], "Kept");
    CHECK(!sk_tabs_rename(w.tabs, a->id, " \xc2\xa0 "));
    CHECK(!sk_tabs_rename(w.tabs, 99, "x"));
    CHECK_STR(sk_tab_name(a), "Kept");
    teardown(&w);
}

static void test_a_name_is_cut_at_120_characters(void)
{
    world w;
    setup(&w, false);
    sk_tab *a = add(&w, "/a");
    sk_buf name = {0};
    for (int i = 0; i < 130; i++)
        sk_buf_append_c(&name, "\xc3\xa9");
    CHECK(sk_tabs_rename(w.tabs, a->id, name.ptr));
    CHECK_INT(strlen(a->named), 240);
    sk_buf_clear(&name);
    for (int i = 0; i < 119; i++)
        sk_buf_putc(&name, 'x');
    sk_buf_append_c(&name, "\xf0\x9f\x98\x80"); /* the 120th unit is half of it */
    CHECK(sk_tabs_rename(w.tabs, a->id, name.ptr));
    CHECK_INT(strlen(a->named), 122);
    CHECK(!strcmp(a->named + 119, "\xef\xbf\xbd"));
    sk_buf_free(&name);
    teardown(&w);
}

static void test_tabs_of_one_name_add_their_folders(void)
{
    world w;
    setup(&w, false);
    sk_tab *a = add(&w, "/home/u/a");
    sk_tab *b = add(&w, "/");
    char *const bash[] = {"bash", NULL};
    sk_tab *c = add_cmd(&w, bash, "/c", -1);
    sk_tab *d = add_cmd(&w, WINDOW_CMD, "/d", 1);
    sk_buf out = {0};
    CHECK(sk_tabs_folder(w.tabs, a, &out));
    CHECK_STR(sk_buf_cstr(&out), "a");
    sk_buf_clear(&out);
    CHECK(sk_tabs_folder(w.tabs, b, &out));
    CHECK_STR(sk_buf_cstr(&out), "/");
    sk_buf_clear(&out);
    CHECK(!sk_tabs_folder(w.tabs, c, &out));
    CHECK(!sk_tabs_folder(w.tabs, d, &out)); /* alone in its window */
    session_of(b)->title = "other";
    CHECK(!sk_tabs_folder(w.tabs, a, &out));
    sk_buf_free(&out);
    teardown(&w);
}

static void test_output_marks_a_background_tab_unseen_once(void)
{
    world w;
    setup(&w, false);
    sk_tab *a = add(&w, "/a");
    sk_tab *b = add(&w, "/b");
    CHECK(!sk_tabs_output(w.tabs, b));
    CHECK(sk_tabs_output(w.tabs, a));
    CHECK(a->unseen);
    CHECK(!sk_tabs_output(w.tabs, a));
    sk_tabs_activate(w.tabs, a->id);
    CHECK(!a->unseen);
    teardown(&w);
}

static void test_a_tab_pops_out_into_a_new_window(void)
{
    world w;
    setup(&w, false);
    sk_tab *a = add(&w, "/a");
    sk_tab *b = add(&w, "/b");
    CHECK(!sk_tabs_pop_begin(w.tabs, a->id, 1)); /* not that window's */
    CHECK(sk_tabs_pop_begin(w.tabs, b->id, 0));
    CHECK(b->moving);
    CHECK(!sk_tabs_pop_begin(w.tabs, b->id, 0)); /* moving already */
    CHECK(!sk_tabs_output(w.tabs, b));
    int win = sk_tabs_pop_end(w.tabs, b->id);
    CHECK_INT(win, 1);
    CHECK_INT(b->win, 1);
    CHECK_INT(sk_tabs_current(w.tabs), 1);
    CHECK(sk_tabs_active(w.tabs) == b);
    CHECK(sk_tabs_active_of(w.tabs, 0) == a);
    sk_tabs_pop_done(w.tabs, b->id);
    CHECK(!b->moving);
    CHECK(!sk_tabs_pop_begin(w.tabs, a->id, 0)); /* a window of one tab drags nothing out */
    sk_tabs_touch(w.tabs, 0);
    CHECK_INT(sk_tabs_current(w.tabs), 0);
    sk_tabs_touch(w.tabs, 7);
    CHECK_INT(sk_tabs_current(w.tabs), 0);
    int c = add(&w, "/c")->id;
    CHECK(sk_tabs_pop_begin(w.tabs, c, 0));
    sk_tabs_close(w.tabs, c);
    tick(&w);
    CHECK_INT(sk_tabs_pop_end(w.tabs, c), -1); /* it ended meanwhile */
    teardown(&w);
}

static void test_a_window_emptied_while_current_gives_way_to_another(void)
{
    world w;
    setup(&w, false);
    add(&w, "/a");
    sk_tab *b = add_cmd(&w, WINDOW_CMD, "/b", 1);
    sk_tabs_set_current(w.tabs, 1);
    sk_tabs_close(w.tabs, b->id);
    tick(&w);
    CHECK_INT(w.empties.ptr[0], 1);
    CHECK_INT(sk_tabs_current(w.tabs), 0);
    teardown(&w);
}

#define REFUSAL                                                              \
    "\"Sort\" is running in the background (5e3a91c0). Run\n"                \
    "`claude attach 5e3a91c0` to open it, or `claude stop 5e3a91c0` first"

static void test_a_refused_background_session_is_opened_in_the_tabs_place(void)
{
    world w;
    setup(&w, false);
    sk_tabs_free(w.tabs);
    sk_tabs_config cfg = {.make = make, .follow_background = true, .log = on_log, .gone = on_gone,
                          .ended = on_ended, .ud = &w, .cmd = WINDOW_CMD, .home = "/home/u"};
    w.tabs = sk_tabs_new(&cfg);
    char *const resume[] = {"claude", "--resume", NULL};
    char *const own[] = {"claude", "--resume", "--model", "opus", NULL};
    char *const bash[] = {"bash", NULL};
    sk_tab *a = add_cmd(&w, resume, "/a", -1);
    sk_tab *b = add_cmd(&w, own, "/b", -1);
    sk_tab *c = add_cmd(&w, bash, "/c", -1);
    session_of(a)->screen = REFUSAL;
    session_of(b)->screen = REFUSAL;
    session_of(c)->screen = REFUSAL;
    ends(a, 1);
    ends(b, 1);
    ends(c, 1);
    tick(&w);
    CHECK_STR(sk_buf_cstr(&w.gone), "1:1:0:4 2:1:0:0 3:1:0:0 ");
    sk_tab *next = sk_tabs_get(w.tabs, 4);
    CHECK_CMD(next, "claude attach 5e3a91c0");
    CHECK_STR(next->cwd, "/a");
    CHECK(logged(&w, "tab 1: claude refused to open a background session in place, "
                     "opening it with: claude attach 5e3a91c0"));
    teardown(&w);
}

static void test_the_last_screen_is_echoed_only_for_a_cli_that_ended_by_itself(void)
{
    world w;
    setup(&w, false);
    sk_tab *a = add(&w, "/a");
    session_of(a)->screen = "bye\n";
    ends(a, 0);
    tick(&w);
    CHECK_STR(sk_tabs_last_screen(w.tabs), "bye\n");
    teardown(&w);

    setup(&w, false);
    a = add(&w, "/a");
    session_of(a)->screen = "bye\n";
    sk_tabs_close(w.tabs, a->id);
    tick(&w);
    CHECK(sk_tabs_last_screen(w.tabs) == NULL);
    teardown(&w);

    setup(&w, false);
    a = add(&w, "/a");
    session_of(a)->screen = "bye\n";
    sk_tabs_fail(w.tabs, "x");
    tick(&w);
    CHECK(sk_tabs_last_screen(w.tabs) == NULL);
    teardown(&w);
}

static void test_a_make_that_fails_reaches_the_caller(void)
{
    world w;
    setup(&w, false);
    w.fail_from = 1;
    char *err = NULL;
    sk_tab_spec spec = {SK_TAB_CLI, WINDOW_CMD, "/a", NULL, NULL, NULL, NULL, NULL};
    CHECK(sk_tabs_add(w.tabs, &spec, -1, &err) == NULL);
    CHECK_STR(err, "no");
    free(err);
    CHECK_INT(sk_tabs_count(w.tabs), 0);
    w.fail_from = 0;
    CHECK_INT(add(&w, "/b")->id, 2); /* the failed one took its id */
    teardown(&w);
}

const sk_test sk_suite_tabs[] = {
    T(test_the_app_ends_with_the_last_tab_with_its_status),
    T(test_the_first_tab_ending_last_gives_its_status_not_the_first_ended_one),
    T(test_an_app_error_decides_125_over_the_last_tab_and_is_told_once),
    T(test_an_app_error_after_the_last_tab_is_logged_the_status_stands),
    T(test_closing_the_active_tab_activates_its_right_neighbour_else_its_left),
    T(test_closing_a_background_tab_keeps_the_active_one),
    T(test_step_wraps_move_reorders_a_gone_tab_reports_its_code_and_whether_it_was_closed),
    T(test_no_tab_is_added_once_the_app_is_ending),
    T(test_a_monitor_tab_ending_last_ends_the_app_with_the_last_cli_tabs_status),
    T(test_a_successor_takes_the_place_and_the_focus_of_a_tab_that_ended_by_itself_and_the_app_goes_on),
    T(test_a_successor_whose_make_fails_is_logged_and_the_tab_just_ends),
    T(test_a_detached_tab_is_active_in_its_new_window_and_its_old_window_activates_a_neighbour),
    T(test_step_and_move_stay_inside_a_window_and_a_window_that_loses_its_last_tab_is_empty),
    T(test_a_session_that_ends_inside_hangup),
    T(test_reopen_resumes_the_newest_closed_tabs_session),
    T(test_a_session_another_tab_runs_is_made_active_not_reopened),
    T(test_reopen_and_duplicate_speak_each_agents_resume),
    T(test_a_reopen_that_fails_keeps_its_entry),
    T(test_tabs_hung_up_as_the_app_ends_are_not_remembered),
    T(test_duplicate_forks_the_active_tabs_session_beside_it),
    T(test_duplicate_places_the_copy_in_its_own_windows_strip),
    T(test_new_tab_carries_the_windows_command_and_names_what_it_dropped),
    T(test_the_name_is_the_newer_of_the_record_and_a_rename),
    T(test_a_name_is_cut_at_120_characters),
    T(test_tabs_of_one_name_add_their_folders),
    T(test_output_marks_a_background_tab_unseen_once),
    T(test_a_tab_pops_out_into_a_new_window),
    T(test_a_window_emptied_while_current_gives_way_to_another),
    T(test_a_refused_background_session_is_opened_in_the_tabs_place),
    T(test_the_last_screen_is_echoed_only_for_a_cli_that_ended_by_itself),
    T(test_a_make_that_fails_reaches_the_caller),
    {0},
};
