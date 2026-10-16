/* Focus reports and paste as xterm.js 6.0.0 made them, with the cases of
 * its Clipboard.test.ts. */
#include "../../src/term/keys.h"
#include "../test.h"

static void test_focus(void)
{
    sk_term_input_modes m = {0};
    sk_buf out = {0};
    CHECK(!sk_term_focus(&m, true, &out));
    CHECK(!sk_term_focus(&m, false, &out));
    CHECK_INT(out.len, 0);
    m.focus_events = true;
    CHECK(sk_term_focus(&m, true, &out));
    CHECK(sk_term_focus(&m, false, &out));
    CHECK_STR(sk_buf_cstr(&out), "\x1b[I\x1b[O");
    sk_buf_free(&out);
}

static void paste(bool bracketed, const char *text, size_t n, sk_buf *out)
{
    sk_term_input_modes m = {.bracketed_paste = bracketed};
    sk_buf_clear(out);
    sk_term_paste(&m, text, n, out);
}

#define PASTE(bracketed, lit, out) paste(bracketed, lit, sizeof(lit) - 1, out)

static void test_line_ends(void)
{
    sk_buf out = {0};
    PASTE(false, "foo\nbar\n", &out);
    CHECK_STR(sk_buf_cstr(&out), "foo\rbar\r");
    PASTE(false, "foo\r\nbar\r\n", &out);
    CHECK_STR(sk_buf_cstr(&out), "foo\rbar\r");
    PASTE(false, "a\rb", &out);
    CHECK_STR(sk_buf_cstr(&out), "a\rb");
    PASTE(false, "a\r\r\nb\n\r", &out);
    CHECK_STR(sk_buf_cstr(&out), "a\r\rb\r\r");
    PASTE(false, "\n\n", &out);
    CHECK_STR(sk_buf_cstr(&out), "\r\r");
    sk_buf_free(&out);
}

static void test_brackets(void)
{
    sk_buf out = {0};
    PASTE(false, "foo bar", &out);
    CHECK_STR(sk_buf_cstr(&out), "foo bar");
    PASTE(true, "foo bar", &out);
    CHECK_STR(sk_buf_cstr(&out), "\x1b[200~foo bar\x1b[201~");
    PASTE(true, "one\r\ntwo", &out);
    CHECK_STR(sk_buf_cstr(&out), "\x1b[200~one\rtwo\x1b[201~");
    PASTE(true, "", &out);
    CHECK_STR(sk_buf_cstr(&out), "\x1b[200~\x1b[201~");
    sk_buf_free(&out);
}

/* xterm.js filters nothing: escapes and controls pass, the end bracket
 * too. */
static void test_nothing_filtered(void)
{
    sk_buf out = {0};
    PASTE(true, "a\x1b[201~b\x03\x1b]0;x\x07", &out);
    CHECK_STR(sk_buf_cstr(&out), "\x1b[200~a\x1b[201~b\x03\x1b]0;x\x07\x1b[201~");
    PASTE(false, "tab\there\x7f", &out);
    CHECK_STR(sk_buf_cstr(&out), "tab\there\x7f");
    PASTE(false, "a\0b", &out);
    CHECK_INT(out.len, 3);
    CHECK(memcmp(out.ptr, "a\0b", 3) == 0);
    sk_buf_free(&out);
}

static void test_utf8(void)
{
    sk_buf out = {0};
    PASTE(false, "h\xc3\xa9llo \xf0\x9f\x98\x80", &out);
    CHECK_STR(sk_buf_cstr(&out), "h\xc3\xa9llo \xf0\x9f\x98\x80");
    PASTE(false, "a\xff\n\xe2\x82", &out);
    CHECK_STR(sk_buf_cstr(&out), "a\xef\xbf\xbd\r\xef\xbf\xbd");
    sk_buf_free(&out);
}

const sk_test sk_suite_keys_paste[] = {
    T(test_focus),
    T(test_line_ends),
    T(test_brackets),
    T(test_nothing_filtered),
    T(test_utf8),
    {0},
};
