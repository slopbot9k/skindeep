/* The tab's identity (decision 0004) from app/identity.js, which has no
 * JavaScript tests of its own: the label and its config dir rule, the
 * title, quoting, the home as ~, and the live command line's hook. */
#include "../../src/app/identity.h"
#include "../test.h"

#include <stdlib.h>
#include <unistd.h>

#define CHECK_OUT(call, want)                                                \
    do {                                                                     \
        sk_buf out_ = {0};                                                   \
        call;                                                                \
        CHECK_STR(sk_buf_cstr(&out_), want);                                 \
        sk_buf_free(&out_);                                                  \
    } while (0)

static char *quoted(const char *s)
{
    sk_buf b = {0};
    sk_identity_quote(&b, sk_str_c(s));
    return b.ptr;
}

#define CHECK_QUOTE(s, want)                                                 \
    do {                                                                     \
        char *q_ = quoted(s);                                                \
        CHECK_STR(q_, want);                                                 \
        free(q_);                                                            \
    } while (0)

static void test_quote_leaves_plain_words_and_quotes_the_rest(void)
{
    CHECK_QUOTE("claude", "claude");
    CHECK_QUOTE("--model=opus", "--model=opus");
    CHECK_QUOTE("a@b%c+d:e,f./g-h_9", "a@b%c+d:e,f./g-h_9");
    CHECK_QUOTE("", "''");
    CHECK_QUOTE("two words", "'two words'");
    CHECK_QUOTE("it's", "'it'\\''s'");
    CHECK_QUOTE("$HOME", "'$HOME'");
    CHECK_QUOTE("~", "'~'");
    CHECK_QUOTE("\xc3\xa9t\xc3\xa9", "'\xc3\xa9t\xc3\xa9'");
    char *const argv[] = {"claude", "--append-system-prompt", "be brief", NULL};
    CHECK_OUT(sk_identity_quote_argv(&out_, argv), "claude --append-system-prompt 'be brief'");
}

static void test_tildify_writes_the_home_as_a_tilde(void)
{
    CHECK_OUT(sk_identity_tildify(&out_, "/home/u", "/home/u"), "~");
    CHECK_OUT(sk_identity_tildify(&out_, "/home/u/Repos/x", "/home/u"), "~/Repos/x");
    CHECK_OUT(sk_identity_tildify(&out_, "/home/user2", "/home/u"), "/home/user2");
    CHECK_OUT(sk_identity_tildify(&out_, "/srv", "/home/u"), "/srv");
    CHECK_OUT(sk_identity_tildify(&out_, "/home/u", NULL), "/home/u");
}

static void test_shown_cmdline_tildifies_each_unquoted_word(void)
{
    const char *line = "claude --settings /home/u/.local/state/claude-skins/settings/a.json "
                       "'/home/u/x y' /home/u /home/uv";
    CHECK_OUT(sk_identity_shown_cmdline(&out_, line, "/home/u"),
              "claude --settings ~/.local/state/claude-skins/settings/a.json "
              "'/home/u/x y' ~ /home/uv");
    CHECK_OUT(sk_identity_shown_cmdline(&out_, "ls /x", "/"), "ls /x");
    CHECK_OUT(sk_identity_shown_cmdline(&out_, "", "/home/u"), "");
    CHECK_OUT(sk_identity_shown_cmdline(&out_, "a  /home/u/b", "/home/u"), "a  ~/b");
}

static void label_of(sk_buf *out, const char *cmd0, const char *var, const char *value, const char *kind)
{
    sk_envp env = {0};
    if (var)
        sk_envp_set(&env, var, value);
    char *const cmd[] = {(char *)cmd0, NULL};
    sk_identity_label(out, cmd, &env, kind, "/home/u");
    sk_envp_free(&env);
}

static void test_the_label_names_a_config_dir_other_than_the_default(void)
{
    CHECK_OUT(label_of(&out_, "/usr/bin/claude", NULL, NULL, "claude"), "claude");
    CHECK_OUT(label_of(&out_, "claude", "CLAUDE_CONFIG_DIR", "/home/u/.claude", "claude"), "claude");
    CHECK_OUT(label_of(&out_, "claude", "CLAUDE_CONFIG_DIR", "/home/u/.claude/", "claude"), "claude");
    CHECK_OUT(label_of(&out_, "claude", "CLAUDE_CONFIG_DIR", "", "claude"), "claude");
    CHECK_OUT(label_of(&out_, "claude", "CLAUDE_CONFIG_DIR", "/home/u/.claude-work", "claude"),
              "claude [work]");
    CHECK_OUT(label_of(&out_, "claude", "CLAUDE_CONFIG_DIR", "/home/u/profiles/work/.claude", "claude"),
              "claude [work]");
    CHECK_OUT(label_of(&out_, "claude_x", "CLAUDE_CONFIG_DIR", "/p/claude-team", "claude"),
              "claude_x [team]");
    CHECK_OUT(label_of(&out_, "claude", "CLAUDE_CONFIG_DIR", "/p/.alt", "claude"), "claude [alt]");
    CHECK_OUT(label_of(&out_, "claude", "CLAUDE_CONFIG_DIR", "/p/q/../.claude-w", "claude"),
              "claude [w]");
    CHECK_OUT(label_of(&out_, "codex", "CODEX_HOME", "/home/u/.codex", "codex"), "codex");
    CHECK_OUT(label_of(&out_, "codex", "CODEX_HOME", "/home/u/.codex-ci", "codex"), "codex [ci]");
    CHECK_OUT(label_of(&out_, "codex", "CODEX_HOME", "/w/ci/.codex", "codex"), "codex [ci]");
    /* The variable of the other CLI, or of none, names nothing. */
    CHECK_OUT(label_of(&out_, "codex", "CLAUDE_CONFIG_DIR", "/home/u/.claude-work", "codex"), "codex");
    CHECK_OUT(label_of(&out_, "bash", "CLAUDE_CONFIG_DIR", "/home/u/.claude-work", "plain"), "bash");
    /* A leaf with nothing left names nothing. */
    CHECK_OUT(label_of(&out_, "claude", "CLAUDE_CONFIG_DIR", "/p/.claude-", "claude"), "claude");
    CHECK_OUT(label_of(&out_, "claude", "CLAUDE_CONFIG_DIR", "/.claude", "claude"), "claude");
}

static void test_a_relative_config_dir_is_the_working_directorys(void)
{
    char cwd[4096];
    CHECK(getcwd(cwd, sizeof cwd) != NULL);
    sk_buf want = {0};
    sk_buf_printf(&want, "claude [rel]");
    CHECK_OUT(label_of(&out_, "claude", "CLAUDE_CONFIG_DIR", "x/../.claude-rel", "claude"), want.ptr);
    sk_buf_free(&want);
}

static void test_the_title_names_the_app(void)
{
    CHECK_OUT(sk_identity_title(&out_, "claude [work]"), "claude [work] in claude-skins");
}

static void test_the_live_cmdline_reads_the_kernels_argv(void)
{
    const char raw[] = "claude\0--settings\0/a b\0\0x\0";
    CHECK_OUT(sk_identity_cmdline_of(&out_, raw, sizeof raw - 1), "claude --settings '/a b' '' x");
    CHECK_OUT(sk_identity_cmdline_of(&out_, "a\0b", 3), "a b");
    CHECK_OUT(sk_identity_cmdline_of(&out_, "", 0), "");
    CHECK_OUT(sk_identity_cmdline_of(&out_, "a\0\0", 3), "a ''");
    CHECK_OUT(sk_identity_cmdline_of(&out_, "\xff\0", 2), "'\xef\xbf\xbd'");
}

static bool fake_argv(pid_t pid, sk_buf *raw)
{
    if (pid != 42)
        return false;
    sk_buf_append(raw, "sh\0-c\0exec claude\0", 18);
    return true;
}

static void test_the_live_cmdline_needs_its_source(void)
{
    sk_buf out = {0};
    sk_identity_set_argv_source(NULL);
    CHECK(!sk_identity_live_cmdline(42, &out));
    sk_identity_set_argv_source(fake_argv);
    CHECK(sk_identity_live_cmdline(42, &out));
    CHECK_STR(sk_buf_cstr(&out), "sh -c 'exec claude'");
    CHECK(!sk_identity_live_cmdline(43, &out));
    sk_identity_set_argv_source(NULL);
    sk_buf_free(&out);
}

/* The app's source is the kernel's: this test program's own argv. */
static void test_the_kernels_argv_is_the_apps_source(void)
{
    sk_buf raw = {0}, out = {0};
    CHECK(sk_identity_kernel_argv(getpid(), &raw));
    CHECK(raw.len > 0 && raw.ptr[raw.len - 1] == 0);
    CHECK(strstr(sk_buf_cstr(&raw), "run") != NULL);
    sk_identity_set_argv_source(sk_identity_kernel_argv);
    CHECK(sk_identity_live_cmdline(getpid(), &out));
    CHECK(strstr(sk_buf_cstr(&out), "run") != NULL);
    sk_identity_set_argv_source(NULL);
    sk_buf_free(&raw);
    sk_buf_free(&out);
}

const sk_test sk_suite_identity[] = {
    T(test_quote_leaves_plain_words_and_quotes_the_rest),
    T(test_tildify_writes_the_home_as_a_tilde),
    T(test_shown_cmdline_tildifies_each_unquoted_word),
    T(test_the_label_names_a_config_dir_other_than_the_default),
    T(test_a_relative_config_dir_is_the_working_directorys),
    T(test_the_title_names_the_app),
    T(test_the_live_cmdline_reads_the_kernels_argv),
    T(test_the_live_cmdline_needs_its_source),
    T(test_the_kernels_argv_is_the_apps_source),
    {0},
};
