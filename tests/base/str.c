#include "../test.h"

#include <stdint.h>

static void test_compare(void)
{
    CHECK(sk_str_eq(SK_STR("abc"), sk_str_c("abc")));
    CHECK(!sk_str_eq(SK_STR("abc"), SK_STR("abd")));
    CHECK(sk_str_eq(SK_STR(""), sk_str_n(NULL, 0)));
    CHECK(sk_str_eq_nocase(SK_STR("Content-Length"), SK_STR("content-length")));
    CHECK(!sk_str_eq_nocase(SK_STR("a"), SK_STR("ab")));
    CHECK(sk_str_prefix(SK_STR("POST /hook"), SK_STR("POST ")));
    CHECK(!sk_str_prefix(SK_STR("PO"), SK_STR("POST")));
    CHECK(sk_str_suffix(SK_STR("a.json"), SK_STR(".json")));
    CHECK(!sk_str_suffix(SK_STR("json"), SK_STR(".json")));
}

static void test_slices(void)
{
    CHECK_MEM(sk_str_trim(SK_STR(" \t a b \r\n")), "a b");
    CHECK_MEM(sk_str_trim(SK_STR("   ")), "");
    CHECK_MEM(sk_str_sub(SK_STR("hello"), 1, 3), "ell");
    CHECK_MEM(sk_str_sub(SK_STR("hello"), 3, 100), "lo");
    CHECK_MEM(sk_str_sub(SK_STR("hello"), 9, 1), "");
    CHECK_INT(sk_str_find(SK_STR("a:b"), ':'), 1);
    CHECK(sk_str_find(SK_STR("ab"), ':') == SIZE_MAX);
    CHECK(sk_str_find(sk_str_n(NULL, 0), ':') == SIZE_MAX);
}

static void test_to_u64(void)
{
    uint64_t v = 0;
    CHECK(sk_str_to_u64(SK_STR("0"), &v) && v == 0);
    CHECK(sk_str_to_u64(SK_STR("65536"), &v) && v == 65536);
    CHECK(sk_str_to_u64(SK_STR("18446744073709551615"), &v) && v == UINT64_MAX);
    CHECK(!sk_str_to_u64(SK_STR("18446744073709551616"), &v));
    CHECK(!sk_str_to_u64(SK_STR(""), &v));
    CHECK(!sk_str_to_u64(SK_STR("12a"), &v));
    CHECK(!sk_str_to_u64(SK_STR("-1"), &v));
    CHECK(!sk_str_to_u64(SK_STR(" 1"), &v));
}

static void test_buf(void)
{
    sk_buf b = {0};
    CHECK_STR(sk_buf_cstr(&b), "");
    sk_buf_reserve(&b, 10); /* allocated but empty: still a C string */
    CHECK_STR(sk_buf_cstr(&b), "");
    for (int i = 0; i < 1000; i++)
        sk_buf_printf(&b, "%d,", i % 10);
    CHECK_INT(b.len, 2000);
    CHECK(b.ptr[b.len] == 0);
    sk_buf_consume(&b, 1990);
    CHECK_STR(b.ptr, "5,6,7,8,9,");
    sk_buf_consume(&b, 100);
    CHECK_INT(b.len, 0);
    CHECK_STR(sk_buf_cstr(&b), "");
    sk_buf_append(&b, "a\0b", 3);
    CHECK_INT(b.len, 3);
    CHECK(memcmp(b.ptr, "a\0b", 4) == 0);
    sk_buf_clear(&b);
    sk_buf_putc(&b, 'x');
    sk_buf_append_str(&b, SK_STR("yz"));
    CHECK_MEM(sk_buf_str(&b), "xyz");
    sk_buf_free(&b);
    CHECK(b.ptr == NULL && b.len == 0);
}

static void test_vec(void)
{
    SK_VEC(int) v = {0};
    for (int i = 0; i < 1000; i++)
        sk_vec_push(&v, i * 2);
    CHECK_INT(v.len, 1000);
    CHECK_INT(v.ptr[999], 1998);
    CHECK(v.cap >= 1000);
    sk_vec_free(&v);
    CHECK(v.ptr == NULL && v.len == 0 && v.cap == 0);
}

const sk_test sk_suite_str[] = {
    T(test_compare),
    T(test_slices),
    T(test_to_u64),
    T(test_buf),
    T(test_vec),
    {0},
};
