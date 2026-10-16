/* The app's own command line and its messages to the user (decision
 * 0003), from app/cli.js and bin/skinned, which have no JavaScript
 * tests of their own. */
#include "../../src/app/options.h"
#include "../test.h"

#include <fcntl.h>
#include <unistd.h>

/* The environment of the last parse: the options point into it. */
static sk_envp env;

static bool parse(sk_options *o, char *const *argv, const char *skin_env, const char *mute_env, sk_buf *err)
{
    sk_envp_free(&env);
    if (skin_env)
        sk_envp_set(&env, "SKINNED_SKIN", skin_env);
    if (mute_env)
        sk_envp_set(&env, "SKINNED_MUTE", mute_env);
    return sk_options_parse(o, argv, &env, err);
}

static void test_the_defaults_and_the_command_after_the_separator(void)
{
    char *const argv[] = {"skinned", "--", "claude", "--model", "opus", NULL};
    sk_options o;
    sk_buf err = {0};
    CHECK(parse(&o, argv, NULL, NULL, &err));
    CHECK(o.skin == NULL);
    CHECK_INT(o.statusline, SK_STATUSLINE_ALWAYS);
    CHECK(o.viz && !o.mute);
    CHECK(o.cmd == argv + 2);
    CHECK_STR(o.cmd[0], "claude");
    CHECK(o.cmd[3] == NULL);
    CHECK_INT(err.len, 0);
    sk_buf_free(&err);
}

static void test_every_option_of_ours(void)
{
    char *const argv[] = {"skinned", "--skin", "deck", "--statusline=chain-only", "--no-viz",
                          "--mute", "--", "codex", "--", "x", NULL};
    sk_options o;
    sk_buf err = {0};
    CHECK(parse(&o, argv, NULL, NULL, &err));
    CHECK_STR(o.skin, "deck");
    CHECK_INT(o.statusline, SK_STATUSLINE_CHAIN_ONLY);
    CHECK(!o.viz && o.mute);
    CHECK_STR(o.cmd[0], "codex");
    CHECK_STR(o.cmd[1], "--"); /* only the first -- is ours */
    char *const again[] = {"skinned", "--skin=/path/to/skin", "--statusline=chain-only",
                           "--statusline=always", "--", "claude", NULL};
    CHECK(parse(&o, again, NULL, NULL, &err));
    CHECK_STR(o.skin, "/path/to/skin");
    CHECK_INT(o.statusline, SK_STATUSLINE_ALWAYS); /* the last one counts */
    sk_buf_free(&err);
}

static void test_the_environment_gives_the_skin_and_the_mute(void)
{
    char *const argv[] = {"skinned", "--", "claude", NULL};
    char *const given[] = {"skinned", "--skin", "coffee", "--", "claude", NULL};
    sk_options o;
    sk_buf err = {0};
    CHECK(parse(&o, argv, "library", "1", &err));
    CHECK_STR(o.skin, "library");
    CHECK(o.mute);
    CHECK(parse(&o, given, "library", "yes", &err));
    CHECK_STR(o.skin, "coffee"); /* --skin over SKINNED_SKIN */
    CHECK(!o.mute);              /* only 1 mutes */
    CHECK(parse(&o, argv, "", "", &err));
    CHECK(o.skin == NULL);
    char *const empty[] = {"skinned", "--skin=", "--", "claude", NULL};
    CHECK(parse(&o, empty, "deck", NULL, &err));
    CHECK(o.skin == NULL); /* an empty name is no skin */
    sk_buf_free(&err);
    sk_envp_free(&env);
}

static void test_a_skin_at_the_end_takes_no_value(void)
{
    char *const argv[] = {"skinned", "--skin", "--", "claude", NULL};
    char *const eaten[] = {"skinned", "--skin", "--mute", "--", "claude", NULL};
    sk_options o;
    sk_buf err = {0};
    CHECK(parse(&o, argv, NULL, NULL, &err));
    CHECK(o.skin == NULL);
    CHECK(parse(&o, eaten, NULL, NULL, &err));
    CHECK_STR(o.skin, "--mute"); /* as the JavaScript took it */
    CHECK(!o.mute);
    sk_buf_free(&err);
}

static void test_no_command_is_the_usage_error(void)
{
    char *const none[] = {"skinned", "--skin", "deck", NULL};
    char *const bare[] = {"skinned", "--", NULL};
    char *const nothing[] = {"skinned", NULL};
    char *const empty[] = {NULL};
    char *const *cases[] = {none, bare, nothing, empty};
    for (size_t i = 0; i < SK_LEN(cases); i++) {
        sk_options o;
        sk_buf err = {0};
        CHECK(!parse(&o, cases[i], NULL, NULL, &err));
        CHECK_STR(sk_buf_cstr(&err), SK_USAGE);
        sk_buf_free(&err);
    }
}

static void test_an_unknown_option_of_ours_is_refused(void)
{
    char *const argv[] = {"skinned", "--skn", "deck", "--", "claude", NULL};
    char *const value[] = {"skinned", "--skin", "-dark", "--", "claude", NULL};
    char *const theirs[] = {"skinned", "--", "claude", "--unknown", NULL};
    sk_options o;
    sk_buf err = {0};
    CHECK(!parse(&o, argv, NULL, NULL, &err));
    CHECK_STR(sk_buf_cstr(&err), "unknown option --skn\n" SK_USAGE);
    sk_buf_clear(&err);
    CHECK(!parse(&o, value, NULL, NULL, &err));
    CHECK(!strncmp(sk_buf_cstr(&err), "unknown option -dark\n", 21));
    sk_buf_clear(&err);
    CHECK(parse(&o, theirs, NULL, NULL, &err)); /* the CLI's own options pass */
    sk_buf_free(&err);
}

static char *drain(int fd)
{
    sk_buf b = {0};
    char chunk[256];
    ssize_t n;
    while ((n = read(fd, chunk, sizeof chunk)) > 0)
        sk_buf_append(&b, chunk, (size_t)n);
    sk_buf_append_c(&b, "");
    return b.ptr;
}

static void test_tell_writes_to_the_callers_stderr_kept_close_on_exec(void)
{
    int p[2];
    CHECK(pipe(p) == 0);
    int user = sk_tell_adopt(p[1]);
    CHECK(user > 2 && user != p[1]);
    CHECK(fcntl(p[1], F_GETFD) == -1); /* the original is closed */
    CHECK(fcntl(user, F_GETFD) & FD_CLOEXEC);
    sk_tell("%s: command not found", "nosuch");
    sk_tell_adopt(-1); /* back to 2, closing the copy */
    CHECK(fcntl(user, F_GETFD) == -1);
    char *got = drain(p[0]);
    CHECK_STR(got, "skinned: nosuch: command not found\n");
    free(got);
    close(p[0]);
}

static void test_a_missing_fd_leaves_tell_on_stderr(void)
{
    int p[2];
    CHECK(pipe(p) == 0);
    close(p[1]);
    CHECK_INT(sk_tell_adopt(p[1]), 2);
    close(p[0]);
}

const sk_test sk_suite_options[] = {
    T(test_the_defaults_and_the_command_after_the_separator),
    T(test_every_option_of_ours),
    T(test_the_environment_gives_the_skin_and_the_mute),
    T(test_a_skin_at_the_end_takes_no_value),
    T(test_no_command_is_the_usage_error),
    T(test_an_unknown_option_of_ours_is_refused),
    T(test_tell_writes_to_the_callers_stderr_kept_close_on_exec),
    T(test_a_missing_fd_leaves_tell_on_stderr),
    {0},
};
