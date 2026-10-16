/* The sessions the app did not start (decision 0027), as app/main.js
 * lists, labels, watches and opens them, on a fake scan, fake ancestry,
 * fake liveness and fake channels. A tab's CLI is a real sh, so the tabs
 * know its pid. */
#include "../../src/app/foreign.h"
#include "../../src/app/clitab.h"
#include "../../src/app/monitor.h"
#include "../../src/base/paths.h"
#include "../../src/core/command.h"
#include "../test.h"
#include "fakes.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

extern char **environ;

typedef struct {
    pid_t pid;
    pid_t up[4];
    size_t n;
} ancestry;

typedef struct {
    pid_t pid;
    const char *file, *session, *profile, *home;
    double mtime;
} channel_of;

typedef struct {
    sk_loop *loop;
    sk_tabs *tabs;
    sk_foreign *foreign;
    sk_foreign_config fc;
    sk_clitab_config cli;
    sk_monitor_config mon;
    sk_envp env;
    char *sh, *home;
    sk_live_sessions live;
    ancestry anc[8];
    size_t n_anc;
    channel_of ch[8];
    size_t n_ch;
    pid_t gone[8];
    size_t n_gone;
    sk_strv asked; /* each channel asked: "<pid> <profile>,<profile>..." */
} world;

static double fake_now = 1700000100000.0;

static double now(void)
{
    return fake_now;
}

static void fake_scan(void *ud, sk_live_sessions *out)
{
    world *w = ud;
    for (size_t i = 0; i < w->live.len; i++) {
        const sk_live_session *s = &w->live.ptr[i];
        sk_live_session c = {s->pid, s->ppid, s->kind, s->started, sk_xstrdup(s->cwd), {0}};
        sk_strv_extend(&c.argv, s->argv.ptr);
        sk_vec_push(out, c);
    }
}

static size_t fake_ancestors(void *ud, pid_t pid, pid_t *out, size_t max)
{
    world *w = ud;
    for (size_t i = 0; i < w->n_anc; i++) {
        if (w->anc[i].pid != pid)
            continue;
        size_t n = SK_MIN(max, w->anc[i].n);
        memcpy(out, w->anc[i].up, n * sizeof *out);
        return n;
    }
    return 0;
}

static bool fake_same(void *ud, pid_t pid, double started)
{
    (void)started;
    world *w = ud;
    for (size_t i = 0; i < w->n_gone; i++)
        if (w->gone[i] == pid)
            return false;
    return true;
}

static bool fake_channel(void *ud, const sk_live_session *s, char *const *profiles, sk_session_channel *out)
{
    world *w = ud;
    sk_buf line = {0};
    sk_buf_printf(&line, "%ld ", (long)s->pid);
    for (size_t i = 0; profiles[i]; i++)
        sk_buf_printf(&line, "%s%s", i ? "," : "", profiles[i]);
    sk_strv_push(&w->asked, line.ptr);
    sk_buf_free(&line);
    for (size_t i = 0; i < w->n_ch; i++) {
        const channel_of *c = &w->ch[i];
        if (c->pid != s->pid)
            continue;
        out->file = c->file ? sk_xstrdup(c->file) : NULL;
        out->session = c->session ? sk_xstrdup(c->session) : NULL;
        out->profile = c->profile ? sk_xstrdup(c->profile) : NULL;
        out->home = c->home ? sk_xstrdup(c->home) : NULL;
        out->mtime = c->mtime;
        return true;
    }
    return false;
}

static sk_agent *watcher(void *ud, const sk_foreign_session *s, sk_bus *bus)
{
    return sk_foreign_watch(((world *)ud)->foreign, s, bus);
}

static int make_tab(void *ud, sk_tab *tab, const sk_tab_spec *spec, char **err)
{
    world *w = ud;
    if (spec->kind == SK_TAB_MONITOR)
        return sk_monitor_make(&w->mon, tab, spec, err);
    return sk_clitab_make(&w->cli, tab, spec, err);
}

static void quiet(void *ud, const char *msg)
{
    (void)ud, (void)msg;
}

/* <tmp>/name, made. */
static char *dir_in(const char *parent, const char *name)
{
    char *d = sk_path_join(parent, name);
    mkdir(d, 0700);
    return d;
}

static void touch(const char *dir, const char *name)
{
    char *p = sk_path_join(dir, name);
    FILE *f = fopen(p, "w");
    CHECK(f != NULL);
    if (f)
        fclose(f);
    free(p);
}

static void world_init(world *w)
{
    memset(w, 0, sizeof *w);
    fake_now = 1700000100000.0;
    w->loop = sk_loop_new();
    w->home = dir_in(sk_test_tmpdir(), "home");
    char *run = dir_in(sk_test_tmpdir(), "run");
    sk_envp_from(&w->env, environ);
    w->sh = sk_resolve_command("sh", sk_envp_get(&w->env, "PATH"));
    w->cli.loop = w->loop;
    w->cli.run_dir = run;
    w->cli.root = sk_test_tmpdir();
    w->cli.store = sk_test_tmpdir();
    w->cli.log = quiet;
    w->cli.tell = quiet;
    w->cli.ud = w;
    w->mon.loop = w->loop;
    w->mon.home = w->home;
    w->mon.watcher = watcher;
    w->mon.same = fake_same;
    w->mon.log = quiet;
    w->mon.ud = w;
    static char *const window_cmd[] = {"sh", NULL};
    sk_tabs_config tc;
    memset(&tc, 0, sizeof tc);
    tc.make = make_tab;
    tc.tell = quiet;
    tc.log = quiet;
    tc.ud = w;
    tc.cmd = window_cmd;
    tc.file = w->sh;
    tc.env = &w->env;
    tc.home = w->home;
    w->tabs = sk_tabs_new(&tc);
    w->cli.tabs = w->mon.tabs = w->tabs;
    w->fc.tabs = w->tabs;
    w->fc.home = w->home;
    w->fc.scan = fake_scan;
    w->fc.ancestors = fake_ancestors;
    w->fc.same = fake_same;
    w->fc.channel = fake_channel;
    w->fc.now = now;
    w->fc.log = quiet;
    w->fc.ud = w;
    w->foreign = sk_foreign_new(&w->fc);
}

static void world_free(world *w)
{
    sk_tabs_free(w->tabs);
    sk_foreign_free(w->foreign);
    sk_loop_free(w->loop);
    sk_sessions_free(&w->live);
    sk_envp_free(&w->env);
    sk_strv_free(&w->asked);
    free((char *)w->cli.run_dir);
    free(w->home);
    free(w->sh);
}

static void live(world *w, pid_t pid, sk_profile_agent kind, double started, const char *cwd,
                 char *const *argv)
{
    sk_live_session s = {pid, 1, kind, started, sk_xstrdup(cwd), {0}};
    sk_strv_extend(&s.argv, argv);
    sk_vec_push(&w->live, s);
}

/* A CLI tab running sh, started, so its pid is known. */
static sk_tab *cli_tab(world *w)
{
    char *cmd[] = {"sh", "-c", "sleep 30", NULL};
    sk_tab_spec spec = {SK_TAB_CLI, cmd, sk_test_tmpdir(), NULL, NULL, NULL, NULL, NULL};
    sk_tab *tab = sk_tabs_add(w->tabs, &spec, -1, NULL);
    CHECK(tab != NULL);
    if (tab)
        sk_session_start(sk_clitab_session(tab), 80, 24);
    return tab;
}

static pid_t pid_of(const sk_tab *tab)
{
    return tab->session.ops->pid(tab->session.self);
}

static char *rows_of(const sk_foreign_entries *l)
{
    sk_buf out = {0};
    sk_jw jw;
    sk_jw_init(&jw, &out, 0);
    sk_foreign_rows(l, &jw);
    return out.ptr;
}

static void test_keys_and_labels(void)
{
    sk_buf b = {0};
    sk_foreign_key(&b, 4242, 1700000000499.5);
    CHECK_STR(b.ptr, "4242:1700000000500");
    sk_buf_clear(&b);
    sk_foreign_key(&b, 7, 1700000000499.4);
    CHECK_STR(b.ptr, "7:1700000000499");
    sk_buf_clear(&b);
    sk_foreign_label(&b, SK_PROFILE_CLAUDE, NULL, "/home/u");
    CHECK_STR(b.ptr, "claude [?]");
    sk_buf_clear(&b);
    sk_session_channel ch = {0};
    sk_foreign_label(&b, SK_PROFILE_CODEX, &ch, "/home/u");
    CHECK_STR(b.ptr, "codex [?]"); /* found, its profile not told */
    sk_buf_clear(&b);
    ch.profile = "/home/u/.claude-work";
    sk_foreign_label(&b, SK_PROFILE_CLAUDE, &ch, "/home/u");
    CHECK_STR(b.ptr, "claude [work]");
    sk_buf_clear(&b);
    ch.profile = "/home/u/.claude";
    sk_foreign_label(&b, SK_PROFILE_CLAUDE, &ch, "/home/u");
    CHECK_STR(b.ptr, "claude");
    sk_buf_clear(&b);
    ch.profile = "/home/u/profiles/alt/.codex";
    sk_foreign_label(&b, SK_PROFILE_CODEX, &ch, "/home/u");
    CHECK_STR(b.ptr, "codex [alt]");
    sk_buf_free(&b);
}

/* knownProfiles: the default, the app's, the open CLI tabs' of that
 * agent, those recorded and those the home scan found, each once. */
static void test_the_known_profiles(void)
{
    world w;
    world_init(&w);
    sk_envp_set(&w.env, "CLAUDE_CONFIG_DIR", "/x/app");
    sk_envp_set(&w.env, "CODEX_HOME", NULL);
    char *records = sk_path_join(sk_test_tmpdir(), "records");
    w.fc.env = &w.env;
    w.fc.profiles_dir = records;
    sk_foreign_free(w.foreign);
    w.foreign = sk_foreign_new(&w.fc);
    CHECK(sk_profiles_record(records, SK_PROFILE_CLAUDE, "/x/rec", 1000) == 0);
    CHECK(sk_profiles_record(records, SK_PROFILE_CODEX, "/x/codex", 1000) == 0);
    CHECK(sk_profiles_record(records, SK_PROFILE_CLAUDE, "/x/app", 900) == 0);
    char *work = dir_in(w.home, ".claude-work");
    touch(work, ".credentials.json");
    char *cmd[] = {"claude", NULL};
    sk_envp tab_env = {0};
    sk_envp_set(&tab_env, "CLAUDE_CONFIG_DIR", "/x/tab");
    sk_tab_spec spec = {SK_TAB_CLI, cmd, "/", &tab_env, NULL, NULL, NULL, NULL};
    CHECK(sk_tabs_add(w.tabs, &spec, -1, NULL) != NULL);
    char *plain[] = {"bash", NULL};
    spec.cmd = plain;
    CHECK(sk_tabs_add(w.tabs, &spec, -1, NULL) != NULL); /* not Claude Code's: left out */

    sk_strv got = {0};
    sk_foreign_profiles(w.foreign, SK_PROFILE_CLAUDE, &got);
    char *dflt = sk_profiles_default(SK_PROFILE_CLAUDE);
    CHECK_INT(got.len, 4); /* no home scan yet */
    if (got.len == 4) {
        CHECK_STR(got.ptr[0], dflt);
        CHECK_STR(got.ptr[1], "/x/app");
        CHECK_STR(got.ptr[2], "/x/tab");
        CHECK_STR(got.ptr[3], "/x/rec");
    }
    sk_strv_free(&got);
    CHECK_INT(sk_foreign_home_scan(w.foreign, 0)->len, 1);
    sk_foreign_profiles(w.foreign, SK_PROFILE_CLAUDE, &got);
    CHECK_INT(got.len, 5);
    if (got.len == 5)
        CHECK_STR(got.ptr[4], work);
    sk_strv_free(&got);
    sk_foreign_profiles(w.foreign, SK_PROFILE_CODEX, &got);
    CHECK_INT(got.len, 2);
    if (got.len == 2)
        CHECK_STR(got.ptr[1], "/x/codex");
    sk_strv_free(&got);
    free(dflt);
    free(work);
    free(records);
    sk_envp_free(&tab_env);
    world_free(&w);
}

/* listSessions: the tab's own session and one inside a tab's CLI are
 * that tab's, one inside a CLI that is itself listed is left out, the
 * rest are found among the profiles, the home scanned again for those
 * not found. Those in a tab come first. */
static void test_the_live_sessions(void)
{
    world w;
    world_init(&w);
    sk_tab *tab = cli_tab(&w);
    if (!tab)
        goto done;
    pid_t sh = pid_of(tab);
    CHECK(sh > 0);
    sk_source *src = sk_bus_source(sk_clitab_bus(tab), &(sk_source_spec){NULL, 0, NULL, 0});
    sk_source_set_num(src, "state.busy", 1);
    char *claude[] = {"claude", NULL}, *codex[] = {"codex", "--model", "o9", NULL};
    sk_buf at = {0};
    sk_buf_printf(&at, "%s/a", w.home);
    live(&w, sh, SK_PROFILE_CLAUDE, 1700000005000, sk_buf_cstr(&at), claude);
    live(&w, 90001, SK_PROFILE_CLAUDE, 1700000004000, "/b", claude);
    w.anc[w.n_anc++] = (ancestry){90001, {sh}, 1};
    live(&w, 90002, SK_PROFILE_CODEX, 1700000003000, "/c", codex);
    w.anc[w.n_anc++] = (ancestry){90002, {80000, 70000}, 2};
    live(&w, 90003, SK_PROFILE_CLAUDE, 1700000002000, "/d", claude);
    sk_buf codex_home = {0};
    sk_buf_printf(&codex_home, "%s/.codex", w.home);
    w.ch[w.n_ch++] = (channel_of){90002, "/r/c.jsonl", "t1", codex_home.ptr, codex_home.ptr, fake_now - 5000};

    sk_foreign_entries l = {0};
    sk_foreign_sessions(w.foreign, &l);
    CHECK_INT(l.len, 3);
    char *rows = rows_of(&l);
    sk_buf want = {0};
    sk_buf_printf(&want,
                  "[{\"key\":\"%ld:1700000005000\",\"folder\":\"~/a\",\"age\":95000,\"tab\":%d,"
                  "\"where\":\"tab\",\"label\":\"sh\",\"state\":\"working\"},"
                  "{\"key\":\"90002:1700000003000\",\"folder\":\"/c\",\"age\":97000,\"tab\":null,"
                  "\"where\":\"\",\"label\":\"codex\",\"state\":\"working\"},"
                  "{\"key\":\"90003:1700000002000\",\"folder\":\"/d\",\"age\":98000,\"tab\":null,"
                  "\"where\":\"\",\"label\":\"claude [?]\",\"state\":\"unknown\"}]",
                  (long)sh, tab->id);
    CHECK_STR(rows, want.ptr);
    free(rows);
    if (l.len == 3) {
        CHECK(l.ptr[0].tab == tab);
        CHECK(l.ptr[0].agent == sk_clitab_agent(tab));
        CHECK(l.ptr[0].bus == sk_clitab_bus(tab));
        CHECK(l.ptr[0].s.found == NULL); /* a tab's is not looked for */
        CHECK(l.ptr[1].s.found && !strcmp(l.ptr[1].s.found->session, "t1"));
        CHECK(l.ptr[1].agent == NULL && l.ptr[1].bus == NULL);
        CHECK(l.ptr[2].s.found == NULL);
    }
    sk_foreign_entries_free(&l);
    /* Found with its profile, a session is asked once; else twice, the
     * home scanned between. */
    CHECK_INT(w.asked.len, 3);

    /* Its CLI not listed, a session inside a tab's CLI is that tab's. */
    sk_live_session first = w.live.ptr[0];
    memmove(w.live.ptr, w.live.ptr + 1, (w.live.len - 1) * sizeof *w.live.ptr);
    w.live.ptr[w.live.len - 1] = first;
    w.live.len--;
    sk_foreign_sessions(w.foreign, &l);
    CHECK_INT(l.len, 3);
    if (l.len == 3) {
        CHECK_STR(l.ptr[0].key, "90001:1700000004000");
        CHECK(l.ptr[0].tab == tab);
        CHECK_STR(l.ptr[0].where, "tab");
        CHECK_STR(l.ptr[0].label, "sh");
    }
    sk_foreign_entries_free(&l);
    w.live.len++;
    free(at.ptr);
    free(want.ptr);
    free(codex_home.ptr);
done:
    world_free(&w);
}

/* RESCAN_MS: a session whose channel the known profiles do not hold has
 * the home scanned again, at most once in 10 s. */
static void test_the_home_is_scanned_again_at_most_every_10_s(void)
{
    world w;
    world_init(&w);
    char *claude[] = {"claude", NULL};
    live(&w, 90003, SK_PROFILE_CLAUDE, 1700000002000, "/d", claude);
    sk_foreign_entries l = {0};
    sk_foreign_sessions(w.foreign, &l);
    sk_foreign_entries_free(&l);
    CHECK_INT(w.asked.len, 2);
    char *work = dir_in(w.home, ".claude-work");
    touch(work, ".credentials.json");
    fake_now += 5000;
    sk_foreign_sessions(w.foreign, &l);
    sk_foreign_entries_free(&l);
    CHECK_INT(w.asked.len, 4);
    CHECK(strstr(w.asked.ptr[3], work) == NULL); /* scanned 5 s ago */
    fake_now += 5001;
    sk_foreign_sessions(w.foreign, &l);
    sk_foreign_entries_free(&l);
    CHECK_INT(w.asked.len, 6);
    CHECK(strstr(w.asked.ptr[4], work) == NULL);
    CHECK(strstr(w.asked.ptr[5], work) != NULL); /* found by the rescan */
    free(work);
    world_free(&w);
}

/* openSession: an ended session says so; a session in a tab, or inside
 * a tab's CLI, makes that tab active; any other gets a monitor tab, once,
 * on a watch agent that follows its transcript. */
static void test_opening_a_session(void)
{
    world w;
    world_init(&w);
    sk_tab *tab = cli_tab(&w);
    if (!tab)
        goto done;
    pid_t sh = pid_of(tab);
    char *claude[] = {"claude", "--model", "opus", NULL};
    live(&w, sh, SK_PROFILE_CLAUDE, 1700000005000, "/a", claude);
    live(&w, 90001, SK_PROFILE_CLAUDE, 1700000004000, "/b", claude);
    w.anc[w.n_anc++] = (ancestry){90001, {sh}, 1};
    live(&w, 90003, SK_PROFILE_CLAUDE, 1700000002000, "/d", claude);
    char *transcript = sk_path_join(sk_test_tmpdir(), "d.jsonl");
    FILE *f = fopen(transcript, "w");
    CHECK(f != NULL);
    if (f) {
        fputs("{\"type\":\"assistant\",\"message\":{\"id\":\"m1\",\"model\":\"claude-test\",\"usage\":"
              "{\"input_tokens\":1,\"output_tokens\":5000,\"cache_creation_input_tokens\":0,"
              "\"cache_read_input_tokens\":0}}}\n",
              f);
        fclose(f);
    }
    w.ch[w.n_ch++] = (channel_of){90003, transcript, "w1", NULL, NULL, fake_now};

    char *err = NULL;
    CHECK_INT(sk_foreign_open(w.foreign, "1:2", &err), -1);
    CHECK_STR(err, "That session has ended.");
    free(err);

    CHECK_INT(sk_foreign_open(w.foreign, "90003:1700000002000", &err), 0);
    CHECK(err == NULL);
    CHECK_INT(sk_tabs_count(w.tabs), 2);
    sk_tab *mon = sk_tabs_active(w.tabs);
    CHECK(mon && mon->kind == SK_TAB_MONITOR);
    if (!mon || mon->kind != SK_TAB_MONITOR)
        goto done;
    CHECK_STR(mon->label, "claude [?]");
    CHECK_STR(mon->cwd, "/d");
    CHECK_STR(mon->cmd.ptr[2], "opus");
    CHECK(mon->file == NULL);
    CHECK(sk_foreign_watching(w.foreign, 90003) == mon);
    CHECK(sk_foreign_watching(w.foreign, 90001) == NULL);

    /* The session is the monitor's in the list now, before the rest. */
    sk_foreign_entries l = {0};
    sk_foreign_sessions(w.foreign, &l);
    CHECK_INT(l.len, 2);
    if (l.len == 2) {
        CHECK(l.ptr[0].tab == tab);
        CHECK(l.ptr[1].tab == mon);
        CHECK_STR(l.ptr[1].where, "monitor");
        CHECK_INT(l.ptr[1].tab_id, mon->id);
        CHECK(l.ptr[1].agent == sk_monitor_agent(mon));
    }
    sk_foreign_entries_free(&l);

    /* The tab's own session, or one inside its CLI, makes it active. */
    CHECK_INT(sk_foreign_open(w.foreign, "90001:1700000004000", &err), 0);
    CHECK(sk_tabs_active(w.tabs) == tab);
    /* Again: the monitor made active, no second one. */
    CHECK_INT(sk_foreign_open(w.foreign, "90003:1700000002000", &err), 0);
    CHECK(sk_tabs_active(w.tabs) == mon);
    CHECK_INT(sk_tabs_count(w.tabs), 2);
    sk_buf key = {0};
    sk_foreign_key(&key, sh, 1700000005000);
    CHECK_INT(sk_foreign_open(w.foreign, key.ptr, &err), 0);
    CHECK(sk_tabs_active(w.tabs) == tab);
    sk_buf_free(&key);

    /* The monitor's agent reads the transcript its channel names. */
    sk_monitor_start(mon);
    sk_agent *agent = sk_monitor_agent(mon);
    uint64_t t = sk_now_ms();
    for (int i = 0; i < 4; i++)
        sk_agent_tick(agent, t += SK_AGENT_TICK_MS);
    sk_report r;
    sk_agent_report(agent, &r);
    CHECK_STR(r.session, "w1");
    CHECK_INT(r.channels[0].state, SK_CH_LIVE);
    CHECK_STR(r.channels[0].target, transcript);
    const sk_val *out = sk_bus_get(sk_monitor_bus(mon), "tokens.out");
    CHECK(out && out->type == SK_VAL_NUM && out->num == 5000);

    /* Gone, it is no monitor's, and the monitor ends at its next poll. */
    w.gone[w.n_gone++] = 90003;
    CHECK(sk_foreign_watching(w.foreign, 90003) == NULL);
    sk_monitor_poll(mon);
    CHECK_INT(mon->session.ops->state(mon->session.self), SK_TAB_SESSION_ENDED);
    free(transcript);
done:
    world_free(&w);
}

/* watchAgent of a Codex session: Codex's own part, under the home it was
 * found in. */
static void test_a_codex_session_is_watched_by_codex(void)
{
    world w;
    world_init(&w);
    char *codex[] = {"codex", NULL};
    sk_foreign_session s;
    fake_session(&s, 99999, 1700000000000, SK_PROFILE_CODEX, "/c", codex);
    sk_session_channel found = {0};
    found.home = w.home;
    s.found = &found;
    sk_bus *bus = sk_bus_new(NULL, NULL);
    sk_agent *a = sk_foreign_watch(w.foreign, &s, bus);
    CHECK_STR(sk_agent_kind(a), "codex");
    sk_report r;
    sk_agent_report(a, &r);
    CHECK_STR(r.channels[0].name, "rollout");
    CHECK_INT(r.channels[0].state, SK_CH_WAITING);
    sk_agent_free(a);
    sk_bus_free(bus);
    s.found = NULL;
    fake_session_free(&s);
    world_free(&w);
}

const sk_test sk_suite_foreign[] = {
    T(test_keys_and_labels),
    T(test_the_known_profiles),
    T(test_the_live_sessions),
    T(test_the_home_is_scanned_again_at_most_every_10_s),
    T(test_opening_a_session),
    T(test_a_codex_session_is_watched_by_codex),
    {0},
};
