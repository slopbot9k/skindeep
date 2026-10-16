/* New tab...'s core (decision 0025), as app/main.js has chooserState,
 * chooserOpen, chooser:forget and folderOf: in a temporary home with
 * fake profile folders, fake claude and codex programs on the tabs'
 * PATH, and tabs whose sessions are fakes. */
#include "../../src/app/newtab.h"
#include "../../src/base/paths.h"
#include "../../src/sessions/files.h"
#include "../test.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    sk_tabs *tabs;
    sk_foreign *foreign;
    sk_newtab_config cfg;
    sk_envp env;
    char *home, *bin, *profiles, *old_home;
    const char *fail; /* make refuses with it */
    sk_strv logged;
} world;

static void fake_hangup(void *self)
{
    (void)self;
}

static void fake_free(void *self)
{
    (void)self;
}

static const sk_tab_session_ops FAKE = {fake_hangup, NULL, NULL, NULL, NULL, NULL, NULL, fake_free};

static int make(void *ud, sk_tab *tab, const sk_tab_spec *spec, char **err)
{
    world *w = ud;
    (void)spec;
    if (w->fail) {
        *err = sk_xstrdup(w->fail);
        return -1;
    }
    tab->session = (sk_tab_session){&FAKE, NULL};
    return 0;
}

static void on_log(void *ud, const char *msg)
{
    sk_strv_push(&((world *)ud)->logged, msg);
}

static void quiet(void *ud, const char *msg)
{
    (void)ud, (void)msg;
}

static double fixed_now(void)
{
    return 1700000000000.0;
}

/* <parent>/name made, with its parents, normalized, as TMPDIR may end
 * in a slash. */
static char *made(const char *parent, const char *name)
{
    char *j = sk_path_join(parent, name), *d = sk_files_resolve(j);
    free(j);
    CHECK_INT(sk_mkdirs(d, 0700), 0);
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

static void program(const char *dir, const char *name)
{
    char *p = sk_path_join(dir, name);
    FILE *f = fopen(p, "w");
    CHECK(f != NULL);
    if (f) {
        fputs("#!/bin/sh\nexit 0\n", f);
        fclose(f);
    }
    chmod(p, 0755);
    free(p);
}

/* A Claude Code config dir as the scan tells one. */
static void claude_dir(const char *home, const char *name)
{
    char *d = made(home, name);
    touch(d, ".credentials.json");
    free(d);
}

static char *const WINDOW_CMD[] = {"claude", "--model", "opus", "--resume", "abc", NULL};

/* The world's home is HOME while it lives, as the app's own home is. */
static void setup(world *w)
{
    memset(w, 0, sizeof *w);
    const char *h = getenv("HOME");
    w->old_home = h ? sk_xstrdup(h) : NULL;
    w->home = made(sk_test_tmpdir(), "home");
    setenv("HOME", w->home, 1);
    w->bin = made(sk_test_tmpdir(), "bin");
    program(w->bin, "claude");
    program(w->bin, "codex");
    char *state = made(sk_test_tmpdir(), "state");
    w->profiles = sk_path_join(state, "profiles");
    free(state);
    sk_envp_set(&w->env, "PATH", w->bin);
    sk_envp_set(&w->env, "CLAUDE_CONFIG_DIR", NULL);
    sk_tabs_config tc;
    memset(&tc, 0, sizeof tc);
    tc.make = make;
    tc.tell = quiet;
    tc.log = quiet;
    tc.ud = w;
    tc.cmd = WINDOW_CMD;
    tc.file = "/usr/bin/claude";
    tc.env = &w->env;
    tc.home = w->home;
    w->tabs = sk_tabs_new(&tc);
    sk_foreign_config fc;
    memset(&fc, 0, sizeof fc);
    fc.tabs = w->tabs;
    fc.env = &w->env;
    fc.home = w->home;
    fc.profiles_dir = w->profiles;
    fc.log = quiet;
    w->foreign = sk_foreign_new(&fc);
    w->cfg = (sk_newtab_config){
        .tabs = w->tabs,
        .foreign = w->foreign,
        .cmd = WINDOW_CMD,
        .env = &w->env,
        .home = w->home,
        .cwd = w->home,
        .profiles_dir = w->profiles,
        .now = fixed_now,
        .log = on_log,
        .ud = w,
    };
}

static void teardown(world *w)
{
    sk_foreign_free(w->foreign);
    sk_tabs_free(w->tabs);
    sk_envp_free(&w->env);
    sk_strv_free(&w->logged);
    if (w->old_home)
        setenv("HOME", w->old_home, 1);
    else
        unsetenv("HOME");
    free(w->old_home);
    free(w->home);
    free(w->bin);
    free(w->profiles);
}

/* A tab of cmd in cwd, with VAR=value set in its environment when var
 * is not NULL. */
static sk_tab *tab_of(world *w, char *const *cmd, const char *cwd, const char *var, const char *value)
{
    sk_envp env = {0};
    sk_envp_from(&env, w->env.vars.ptr);
    if (var)
        sk_envp_set(&env, var, value);
    sk_tab_spec spec = {SK_TAB_CLI, cmd, cwd, &env, "/usr/bin/x", NULL, NULL, NULL};
    sk_tab *t = sk_tabs_add(w->tabs, &spec, -1, NULL);
    sk_envp_free(&env);
    CHECK(t != NULL);
    return t;
}

/* An agent's profiles as "shown default remembered" lines. */
static char *profiles_text(const sk_newtab_agent *a)
{
    sk_buf b = {0};
    sk_buf_reserve(&b, 0);
    for (size_t i = 0; i < a->profiles.len; i++)
        sk_buf_printf(&b, "%s %d %d\n", a->profiles.ptr[i].shown, a->profiles.ptr[i].is_default,
                      a->profiles.ptr[i].remembered);
    return b.ptr;
}

#define CHECK_PROFILES(agent, want)                                          \
    do {                                                                     \
        char *got_ = profiles_text(agent);                                   \
        CHECK_STR(got_, want);                                               \
        free(got_);                                                          \
    } while (0)

static char *in_home(world *w, const char *rest)
{
    return sk_path_join(w->home, rest);
}

static void test_the_offer_the_window_command_carried_and_the_bare_cli(void)
{
    world w;
    setup(&w);
    sk_newtab_state st;
    sk_newtab_offer(&w.cfg, -1, true, &st);
    /* The window runs Claude Code: its line keeps what New tab carries
     * and names what it does not; Codex gets its bare CLI. */
    CHECK_STR(st.agents[SK_PROFILE_CLAUDE].line, "claude --model opus");
    CHECK_STR(st.agents[SK_PROFILE_CLAUDE].dropped, "--resume abc");
    CHECK_STR(st.agents[SK_PROFILE_CODEX].line, "codex");
    CHECK_STR(st.agents[SK_PROFILE_CODEX].dropped, "");
    /* No tab: Claude Code on its default, in the app's folder. */
    CHECK_INT(st.agent, SK_PROFILE_CLAUDE);
    char *dflt = in_home(&w, ".claude");
    CHECK_STR(st.agents[SK_PROFILE_CLAUDE].profile, dflt);
    CHECK_PROFILES(&st.agents[SK_PROFILE_CLAUDE], "~/.claude 1 0\n");
    CHECK_PROFILES(&st.agents[SK_PROFILE_CODEX], "~/.codex 1 0\n");
    CHECK_STR(st.folder, "~");
    CHECK_STR(st.home, w.home);
    CHECK_STR(st.base, w.home);
    free(dflt);
    sk_newtab_state_free(&st);
    teardown(&w);
}

static void test_the_offer_starts_on_the_active_tab_its_agent_profile_and_folder(void)
{
    world w;
    setup(&w);
    char *work = made(w.home, "src/app"), *lab = made(w.home, ".codex-lab");
    char *const codex[] = {"codex", "-m", "o3", NULL};
    tab_of(&w, codex, work, "CODEX_HOME", lab);
    sk_newtab_state st;
    sk_newtab_offer(&w.cfg, -1, true, &st);
    CHECK_INT(st.agent, SK_PROFILE_CODEX);
    CHECK_STR(st.agents[SK_PROFILE_CODEX].profile, lab);
    /* The open tab's profile is listed after the default. */
    CHECK_PROFILES(&st.agents[SK_PROFILE_CODEX], "~/.codex 1 0\n~/.codex-lab 0 0\n");
    char *dflt = in_home(&w, ".claude");
    CHECK_STR(st.agents[SK_PROFILE_CLAUDE].profile, dflt);
    CHECK_STR(st.folder, "~/src/app");
    CHECK_STR(st.base, work);
    sk_newtab_state_free(&st);
    /* A tab's live folder, where its CLI has gone since. */
    free(dflt);
    teardown(&w);
    free(work);
    free(lab);
}

static char *live_elsewhere(void *ud, const sk_tab *tab)
{
    (void)tab;
    return sk_path_join(((world *)ud)->home, "moved");
}

static void test_the_folder_is_the_live_one_of_the_active_tab(void)
{
    world w;
    setup(&w);
    char *start = made(w.home, "start"), *moved = made(w.home, "moved");
    tab_of(&w, WINDOW_CMD, start, NULL, NULL);
    w.cfg.live_cwd = live_elsewhere;
    sk_newtab_state st;
    sk_newtab_offer(&w.cfg, -1, true, &st);
    CHECK_STR(st.folder, "~/moved");
    sk_newtab_state_free(&st);
    /* A live folder that is gone falls back to the one it started in. */
    rmdir(moved);
    sk_newtab_offer(&w.cfg, -1, true, &st);
    CHECK_STR(st.folder, "~/start");
    sk_newtab_state_free(&st);
    teardown(&w);
    free(start);
    free(moved);
}

static void test_the_profiles_default_open_recorded_newest_first_then_found(void)
{
    world w;
    setup(&w);
    claude_dir(w.home, ".claude-found");
    char *work = made(w.home, ".claude-work"), *old = made(w.home, ".claude-old");
    tab_of(&w, WINDOW_CMD, w.home, "CLAUDE_CONFIG_DIR", work);
    CHECK_INT(sk_profiles_record(w.profiles, SK_PROFILE_CLAUDE, old, 1000), 0);
    CHECK_INT(sk_profiles_record(w.profiles, SK_PROFILE_CLAUDE, work, 2000), 0);
    sk_newtab_state st;
    sk_newtab_offer(&w.cfg, -1, true, &st);
    CHECK_PROFILES(&st.agents[SK_PROFILE_CLAUDE],
                   "~/.claude 1 0\n~/.claude-work 0 1\n~/.claude-old 0 1\n~/.claude-found 0 0\n");
    CHECK_STR(st.agents[SK_PROFILE_CLAUDE].profile, work);
    sk_newtab_state_free(&st);
    /* Forget keeps the scan of the open dialog: a profile made since is
     * found by the next opening alone. */
    claude_dir(w.home, ".claude-new");
    sk_newtab_forget(&w.cfg, "claude", old);
    sk_newtab_offer(&w.cfg, -1, false, &st);
    CHECK_PROFILES(&st.agents[SK_PROFILE_CLAUDE], "~/.claude 1 0\n~/.claude-work 0 1\n~/.claude-found 0 0\n");
    sk_newtab_state_free(&st);
    sk_newtab_offer(&w.cfg, -1, true, &st);
    CHECK_PROFILES(&st.agents[SK_PROFILE_CLAUDE],
                   "~/.claude 1 0\n~/.claude-work 0 1\n~/.claude-found 0 0\n~/.claude-new 0 0\n");
    sk_newtab_state_free(&st);
    /* A profile the scan finds stays listed when forgotten. */
    CHECK_INT(sk_profiles_record(w.profiles, SK_PROFILE_CLAUDE, "/nowhere", 3000), 0);
    char *found = in_home(&w, ".claude-found");
    CHECK_INT(sk_profiles_record(w.profiles, SK_PROFILE_CLAUDE, found, 4000), 0);
    sk_newtab_forget(&w.cfg, "claude", found);
    sk_newtab_forget(&w.cfg, "nobody", "/nowhere");
    sk_newtab_offer(&w.cfg, -1, false, &st);
    CHECK_PROFILES(&st.agents[SK_PROFILE_CLAUDE],
                   "~/.claude 1 0\n~/.claude-work 0 1\n/nowhere 0 1\n~/.claude-found 0 0\n~/.claude-new 0 0\n");
    sk_newtab_state_free(&st);
    free(found);
    free(work);
    free(old);
    teardown(&w);
}

/* sk_newtab_open's error for a pick, or NULL. */
static char *refusal(world *w, const char *agent, const char *profile, const char *folder, const char *line)
{
    sk_newtab_pick p = {(char *)agent, (char *)profile, (char *)folder, (char *)line};
    char *err = NULL;
    int r = sk_newtab_open(&w->cfg, -1, &p, &err);
    CHECK_INT(r, err ? -1 : 0);
    return err;
}

/* want NULL: opened. */
#define CHECK_REFUSED(w, agent, profile, folder, line, want)                 \
    do {                                                                     \
        char *e_ = refusal(w, agent, profile, folder, line);                 \
        const char *want_ = (want);                                          \
        if (want_)                                                           \
            CHECK_STR(e_, want_);                                            \
        else if (e_)                                                         \
            sk_test_fail(__FILE__, __LINE__, "refused: %s", e_);             \
        free(e_);                                                            \
    } while (0)

static void test_a_pick_is_refused_as_main_js_words_it(void)
{
    world w;
    setup(&w);
    char *dflt = in_home(&w, ".claude"), *gone = in_home(&w, ".claude-gone");
    CHECK_REFUSED(&w, "gemini", dflt, "~", "claude", "no agent gemini");
    CHECK_REFUSED(&w, NULL, dflt, "~", "claude", "no agent undefined");
    CHECK_REFUSED(&w, "claude", dflt, "~/nope", "claude", "no such folder: ~/nope");
    CHECK_REFUSED(&w, "claude", dflt, "/nope/at/all", "claude", "no such folder: /nope/at/all");
    CHECK_REFUSED(&w, "claude", NULL, "~", "claude", "choose a profile");
    CHECK_REFUSED(&w, "claude", "relative", "~", "claude", "choose a profile");
    CHECK_REFUSED(&w, "claude", gone, "~", "claude", "no such profile folder: ~/.claude-gone");
    CHECK_REFUSED(&w, "claude", dflt, "~", "claude 'open", "command line: unclosed single quote");
    CHECK_REFUSED(&w, "claude", dflt, "~", "   ", "command line: the command line is empty");
    CHECK_REFUSED(&w, "claude", dflt, "~", "claude_work --x", "claude_work: command not found");
    CHECK_REFUSED(&w, "claude", dflt, "~", "./bin/claude", "./bin/claude: command not found");
    w.fail = "no pty";
    CHECK_REFUSED(&w, "claude", dflt, "~", "claude", "cannot open the tab: no pty");
    CHECK_INT(w.logged.len, 1);
    CHECK_STR(w.logged.ptr[0], "new tab: no pty");
    /* The default profile need not exist: the CLI makes it at login. */
    w.fail = NULL;
    CHECK_REFUSED(&w, "claude", dflt, "~", "claude", NULL);
    CHECK_INT(sk_tabs_count(w.tabs), 1);
    free(dflt);
    free(gone);
    teardown(&w);
}

static void test_an_open_tab_has_its_command_folder_profile_and_record(void)
{
    world w;
    setup(&w);
    char *work = made(w.home, ".claude-work"), *src = made(w.home, "src/app");
    sk_tab *from = tab_of(&w, WINDOW_CMD, src, NULL, NULL);
    (void)from;
    /* A relative folder is the active tab's; ~ is the home. */
    CHECK_REFUSED(&w, "claude", work, "  ../app/./ ", "claude --model 'big one' \"x\\\"y\"", NULL);
    sk_tab *t = sk_tabs_active(w.tabs);
    CHECK_STR(t->cwd, src);
    CHECK_INT(t->cmd.len, 4);
    CHECK_STR(t->cmd.ptr[2], "big one");
    CHECK_STR(t->cmd.ptr[3], "x\"y");
    char *claude = sk_path_join(w.bin, "claude");
    CHECK_STR(t->file, claude);
    CHECK_STR(sk_envp_get(&t->env, "CLAUDE_CONFIG_DIR"), work);
    CHECK_STR(sk_envp_get(&t->env, "PATH"), w.bin);
    CHECK_STR(t->label, "claude [work]");
    sk_profile_records r = {0};
    sk_profiles_read(w.profiles, &r);
    CHECK_INT(r.len, 1);
    if (r.len) {
        CHECK_STR(r.ptr[0].dir, work);
        CHECK(r.ptr[0].t == 1700000000000.0);
    }
    sk_profile_records_free(&r);
    /* The default profile runs with the variable unset, even when the
     * window's environment sets it, and is not recorded. */
    sk_envp_set(&w.env, "CODEX_HOME", "/elsewhere");
    char *codex_dflt = in_home(&w, ".codex");
    CHECK_REFUSED(&w, "codex", codex_dflt, "~", "codex", NULL);
    t = sk_tabs_active(w.tabs);
    CHECK(sk_envp_get(&t->env, "CODEX_HOME") == NULL);
    CHECK_STR(t->cwd, w.home);
    CHECK_STR(t->label, "codex");
    sk_profiles_read(w.profiles, &r);
    CHECK_INT(r.len, 1);
    sk_profile_records_free(&r);
    /* A command with a slash is taken from the tab's folder. */
    char *tools = made(src, "tools");
    program(tools, "wrap");
    CHECK_REFUSED(&w, "claude", work, "~/src/app", "tools/wrap", NULL);
    t = sk_tabs_active(w.tabs);
    char *wrap = sk_path_join(tools, "wrap");
    CHECK_STR(t->file, wrap);
    CHECK_STR(t->cmd.ptr[0], "tools/wrap");
    free(wrap);
    free(tools);
    free(codex_dflt);
    free(claude);
    free(work);
    free(src);
    teardown(&w);
}

static void test_a_record_that_cannot_be_written_is_logged(void)
{
    world w;
    setup(&w);
    char *work = made(w.home, ".claude-work");
    /* The records' directory is a file. */
    char *state = made(sk_test_tmpdir(), "state");
    touch(state, "profiles");
    CHECK_REFUSED(&w, "claude", work, "~", "claude", NULL);
    CHECK_INT(sk_tabs_count(w.tabs), 1);
    CHECK_INT(w.logged.len, 1);
    if (w.logged.len)
        CHECK(!strncmp(w.logged.ptr[0], "profiles: ", 10));
    free(state);
    free(work);
    teardown(&w);
}

static void test_a_folder_as_typed(void)
{
    char *p;
#define CHECK_FOLDER(typed, home, base, want)                                \
    p = sk_newtab_folder_of(typed, home, base);                              \
    CHECK_STR(p, want);                                                      \
    free(p)
    CHECK_FOLDER("~", "/home/u", "/w", "/home/u");
    CHECK_FOLDER("~/", "/home/u", "/w", "/home/u");
    CHECK_FOLDER("~/a/../b", "/home/u", "/w", "/home/u/b");
    CHECK_FOLDER(" \t~/x \n", "/home/u", "/w", "/home/u/x");
    CHECK_FOLDER("~x", "/home/u", "/w", "/w/~x");
    CHECK_FOLDER("rel/./d/", "/home/u", "/w/v", "/w/v/rel/d");
    CHECK_FOLDER("..", "/home/u", "/w/v", "/w");
    CHECK_FOLDER("/abs//d/", "/home/u", "/w", "/abs/d");
    CHECK_FOLDER("", "/home/u", "/w/v", "/w/v");
    CHECK_FOLDER(NULL, "/home/u", "/w/v", "/w/v");
    CHECK_FOLDER("~", NULL, "/w", "/w/~");
#undef CHECK_FOLDER
}

static void test_the_picker_starts_in_the_folder_typed_else_the_home(void)
{
    char *d = made(sk_test_tmpdir(), "there");
    char *p = sk_newtab_browse_start("there", "/home/u", sk_test_tmpdir());
    CHECK_STR(p, d);
    free(p);
    p = sk_newtab_browse_start("not-there", "/home/u", sk_test_tmpdir());
    CHECK_STR(p, "/home/u");
    free(p);
    p = sk_newtab_browse_start("not-there", NULL, "/w");
    CHECK_STR(p, "/w");
    free(p);
    free(d);
}

const sk_test sk_suite_newtab[] = {
    T(test_the_offer_the_window_command_carried_and_the_bare_cli),
    T(test_the_offer_starts_on_the_active_tab_its_agent_profile_and_folder),
    T(test_the_folder_is_the_live_one_of_the_active_tab),
    T(test_the_profiles_default_open_recorded_newest_first_then_found),
    T(test_a_pick_is_refused_as_main_js_words_it),
    T(test_an_open_tab_has_its_command_folder_profile_and_record),
    T(test_a_record_that_cannot_be_written_is_logged),
    T(test_a_folder_as_typed),
    T(test_the_picker_starts_in_the_folder_typed_else_the_home),
    {0},
};
