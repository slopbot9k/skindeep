/* Background sessions seen from a tab (decision 0030), as
 * test/background.test.js has them: Claude Code's refusal to open one in
 * place, and the arguments that let the app open it instead. The
 * subcommand, attachedTranscript and create cases of that file test the
 * Claude adapter (app/metrics/claude), not this module. */
#include "../../src/app/background.h"
#include "../test.h"

#include <stdlib.h>

/* As Claude Code 2.1.296 prints it (function Sfe), wrapped by the
 * terminal's width, under the picker's last rows. */
#define SCREEN                                                                         \
    "  Resume Session\n"                                                               \
    "> Sort the photos   2 minutes ago\n"                                              \
    "\n"                                                                               \
    "\"Sort the photos\" is running in the background (5e3a91c0). Run\n"               \
    "`claude attach 5e3a91c0` to open it, or `claude stop 5e3a91c0` first to\n"        \
    "resume it here."

static char *refusal(const char *screen)
{
    char id[9];
    return sk_background_refusal(screen, id) ? sk_xstrdup(id) : NULL;
}

#define CHECK_REFUSAL(screen, want)                                          \
    do {                                                                     \
        char *id_ = refusal(screen);                                         \
        const char *want_ = (want);                                          \
        if (want_)                                                           \
            CHECK_STR(id_, want_);                                           \
        else                                                                 \
            CHECK(id_ == NULL);                                              \
        free(id_);                                                           \
    } while (0)

static char *replace_all(const char *s, const char *from, const char *to)
{
    sk_buf b = {0};
    size_t n = strlen(from);
    for (const char *p = s; *p;) {
        if (!strncmp(p, from, n)) {
            sk_buf_append_c(&b, to);
            p += n;
        } else {
            sk_buf_putc(&b, *p++);
        }
    }
    sk_buf_append_c(&b, "");
    return b.ptr;
}

static void test_the_refusal_names_the_job_wrapped_or_not_only_at_the_end_of_the_screen(void)
{
    CHECK_REFUSAL(SCREEN, "5e3a91c0");
    char *flat = replace_all(SCREEN, "\n", " ");
    CHECK_REFUSAL(flat, "5e3a91c0");
    free(flat);
    sk_buf more = {0};
    sk_buf_append_c(&more, SCREEN "\n");
    for (int i = 0; i < 10; i++)
        sk_buf_append_c(&more, "more\n");
    CHECK_REFUSAL(more.ptr, NULL); /* scrolled out of the tail */
    sk_buf_free(&more);
    char *two = replace_all(SCREEN, "attach 5e3a91c0", "attach 00000000");
    CHECK_REFUSAL(two, NULL); /* two ids */
    free(two);
    CHECK_REFUSAL("That session is running in the background. Run `claude agents` to find it", NULL);
    CHECK_REFUSAL(NULL, NULL);
}

static void test_only_resume_and_continue_arguments_let_the_app_open_the_session(void)
{
    char *const yes[][3] = {{NULL},
                            {"--resume", NULL},
                            {"-r", NULL},
                            {"--resume", "abc", NULL},
                            {"-r", "abc", NULL},
                            {"--resume=abc", NULL},
                            {"--continue", NULL},
                            {"-c", NULL}};
    for (size_t i = 0; i < SK_LEN(yes); i++)
        CHECK(sk_background_bare_resume(yes[i]));
    char *const no[][4] = {{"--model", "opus", NULL},
                           {"--resume", "--disallowedTools", "Bash", NULL},
                           {"--settings", "f", NULL},
                           {"--permission-mode", "plan", "-r", NULL},
                           {"--resume", "abc", "prompt", NULL}};
    for (size_t i = 0; i < SK_LEN(no); i++)
        CHECK(!sk_background_bare_resume(no[i]));

    sk_strv out = {0};
    char *const resume[] = {"claude", "--resume", NULL};
    CHECK(sk_background_attach_for(resume, SCREEN, &out));
    CHECK_INT(out.len, 3);
    CHECK_STR(out.ptr[0], "claude");
    CHECK_STR(out.ptr[1], "attach");
    CHECK_STR(out.ptr[2], "5e3a91c0");
    sk_strv_free(&out);
    char *const work[] = {"claude_work", "-r", NULL};
    CHECK(sk_background_attach_for(work, SCREEN, &out));
    CHECK_STR(out.ptr[0], "claude_work");
    sk_strv_free(&out);
    char *const own[] = {"claude", "--resume", "--model", "opus", NULL};
    CHECK(!sk_background_attach_for(own, SCREEN, &out)); /* the refusal is the user's own */
    CHECK(!sk_background_attach_for(resume, "Goodbye!", &out));
    CHECK_INT(out.len, 0);
}

/* Beyond the JavaScript tests: the rows as the terminal will give them,
 * blank rows passed over, whitespace of any kind made one space, and
 * the 400 code units between the two halves. */
static void test_the_rows_as_strings(void)
{
    const char *rows[] = {"\"Sort\" is running in the background (5e3a91c0).",
                          "   ",
                          "\xc2\xa0Run\t`claude attach 5e3a91c0`\xe2\x80\x83to open it",
                          ""};
    char id[9];
    CHECK(sk_background_refusal_rows(rows, SK_LEN(rows), id));
    CHECK_STR(id, "5e3a91c0");
    CHECK(!sk_background_refusal_rows(rows, 0, id));
}

static char *padded(size_t units, const char *pad)
{
    sk_buf b = {0};
    sk_buf_append_c(&b, "x is running in the background (0123abcd).");
    for (size_t i = 0; i < units; i++)
        sk_buf_append_c(&b, pad);
    sk_buf_append_c(&b, "Run `claude attach 0123abcd` to open it");
    return b.ptr;
}

static void test_the_two_halves_lie_within_400_code_units(void)
{
    char *s = padded(400, "a");
    CHECK_REFUSAL(s, "0123abcd");
    free(s);
    s = padded(401, "a");
    CHECK_REFUSAL(s, NULL);
    free(s);
    s = padded(200, "\xf0\x9f\x98\x80"); /* two code units each */
    CHECK_REFUSAL(s, "0123abcd");
    free(s);
    s = padded(201, "\xf0\x9f\x98\x80");
    CHECK_REFUSAL(s, NULL);
    free(s);
    s = padded(400, "\xc3\xa9"); /* one code unit, two bytes */
    CHECK_REFUSAL(s, "0123abcd");
    free(s);
}

const sk_test sk_suite_background[] = {
    T(test_the_refusal_names_the_job_wrapped_or_not_only_at_the_end_of_the_screen),
    T(test_only_resume_and_continue_arguments_let_the_app_open_the_session),
    T(test_the_rows_as_strings),
    T(test_the_two_halves_lie_within_400_code_units),
    {0},
};
