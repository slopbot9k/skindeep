#include "../../src/base/utf8.h"
#include "../test.h"

#define FFFD "\xEF\xBF\xBD"

static void test_decode(void)
{
    uint32_t cp;
    bool bad = false;
    CHECK_INT(sk_utf8_decode("A", 1, &cp, &bad), 1);
    CHECK_INT(cp, 'A');
    CHECK_INT(sk_utf8_decode("\xC3\xA9", 2, &cp, &bad), 2);
    CHECK_INT(cp, 0xE9);
    CHECK_INT(sk_utf8_decode("\xE2\x82\xAC", 3, &cp, &bad), 3);
    CHECK_INT(cp, 0x20AC);
    CHECK_INT(sk_utf8_decode("\xF0\x9F\x98\x80", 4, &cp, &bad), 4);
    CHECK_INT(cp, 0x1F600);
    CHECK(!bad);
    /* Each case: the bytes the WHATWG decoder takes as one U+FFFD. */
    struct {
        const char *s;
        size_t n, take;
    } cases[] = {
        {"\x80", 1, 1},             /* a stray continuation */
        {"\xC0\xAF", 2, 1},         /* never a lead byte */
        {"\xF5\x80", 2, 1},         /* past U+10FFFF */
        {"\xE0\x80\x80", 3, 1},     /* overlong: E0 needs A0..BF next */
        {"\xED\xA0\x80", 3, 1},     /* a surrogate: ED needs 80..9F next */
        {"\xF4\x90\x80\x80", 4, 1}, /* past U+10FFFF */
        {"\xE2\x82", 2, 2},         /* cut off */
        {"\xE2\x82X", 3, 2},
        {"\xF0\x9F\x98", 3, 3},
    };
    for (size_t i = 0; i < SK_LEN(cases); i++) {
        bad = false;
        CHECK_INT(sk_utf8_decode(cases[i].s, cases[i].n, &cp, &bad), cases[i].take);
        CHECK_INT(cp, 0xFFFD);
        CHECK(bad);
    }
}

static void test_encode(void)
{
    char out[4];
    CHECK_INT(sk_utf8_encode('A', out), 1);
    CHECK_INT(sk_utf8_encode(0xE9, out), 2);
    CHECK(memcmp(out, "\xC3\xA9", 2) == 0);
    CHECK_INT(sk_utf8_encode(0x20AC, out), 3);
    CHECK(memcmp(out, "\xE2\x82\xAC", 3) == 0);
    CHECK_INT(sk_utf8_encode(0x1F600, out), 4);
    CHECK(memcmp(out, "\xF0\x9F\x98\x80", 4) == 0);
    CHECK_INT(sk_utf8_encode(0xD800, out), 3);
    CHECK(memcmp(out, FFFD, 3) == 0);
    CHECK_INT(sk_utf8_encode(0x110000, out), 3);
    CHECK(memcmp(out, FFFD, 3) == 0);
}

static void test_valid(void)
{
    CHECK(sk_utf8_valid("", 0));
    CHECK(sk_utf8_valid("h\xC3\xA9llo \xF0\x9F\x98\x80", 11));
    CHECK(sk_utf8_valid(FFFD, 3)); /* a real U+FFFD is valid */
    CHECK(!sk_utf8_valid("\xC0\xAF", 2));
    CHECK(!sk_utf8_valid("ab\xE2\x82", 4));
}

/* What Buffer.from(bytes).toString('utf8') gives in Node. */
static void test_sanitize_like_node(void)
{
    struct {
        const char *in;
        size_t n;
        const char *out;
    } cases[] = {
        {"plain", 5, "plain"},
        {"a\xFF" "b", 3, "a" FFFD "b"},
        {"\xE0\x80\x80", 3, FFFD FFFD FFFD},
        {"\xF0\x9F\x98", 3, FFFD},
        {"\xF0\x9F\x98" "A", 4, FFFD "A"},
        {"\xED\xA0\x80", 3, FFFD FFFD FFFD},
        {"\xC3\xA9\xC3", 3, "\xC3\xA9" FFFD},
    };
    for (size_t i = 0; i < SK_LEN(cases); i++) {
        sk_buf b = {0};
        sk_utf8_sanitize(&b, cases[i].in, cases[i].n);
        CHECK_STR(sk_buf_cstr(&b), cases[i].out);
        sk_buf_free(&b);
    }
}

static void test_whole(void)
{
    CHECK_INT(sk_utf8_whole("", 0), 0);
    CHECK_INT(sk_utf8_whole("abc", 3), 3);
    CHECK_INT(sk_utf8_whole("ab\xE2\x82", 4), 2);      /* held for the next chunk */
    CHECK_INT(sk_utf8_whole("ab\xE2\x82\xAC", 5), 5);  /* complete */
    CHECK_INT(sk_utf8_whole("ab\xE2" "A", 4), 4);       /* already invalid */
    CHECK_INT(sk_utf8_whole("\xF0\x9F\x98", 3), 0);
    CHECK_INT(sk_utf8_whole("x\xF0", 2), 1);
    CHECK_INT(sk_utf8_whole("ab\x80", 3), 3);           /* stray continuation */
    CHECK_INT(sk_utf8_whole("\xC3\xA9\xE0\x80", 4), 4); /* E0 80 can never complete */
}

const sk_test sk_suite_utf8[] = {
    T(test_decode),
    T(test_encode),
    T(test_valid),
    T(test_sanitize_like_node),
    T(test_whole),
    {0},
};
