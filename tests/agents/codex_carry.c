/* What a new tab carries of a Codex command (decision 0023), the codex
 * cases of test/carry.test.js, and the dispatch of decision 0021. */
#include "../../src/agents/agent.h"
#include "../test.h"

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

static void test_setup_carried_subcommand_image_cd_and_prompt_dropped(void)
{
    char *cmd[] = {"codex", "-m", "o3", "resume", "--last", "-s", "read-only", "-i", "x.png",
                   "-C", "/tmp", "-c", "a.b=1", "hello", NULL};
    check_carry(cmd, "-m|o3|-s|read-only|-c|a.b=1", "resume|--last|-i|x.png|-C|/tmp|hello");
}

static void test_attached_short_values_and_flags(void)
{
    char *cmd[] = {"codex", "-mo3", "-aon-request", "--search", "--worktree", NULL};
    check_carry(cmd, "-mo3|-aon-request|--search", "--worktree");
}

/* /^codex(_.+)?$/ */
static void test_codex_and_codex_suffix_dispatch_to_codex(void)
{
    struct {
        const char *base;
        bool codex;
    } cases[] = {
        {"codex", true},    {"/opt/bin/codex", true}, {"codex_work", true}, {"codex_", false},
        {"codexx", false},  {"xcodex", false},        {"codex_a\nb", false}, {"Codex", false},
    };
    for (size_t i = 0; i < SK_LEN(cases); i++) {
        char *cmd[] = {(char *)cases[i].base, NULL};
        CHECK_STR(sk_agent_provider(cmd)->kind, cases[i].codex ? "codex" : "plain");
    }
}

const sk_test sk_suite_codex_carry[] = {
    T(test_setup_carried_subcommand_image_cd_and_prompt_dropped),
    T(test_attached_short_values_and_flags),
    T(test_codex_and_codex_suffix_dispatch_to_codex),
    {0},
};
