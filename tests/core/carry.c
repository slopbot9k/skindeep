/* The option picker of decision 0023 with a table of its own (the
 * providers' tables are tested with their adapters), and the string
 * lists and environments the sessions pass around. */
#include "../../src/agents/agent.h"
#include "../../src/core/carry.h"
#include "../../src/core/env.h"
#include "../test.h"

static const sk_option TABLE[] = {
    {"--model", SK_ARITY_ONE},     {"-m", SK_ARITY_ONE},          {"--verbose", SK_ARITY_FLAG},
    {"-d", SK_ARITY_OPTIONAL},     {"--tools", SK_ARITY_MANY},    {"--append", SK_ARITY_ONE},
    {"--settings", SK_ARITY_ONE},  {"-a", SK_ARITY_ONE},
};

static void check_pick(const char *const *argv, const char *args, const char *dropped)
{
    sk_strv a = {0}, d = {0};
    sk_carry_pick((char *const *)argv, TABLE, SK_LEN(TABLE), &a, &d);
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

static void test_pick(void)
{
    const char *one[] = {"--model", "opus", "--resume", "abc", "fix it", NULL};
    check_pick(one, "--model|opus", "--resume|abc|fix it");
    /* = forms, flags, an optional value, a value that starts with - */
    const char *two[] = {"--model=sonnet", "--verbose", "-d", "api", "--append", "-be terse", "--continue", NULL};
    check_pick(two, "--model=sonnet|--verbose|-d|api|--append|-be terse", "--continue");
    /* -- and everything after it */
    const char *three[] = {"--settings", "s.json", "--", "--model", "opus", NULL};
    check_pick(three, "--settings|s.json", "--|--model|opus");
    /* many values, then an unknown option with its operand */
    const char *four[] = {"--new-thing", "v", "--tools", "Read", "Grep", "--verbose", NULL};
    check_pick(four, "--tools|Read|Grep|--verbose", "--new-thing|v");
    /* attached short values; an optional value that is not there */
    const char *five[] = {"-mo3", "-aon-request", "-d", "--verbose", "-", NULL};
    check_pick(five, "-mo3|-aon-request|-d|--verbose", "-");
    /* a one-value option at the very end takes nothing */
    const char *six[] = {"--model", NULL};
    check_pick(six, "--model", "");
}

static void test_plain_carries_nothing(void)
{
    char *cmd[] = {"bash", "--norc", "-i", NULL};
    sk_strv a = {0}, d = {0};
    sk_agent_carry(cmd, &a, &d);
    CHECK_INT(a.len, 0);
    CHECK_INT(d.len, 2);
    CHECK_STR(d.ptr[0], "--norc");
    sk_strv_free(&a);
    sk_strv_free(&d);
}

static void test_strv_and_env(void)
{
    sk_strv v = {0};
    CHECK(sk_strv_argv(&v)[0] == NULL);
    sk_strv_push(&v, "a");
    sk_strv_pushf(&v, "%s=%d", "n", 3);
    sk_strv_push_n(&v, "xyz", 2);
    CHECK_INT(v.len, 3);
    CHECK_STR(v.ptr[1], "n=3");
    CHECK_STR(v.ptr[2], "xy");
    CHECK(v.ptr[3] == NULL);
    sk_strv_free(&v);

    char *base[] = {"PATH=/bin", "HOME=/h", "EMPTY=", "SKINNED_SKIN=deck", NULL};
    sk_envp e;
    sk_envp_from(&e, base);
    CHECK_STR(sk_envp_get(&e, "PATH"), "/bin");
    CHECK_STR(sk_envp_get(&e, "EMPTY"), "");
    CHECK(sk_envp_get(&e, "PAT") == NULL);
    sk_envp_set(&e, "TERM", "xterm-256color");
    sk_envp_set(&e, "PATH", "/usr/bin");
    sk_envp_set(&e, "SKINNED_SKIN", NULL);
    CHECK_STR(sk_envp_get(&e, "TERM"), "xterm-256color");
    CHECK_STR(sk_envp_get(&e, "PATH"), "/usr/bin");
    CHECK(sk_envp_get(&e, "SKINNED_SKIN") == NULL);
    size_t n = 0;
    for (char *const *p = sk_envp_ptr(&e); *p; p++)
        n++;
    CHECK_INT(n, 4);
    sk_envp_free(&e);
}

const sk_test sk_suite_carry[] = {
    T(test_pick),
    T(test_plain_carries_nothing),
    T(test_strv_and_env),
    {0},
};
