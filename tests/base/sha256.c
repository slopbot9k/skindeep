#include "../../src/base/sha256.h"
#include "../test.h"

#include <stdlib.h>

static void test_vectors(void)
{
    char hex[65];
    sk_sha256_hex("", 0, hex);
    CHECK_STR(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    sk_sha256_hex("abc", 3, hex);
    CHECK_STR(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const char *two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    sk_sha256_hex(two, strlen(two), hex);
    CHECK_STR(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

static void test_million_a_in_pieces(void)
{
    char *a = malloc(1000000);
    memset(a, 'a', 1000000);
    sk_sha256 c;
    unsigned char d[32], whole[32];
    sk_sha256_init(&c);
    /* Pieces of awkward sizes cross every block boundary case. */
    size_t at = 0, step = 1;
    while (at < 1000000) {
        size_t n = SK_MIN(step, 1000000 - at);
        sk_sha256_update(&c, a + at, n);
        at += n;
        step = step * 7 % 191 + 1;
    }
    sk_sha256_final(&c, d);
    sk_sha256_init(&c);
    sk_sha256_update(&c, a, 1000000);
    sk_sha256_final(&c, whole);
    CHECK(memcmp(d, whole, 32) == 0);
    char hex[65];
    sk_sha256_hex(a, 1000000, hex);
    CHECK_STR(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    free(a);
}

static void test_padding_edges(void)
{
    /* 55, 56 and 64 bytes put the length in the same block, the next
     * one, and after a full block. */
    const char *expect[] = {
        "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318",
        "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a",
        "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb",
    };
    size_t lens[] = {55, 56, 64};
    char a[64];
    memset(a, 'a', sizeof a);
    for (size_t i = 0; i < SK_LEN(lens); i++) {
        char hex[65];
        sk_sha256_hex(a, lens[i], hex);
        CHECK_STR(hex, expect[i]);
    }
}

const sk_test sk_suite_sha256[] = {
    T(test_vectors),
    T(test_million_a_in_pieces),
    T(test_padding_edges),
    {0},
};
