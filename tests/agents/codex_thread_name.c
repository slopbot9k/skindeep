/* Codex thread names from session_index.jsonl (decision 0032), as
 * test/thread-name.test.js has it. */
#include "../../src/agents/codex/codex.h"
#include "../../src/base/paths.h"
#include "../test.h"

static void line(sk_buf *b, const char *id, const char *name)
{
    sk_buf_printf(b, "{\"id\":\"%s\",\"thread_name\":\"%s\",\"updated_at\":\"2026-10-14T00:00:00Z\"}",
                  id, name);
}

static void check_name(sk_str text, const char *id, const char *want)
{
    char *got = sk_codex_thread_name(text, id);
    if (want)
        CHECK_STR(got, want);
    else
        CHECK(got == NULL);
    free(got);
}

static void test_the_last_line_of_a_thread_names_it(void)
{
    sk_buf text = {0};
    line(&text, "A", "first");
    sk_buf_append_c(&text, "\nnot json\n");
    line(&text, "B", "bee");
    sk_buf_putc(&text, '\n');
    line(&text, "A", " second ");
    sk_buf_append_c(&text, "\n{\"id\":\"A\"}\n");
    check_name(sk_buf_str(&text), "A", "second");
    check_name(sk_buf_str(&text), "B", "bee");
    check_name(sk_buf_str(&text), "C", NULL);
    check_name(SK_STR(""), "A", NULL);
    sk_buf_free(&text);
}

static void append(const char *file, const char *id, const char *name)
{
    sk_buf b = {0};
    line(&b, id, name);
    sk_buf_putc(&b, '\n');
    FILE *f = fopen(file, "ab");
    CHECK(f != NULL);
    if (f) {
        fwrite(b.ptr, 1, b.len, f);
        fclose(f);
    }
    sk_buf_free(&b);
}

static void test_names_read_the_index_and_follow_a_change(void)
{
    const char *home = sk_test_tmpdir();
    char *vars[] = {NULL, NULL};
    sk_buf var = {0};
    sk_buf_printf(&var, "CODEX_HOME=%s", home);
    vars[0] = var.ptr;
    sk_envp env;
    sk_envp_from(&env, vars);
    sk_codex_names names;
    sk_codex_names_init(&names, &env);
    CHECK(sk_codex_names_of(&names, "A") == NULL);
    CHECK(sk_codex_names_of(&names, NULL) == NULL);
    char *file = sk_path_join(home, "session_index.jsonl");
    append(file, "A", "one");
    CHECK_STR(sk_codex_names_of(&names, "A"), "one");
    append(file, "A", "two");
    CHECK_STR(sk_codex_names_of(&names, "A"), "two");
    free(file);
    sk_codex_names_free(&names);
    sk_envp_free(&env);
    sk_buf_free(&var);
}

/* A name is trimmed and cut to 120 UTF-16 units; an empty one is none. */
static void test_a_name_is_trimmed_and_cut(void)
{
    sk_buf text = {0}, longer = {0};
    for (int i = 0; i < 130; i++)
        sk_buf_putc(&longer, 'n');
    line(&text, "A", longer.ptr);
    sk_buf_putc(&text, '\n');
    char *got = sk_codex_thread_name(sk_buf_str(&text), "A");
    CHECK(got && strlen(got) == SK_CODEX_NAME_MAX);
    free(got);
    sk_buf_clear(&text);
    line(&text, "A", "kept");
    sk_buf_putc(&text, '\n');
    line(&text, "A", "   ");
    check_name(sk_buf_str(&text), "A", NULL);
    sk_buf_free(&text);
    sk_buf_free(&longer);
}

const sk_test sk_suite_codex_thread_name[] = {
    T(test_the_last_line_of_a_thread_names_it),
    T(test_names_read_the_index_and_follow_a_change),
    T(test_a_name_is_trimmed_and_cut),
    {0},
};
