#include "../../src/base/arena.h"
#include "../test.h"

#include <stdalign.h>

static void test_aligned_and_distinct(void)
{
    sk_arena a;
    sk_arena_init(&a, 256);
    char *prev = NULL;
    for (size_t n = 1; n < 200; n++) {
        char *p = sk_arena_alloc(&a, n);
        CHECK((uintptr_t)p % alignof(max_align_t) == 0);
        memset(p, (int)n, n);
        CHECK(p != prev);
        prev = p;
    }
    sk_arena_free(&a);
}

static void test_large_requests(void)
{
    sk_arena a;
    sk_arena_init(&a, 1024);
    char *small = sk_arena_alloc(&a, 16);
    char *big = sk_arena_alloc(&a, 100000);
    char *after = sk_arena_alloc(&a, 16);
    memset(big, 7, 100000);
    memset(small, 1, 16);
    memset(after, 2, 16);
    CHECK(big[99999] == 7);
    /* A large request does not take the current block's place. */
    CHECK(after - small < 1024);
    sk_arena_free(&a);
}

static void test_strndup_and_zalloc(void)
{
    sk_arena a;
    sk_arena_init(&a, 0);
    char *s = sk_arena_strndup(&a, "hello world", 5);
    CHECK_STR(s, "hello");
    unsigned char *z = sk_arena_zalloc(&a, 64);
    int nonzero = 0;
    for (int i = 0; i < 64; i++)
        nonzero += z[i] != 0;
    CHECK_INT(nonzero, 0);
    CHECK_STR(sk_arena_strndup(&a, NULL, 0), "");
    sk_arena_free(&a);
}

static void test_reset_reuses(void)
{
    sk_arena a;
    sk_arena_init(&a, 512);
    for (int round = 0; round < 3; round++) {
        for (int i = 0; i < 100; i++)
            memset(sk_arena_alloc(&a, 100), round, 100);
        sk_arena_reset(&a);
    }
    char *p = sk_arena_alloc(&a, 8);
    CHECK(p != NULL);
    sk_arena_free(&a);
    sk_arena_free(&a); /* twice is harmless */
}

const sk_test sk_suite_arena[] = {
    T(test_aligned_and_distinct),
    T(test_large_requests),
    T(test_strndup_and_zalloc),
    T(test_reset_reuses),
    {0},
};
