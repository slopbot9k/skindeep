#include "../../src/base/map.h"
#include "../test.h"

#include <stdint.h>
#include <stdlib.h>

static void test_siphash_vectors(void)
{
    /* The reference implementation's vectors: key 00..0f, message 00..n-1. */
    unsigned char msg[16];
    for (int i = 0; i < 16; i++)
        msg[i] = (unsigned char)i;
    uint64_t k0 = 0x0706050403020100ULL, k1 = 0x0f0e0d0c0b0a0908ULL;
    CHECK(sk_siphash(k0, k1, msg, 0) == 0x726fdb47dd0e0e31ULL);
    CHECK(sk_siphash(k0, k1, msg, 1) == 0x74f839c593dc67fdULL);
    CHECK(sk_siphash(k0, k1, msg, 15) == 0xa129ca6149be45e5ULL);
}

static void test_put_get_del(void)
{
    sk_map m;
    sk_map_init(&m);
    int a = 1, b = 2;
    CHECK(sk_map_get(&m, SK_STR("a")) == NULL);
    *sk_map_put(&m, SK_STR("a")) = &a;
    *sk_map_put(&m, SK_STR("b")) = &b;
    *sk_map_put(&m, SK_STR("")) = &b;
    CHECK(sk_map_get(&m, SK_STR("a")) == &a);
    CHECK(sk_map_get(&m, SK_STR("b")) == &b);
    CHECK(sk_map_get(&m, SK_STR("")) == &b);
    CHECK(sk_map_has(&m, SK_STR("a")));
    CHECK_INT(m.len, 3);
    *sk_map_put(&m, SK_STR("a")) = &b; /* replaces */
    CHECK(sk_map_get(&m, SK_STR("a")) == &b);
    CHECK_INT(m.len, 3);
    CHECK(sk_map_del(&m, SK_STR("a")) == &b);
    CHECK(sk_map_del(&m, SK_STR("a")) == NULL);
    CHECK(!sk_map_has(&m, SK_STR("a")));
    CHECK_INT(m.len, 2);
    sk_map_free(&m);
}

static void test_binary_keys(void)
{
    sk_map m;
    sk_map_init(&m);
    int x = 0;
    *sk_map_put(&m, sk_str_n("a\0b", 3)) = &x;
    CHECK(sk_map_get(&m, sk_str_n("a\0b", 3)) == &x);
    CHECK(sk_map_get(&m, sk_str_n("a\0c", 3)) == NULL);
    CHECK(sk_map_get(&m, SK_STR("a")) == NULL);
    sk_map_free(&m);
}

static void test_many_with_deletes(void)
{
    sk_map m;
    sk_map_init(&m);
    enum { N = 20000 };
    static int vals[N];
    char key[32];
    for (int i = 0; i < N; i++) {
        vals[i] = i;
        snprintf(key, sizeof key, "key-%d", i);
        *sk_map_put(&m, sk_str_c(key)) = &vals[i];
    }
    for (int i = 0; i < N; i += 3) {
        snprintf(key, sizeof key, "key-%d", i);
        CHECK(sk_map_del(&m, sk_str_c(key)) == &vals[i]);
    }
    int found = 0, wrong = 0;
    for (int i = 0; i < N; i++) {
        snprintf(key, sizeof key, "key-%d", i);
        int *v = sk_map_get(&m, sk_str_c(key));
        if (i % 3 == 0)
            wrong += v != NULL;
        else
            found += v == &vals[i];
    }
    CHECK_INT(wrong, 0);
    CHECK_INT(found, N - (N + 2) / 3);
    CHECK_INT(m.len, (size_t)found);
    size_t it = 0, seen = 0;
    sk_str k;
    void *v;
    while (sk_map_next(&m, &it, &k, &v))
        seen++;
    CHECK_INT(seen, m.len);
    sk_map_free(&m);
}

const sk_test sk_suite_map[] = {
    T(test_siphash_vectors),
    T(test_put_get_del),
    T(test_binary_keys),
    T(test_many_with_deletes),
    {0},
};
