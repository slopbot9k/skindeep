/* The command line of New tab... split into argv (decision 0025), as
 * test/argv.test.js has it. */
#include "../../src/app/argv.h"
#include "../../src/app/identity.h"
#include "../test.h"

/* The words of line joined by |, or the error. */
static char *split(const char *line)
{
    sk_strv v = {0};
    sk_buf out = {0};
    const char *err = sk_argv_split(line, &v);
    if (err)
        sk_buf_printf(&out, "error: %s", err);
    for (size_t i = 0; !err && i < v.len; i++)
        sk_buf_printf(&out, "%s%s", i ? "|" : "", v.ptr[i]);
    sk_strv_free(&v);
    sk_buf_append_c(&out, "");
    return out.ptr;
}

#define CHECK_SPLIT(line, want)                                              \
    do {                                                                     \
        char *got_ = split(line);                                            \
        CHECK_STR(got_, want);                                               \
        free(got_);                                                          \
    } while (0)

static void test_blanks_separate_words_quotes_and_backslashes_join_them(void)
{
    CHECK_SPLIT("  claude --model  opus\t-c ", "claude|--model|opus|-c");
    CHECK_SPLIT("codex -c 'model=\"o3\"' a\\ b \"x \\\"y\\\" \\n\"",
                "codex|-c|model=\"o3\"|a b|x \"y\" \\n");
    CHECK_SPLIT("a'b'\"c\"d ''", "abcd|");
}

static void test_nothing_expands(void)
{
    CHECK_SPLIT("ls ~ $HOME *.js >out", "ls|~|$HOME|*.js|>out");
}

static void test_the_inverse_of_the_quoting_the_strip_uses(void)
{
    const char *words[] = {"claude", "--append-system-prompt", "it's a test", "two  spaces",
                           "a\"b", "back\\slash", "$x", ""};
    sk_buf line = {0};
    for (size_t i = 0; i < SK_LEN(words); i++) {
        if (i)
            sk_buf_putc(&line, ' ');
        sk_identity_quote(&line, sk_str_c(words[i]));
    }
    sk_strv v = {0};
    CHECK(sk_argv_split(line.ptr, &v) == NULL);
    CHECK_INT(v.len, SK_LEN(words));
    for (size_t i = 0; i < SK_LEN(words) && i < v.len; i++)
        CHECK_STR(v.ptr[i], words[i]);
    sk_strv_free(&v);
    sk_buf_free(&line);
}

static void test_a_malformed_line_is_refused_with_a_reason(void)
{
    CHECK_SPLIT("claude 'open", "error: unclosed single quote");
    CHECK_SPLIT("claude \"open", "error: unclosed double quote");
    CHECK_SPLIT("claude \\", "error: a backslash ends the line");
    CHECK_SPLIT("   ", "error: the command line is empty");
    sk_strv v = {0};
    sk_strv_push(&v, "kept");
    CHECK(sk_argv_split("a 'b", &v) != NULL);
    CHECK_INT(v.len, 1); /* a refused line appends nothing */
    sk_strv_free(&v);
}

/* Beyond the JavaScript tests: a backslash takes a whole UTF-8
 * character, as JavaScript's code units rejoin. */
static void test_a_backslash_before_utf8_keeps_the_character(void)
{
    CHECK_SPLIT("echo \\\xc3\xa9t\xc3\xa9 \"\\\xf0\x9f\x98\x80\"", "echo|\xc3\xa9t\xc3\xa9|\\\xf0\x9f\x98\x80");
}

const sk_test sk_suite_argv[] = {
    T(test_blanks_separate_words_quotes_and_backslashes_join_them),
    T(test_nothing_expands),
    T(test_the_inverse_of_the_quoting_the_strip_uses),
    T(test_a_malformed_line_is_refused_with_a_reason),
    T(test_a_backslash_before_utf8_keeps_the_character),
    {0},
};
