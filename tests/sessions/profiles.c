/* The profiles of New tab... (decision 0025), as test/profiles.test.js
 * has them: records newest first, one per profile, Forget, the list
 * order, the environment of a tab, and the home scan. */
#include "../../src/sessions/profiles.h"
#include "../test.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

/* The records as "t agent dir" lines, to compare whole. */
static char *records_text(const sk_profile_records *r)
{
    sk_buf b = {0};
    for (size_t i = 0; i < r->len; i++)
        sk_buf_printf(&b, "%g %s %s\n", r->ptr[i].t, sk_profile_agents[r->ptr[i].agent].kind, r->ptr[i].dir);
    sk_buf_reserve(&b, 0);
    return b.ptr;
}

static void check_read(const char *d, const char *want)
{
    sk_profile_records r = {0};
    sk_profiles_read(d, &r);
    char *got = records_text(&r);
    CHECK_STR(got, want);
    free(got);
    sk_profile_records_free(&r);
}

static void test_records_one_per_profile_newest_first_forgotten_one_by_one(void)
{
    char *d = sk_path_join(sk_test_tmpdir(), "profiles");
    CHECK_INT(sk_profiles_record(d, SK_PROFILE_CLAUDE, "/p/work", 1000), 0);
    CHECK_INT(sk_profiles_record(d, SK_PROFILE_CODEX, "/p/lab", 2000), 0);
    CHECK_INT(sk_profiles_record(d, SK_PROFILE_CLAUDE, "/p/work", 3000), 0);
    char *junk = sk_path_join(d, "junk"), *bad = sk_path_join(d, "claude-0000000000000000");
    write_file(junk, "x");
    write_file(bad, "not json");
    check_read(d, "3000 claude /p/work\n2000 codex /p/lab\n");
    sk_profiles_forget(d, SK_PROFILE_CLAUDE, "/p/work");
    check_read(d, "2000 codex /p/lab\n");
    char *none = sk_path_join(d, "none");
    check_read(none, "");
    free(none);
    free(junk);
    free(bad);
    free(d);
}

/* The choices as "dir default remembered" lines. */
static char *choices_text(const sk_profile_choices *c)
{
    sk_buf b = {0};
    for (size_t i = 0; i < c->len; i++)
        sk_buf_printf(&b, "%s %d %d\n", c->ptr[i].dir, c->ptr[i].is_default, c->ptr[i].remembered);
    sk_buf_reserve(&b, 0);
    return b.ptr;
}

static void test_the_list_the_default_the_open_tabs_then_the_records_each_once(void)
{
    char *dflt = sk_profiles_default(SK_PROFILE_CLAUDE);
    sk_profile_records records = {0};
    sk_vec_push(&records, ((sk_profile_record){3, SK_PROFILE_CLAUDE, sk_xstrdup("/p/a")}));
    sk_vec_push(&records, ((sk_profile_record){2, SK_PROFILE_CODEX, sk_xstrdup("/p/x")}));
    sk_vec_push(&records, ((sk_profile_record){1, SK_PROFILE_CLAUDE, sk_xstrdup("/p/b")}));
    char *open[] = {"/p/b", dflt};
    sk_profile_choices c = {0};
    sk_profiles_list(SK_PROFILE_CLAUDE, &records, open, 2, NULL, &c);
    sk_buf want = {0};
    sk_buf_printf(&want, "%s 1 0\n/p/b 0 1\n/p/a 0 1\n", dflt);
    char *got = choices_text(&c);
    CHECK_STR(got, want.ptr);
    free(got);
    sk_buf_free(&want);
    sk_profile_choices_free(&c);
    sk_profile_records_free(&records);
    free(dflt);
}

static char *env_text(sk_envp *e)
{
    sk_buf b = {0};
    for (char *const *v = sk_envp_ptr(e); *v; v++)
        sk_buf_printf(&b, "%s%s", b.len ? " " : "", *v);
    sk_buf_reserve(&b, 0);
    return b.ptr;
}

#define CHECK_ENV_FOR(agent, p, want)                                        \
    do {                                                                     \
        sk_envp e_;                                                          \
        sk_envp_from(&e_, sk_envp_ptr(&env));                                \
        sk_profiles_env_for(&e_, agent, p);                                   \
        char *t_ = env_text(&e_);                                            \
        CHECK_STR(t_, want);                                                 \
        free(t_);                                                            \
        sk_envp_free(&e_);                                                   \
    } while (0)

static void test_a_tabs_environment_the_variable_set_or_unset_for_the_default(void)
{
    char *vars[] = {"PATH=/bin", "CLAUDE_CONFIG_DIR=/p/lab", NULL};
    sk_envp env;
    sk_envp_from(&env, vars);
    char *claude = sk_profiles_default(SK_PROFILE_CLAUDE), *codex = sk_profiles_default(SK_PROFILE_CODEX);
    CHECK_ENV_FOR(SK_PROFILE_CLAUDE, "/p/work", "PATH=/bin CLAUDE_CONFIG_DIR=/p/work");
    CHECK_ENV_FOR(SK_PROFILE_CLAUDE, claude, "PATH=/bin");
    CHECK_ENV_FOR(SK_PROFILE_CODEX, "/p/x", "PATH=/bin CLAUDE_CONFIG_DIR=/p/lab CODEX_HOME=/p/x");
    CHECK_STR(sk_envp_get(&env, "CLAUDE_CONFIG_DIR"), "/p/lab");
    char *of = sk_profiles_of(&env, SK_PROFILE_CLAUDE);
    CHECK_STR(of, "/p/lab");
    free(of);
    of = sk_profiles_of(&env, SK_PROFILE_CODEX);
    CHECK_STR(of, codex);
    free(of);
    /* path.resolve: a profile given with a trailing slash or dots is the
     * same profile. */
    sk_envp_set(&env, "CLAUDE_CONFIG_DIR", "/p/./lab/../lab/");
    of = sk_profiles_of(&env, SK_PROFILE_CLAUDE);
    CHECK_STR(of, "/p/lab");
    free(of);
    free(claude);
    free(codex);
    sk_envp_free(&env);
}

/* root/rel made, with each file empty and each name ending in / a dir. */
static char *mk(const char *root, const char *rel, const char *const *files)
{
    char *d = sk_path_join(root, rel);
    CHECK(sk_mkdirs(d, 0777) == 0);
    for (; files && *files; files++) {
        char *p = sk_path_join(d, *files);
        size_t n = strlen(*files);
        if ((*files)[n - 1] == '/')
            CHECK(sk_mkdirs(p, 0777) == 0);
        else
            write_file(p, "");
        free(p);
    }
    return d;
}

static char *founds_text(const sk_profile_founds *f, const char *root)
{
    sk_buf b = {0};
    size_t n = strlen(root);
    for (size_t i = 0; i < f->len; i++) {
        const char *dir = f->ptr[i].dir;
        if (!strncmp(dir, root, n))
            dir += n;
        sk_buf_printf(&b, "%s %s\n", sk_profile_agents[f->ptr[i].agent].kind, dir);
    }
    sk_buf_reserve(&b, 0);
    return b.ptr;
}

static void test_the_scan_config_dirs_by_the_names_they_hold_bounded_links_not_followed(void)
{
    const char *root = sk_test_tmpdir();
    char *p = sk_path_join(root, ".claude.json");
    write_file(p, "");
    free(p);
    free(mk(root, ".claude", (const char *[]){".credentials.json", "projects/", NULL}));
    char *split = mk(root, ".split/a/.claude", (const char *[]){".claude.json", "sessions/", NULL});
    free(mk(root, ".split/a/.claude/inner", (const char *[]){".credentials.json", NULL}));
    free(mk(root, ".split/b/.claude", (const char *[]){".claude.json", NULL}));
    free(mk(root, ".codex", (const char *[]){"auth.json", "config.toml", NULL}));
    free(mk(root, ".decoy", (const char *[]){"settings.json", "config.toml", NULL}));
    free(mk(root, "a/b/c/d/.claude", (const char *[]){".credentials.json", NULL}));
    free(mk(root, "x/node_modules/.claude", (const char *[]){".credentials.json", NULL}));
    char *target = sk_path_join(root, ".claude"), *link = sk_path_join(root, "link"),
         *loop = sk_path_join(root, "loop");
    CHECK(symlink(target, link) == 0);
    CHECK(symlink(root, loop) == 0);

    sk_profile_founds found = {0};
    sk_profiles_scan(root, 4, SK_PROFILES_SCAN_CAP, &found);
    char *got = founds_text(&found, root);
    CHECK_STR(got, "claude /.claude\ncodex /.codex\nclaude /.split/a/.claude\n");
    free(got);

    sk_profile_founds deep = {0};
    sk_profiles_scan(root, 5, SK_PROFILES_SCAN_CAP, &deep);
    size_t under = 0;
    for (size_t i = 0; i < deep.len; i++) {
        if (strstr(deep.ptr[i].dir, "/a/b/")) {
            under++;
            char *want = sk_path_join(root, "a/b/c/d/.claude");
            CHECK_STR(deep.ptr[i].dir, want);
            free(want);
        }
    }
    CHECK_INT(under, 1);
    sk_profile_founds_free(&deep);

    sk_profile_founds capped = {0};
    sk_profiles_scan(root, SK_PROFILES_SCAN_DEPTH, 1, &capped);
    CHECK_INT(capped.len, 0);
    sk_profile_founds_free(&capped);

    char *dflt = sk_profiles_default(SK_PROFILE_CLAUDE);
    sk_profile_records records = {0};
    sk_vec_push(&records, ((sk_profile_record){1, SK_PROFILE_CLAUDE, sk_xstrdup("/p/a")}));
    sk_vec_push(&found, ((sk_profile_found){SK_PROFILE_CLAUDE, sk_xstrdup("/p/a")}));
    sk_profile_choices c = {0};
    sk_profiles_list(SK_PROFILE_CLAUDE, &records, NULL, 0, &found, &c);
    sk_buf want = {0};
    sk_buf_printf(&want, "%s 1 0\n/p/a 0 1\n%s 0 0\n%s 0 0\n", dflt, target, split);
    got = choices_text(&c);
    CHECK_STR(got, want.ptr);
    free(got);
    sk_buf_free(&want);
    sk_profile_choices_free(&c);
    sk_profile_records_free(&records);
    sk_profile_founds_free(&found);
    free(dflt);
    free(target);
    free(link);
    free(loop);
    free(split);
}

static void test_the_records_dir_is_under_the_state_dir(void)
{
    sk_env e = {SK_OS_LINUX, "/home/u", NULL, NULL, NULL, NULL, 1000};
    char *d = sk_profiles_dir(&e);
    CHECK_STR(d, "/home/u/.local/state/claude-skins/profiles");
    free(d);
    e.xdg_state_home = "/s";
    d = sk_profiles_dir(&e);
    CHECK_STR(d, "/s/claude-skins/profiles");
    free(d);
}

const sk_test sk_suite_profiles[] = {
    T(test_records_one_per_profile_newest_first_forgotten_one_by_one),
    T(test_the_list_the_default_the_open_tabs_then_the_records_each_once),
    T(test_a_tabs_environment_the_variable_set_or_unset_for_the_default),
    T(test_the_scan_config_dirs_by_the_names_they_hold_bounded_links_not_followed),
    T(test_the_records_dir_is_under_the_state_dir),
    {0},
};
