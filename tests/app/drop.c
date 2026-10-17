/* The text a drop of files pastes (decision 0002), as the page's drop
 * handler in app/renderer/renderer.js built it: each path quoted for a
 * shell, joined by spaces, a space after. */
#include "../../src/app/drop.h"
#include "../test.h"

static void check_drop(const char *const *paths, size_t n, const char *want, int line)
{
    sk_buf out = {0};
    sk_drop_text(&out, paths, n);
    if (strcmp(sk_buf_cstr(&out), want) != 0)
        sk_test_fail(__FILE__, line, "drop text:\n  \"%s\"\n  \"%s\"", sk_buf_cstr(&out), want);
    sk_buf_free(&out);
}

#define CHECK_DROP(want, ...)                                                \
    do {                                                                     \
        const char *const p_[] = {__VA_ARGS__};                              \
        check_drop(p_, SK_LEN(p_), want, __LINE__);                          \
    } while (0)

static void test_a_plain_path_goes_as_it_is_with_a_space_after(void)
{
    CHECK_DROP("/home/u/notes.txt ", "/home/u/notes.txt");
    CHECK_DROP("/srv/a@b%c+d=e:f,g.h/i-j_9 ", "/srv/a@b%c+d=e:f,g.h/i-j_9");
}

static void test_any_other_path_is_quoted(void)
{
    CHECK_DROP("'/home/u/two words' ", "/home/u/two words");
    CHECK_DROP("'/home/u/it'\\''s here' ", "/home/u/it's here");
    CHECK_DROP("'/tmp/$HOME' ", "/tmp/$HOME");
    CHECK_DROP("'/tmp/a*b?' ", "/tmp/a*b?");
    CHECK_DROP("'~/x' ", "~/x");
    CHECK_DROP("'/tmp/\xc3\xa9t\xc3\xa9' ", "/tmp/\xc3\xa9t\xc3\xa9");
    CHECK_DROP("'/tmp/line\nbreak' ", "/tmp/line\nbreak");
}

static void test_several_paths_are_joined_by_spaces(void)
{
    CHECK_DROP("/a/b '/c d' /e ", "/a/b", "/c d", "/e");
}

static void test_empty_paths_are_skipped(void)
{
    CHECK_DROP("/a /b ", "", "/a", "", "/b");
    CHECK_DROP("", "");
    check_drop(NULL, 0, "", __LINE__);
}

const sk_test sk_suite_drop[] = {
    T(test_a_plain_path_goes_as_it_is_with_a_space_after),
    T(test_any_other_path_is_quoted),
    T(test_several_paths_are_joined_by_spaces),
    T(test_empty_paths_are_skipped),
    {0},
};
