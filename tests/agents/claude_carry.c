/* What a new Claude tab carries from the window's command (decision
 * 0023): the claude cases of test/carry.test.js, and the subcommands of
 * test/background.test.js that get nothing injected (decision 0030). */
#include "../../src/agents/agent.h"
#include "../../src/agents/claude/internal.h"
#include "../test.h"

/* args and dropped of cmd, each joined with |. */
static void check_carry(char *const *cmd, const char *args, const char *dropped)
{
    sk_strv a = {0}, d = {0};
    sk_agent_carry(cmd, &a, &d);
    sk_buf ja = {0}, jd = {0};
    for (size_t i = 0; i < a.len; i++)
        sk_buf_printf(&ja, "%s%s", i ? "|" : "", a.ptr[i]);
    for (size_t i = 0; i < d.len; i++)
        sk_buf_printf(&jd, "%s%s", i ? "|" : "", d.ptr[i]);
    CHECK_STR(sk_buf_cstr(&ja), args);
    CHECK_STR(sk_buf_cstr(&jd), dropped);
    sk_buf_free(&ja);
    sk_buf_free(&jd);
    sk_strv_free(&a);
    sk_strv_free(&d);
}

static void test_setup_carried_session_and_prompt_dropped(void)
{
    char *cmd[] = {"claude", "--model", "opus", "--permission-mode", "plan", "--disallowedTools", "Bash", "Edit",
                   "--resume", "abc", "fix it", NULL};
    check_carry(cmd, "--model|opus|--permission-mode|plan|--disallowedTools|Bash|Edit", "--resume|abc|fix it");
}

static void test_equals_forms_flags_optional_value_and_dash_value(void)
{
    char *cmd[] = {"claude_work", "--model=sonnet", "--verbose", "-d", "api", "--append-system-prompt",
                   "-be terse", "--continue", NULL};
    check_carry(cmd, "--model=sonnet|--verbose|-d|api|--append-system-prompt|-be terse", "--continue");
}

static void test_print_mode_fork_worktree_and_double_dash(void)
{
    char *cmd[] = {"claude", "-p", "--output-format", "json", "--fork-session", "-w", "x", "--settings", "s.json",
                   "--", "--model", "opus", NULL};
    check_carry(cmd, "--settings|s.json", "-p|--output-format|json|--fork-session|-w|x|--|--model|opus");
}

static void test_unknown_option_dropped_with_its_operand(void)
{
    char *cmd[] = {"claude", "--new-thing", "v", "--tools", "Read", "Grep", "--add-dir", "../lib", NULL};
    check_carry(cmd, "--tools|Read|Grep|--add-dir|../lib", "--new-thing|v");
}

static void test_subcommand_is_the_first_argument_only(void)
{
    char *attach[] = {"claude", "attach", "x", NULL}, *mcp[] = {"claude", "mcp", "list", NULL};
    char *prompt[] = {"claude", "--model", "opus", "attach", NULL}, *fix[] = {"claude", "fix the bug", NULL};
    char *bare[] = {"claude", NULL};
    CHECK_STR(sk_claude_subcommand(attach), "attach");
    CHECK_STR(sk_claude_subcommand(mcp), "mcp");
    CHECK(sk_claude_subcommand(prompt) == NULL); /* Claude Code reads it as a prompt */
    CHECK(sk_claude_subcommand(fix) == NULL);
    CHECK(sk_claude_subcommand(bare) == NULL);
}

const sk_test sk_suite_claude_carry[] = {
    T(test_setup_carried_session_and_prompt_dropped),
    T(test_equals_forms_flags_optional_value_and_dash_value),
    T(test_print_mode_fork_worktree_and_double_dash),
    T(test_unknown_option_dropped_with_its_operand),
    T(test_subcommand_is_the_first_argument_only),
    {0},
};
