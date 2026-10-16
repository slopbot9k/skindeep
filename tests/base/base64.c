#include "../../src/base/base64.h"
#include "../test.h"

static void test_rfc4648_vectors(void)
{
    const char *in[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
    const char *out[] = {"", "Zg", "Zm8", "Zm9v", "Zm9vYg", "Zm9vYmE", "Zm9vYmFy"};
    for (size_t i = 0; i < SK_LEN(in); i++) {
        sk_buf b = {0};
        sk_base64url_encode(&b, in[i], strlen(in[i]));
        CHECK_STR(sk_buf_cstr(&b), out[i]);
        sk_buf_clear(&b);
        CHECK(sk_base64url_decode(&b, sk_str_c(out[i])));
        CHECK_STR(sk_buf_cstr(&b), in[i]);
        sk_buf_free(&b);
    }
}

static void test_url_alphabet(void)
{
    sk_buf b = {0};
    sk_base64url_encode(&b, "\xFB\xFF", 2);
    CHECK_STR(b.ptr, "-_8"); /* standard base64 would be "+/8=" */
    sk_buf_clear(&b);
    CHECK(sk_base64url_decode(&b, SK_STR("+/8=")));
    CHECK(b.len == 2 && memcmp(b.ptr, "\xFB\xFF", 2) == 0);
    sk_buf_free(&b);
}

static void test_rejects(void)
{
    const char *bad[] = {"Z", "Zm9v!", "Zg=", "Zm9vY===", "Z===", "a b"};
    for (size_t i = 0; i < SK_LEN(bad); i++) {
        sk_buf b = {0};
        sk_buf_append_c(&b, "keep");
        CHECK(!sk_base64url_decode(&b, sk_str_c(bad[i])));
        CHECK_STR(sk_buf_cstr(&b), "keep");
        sk_buf_free(&b);
    }
}

/* The statusline relay's chain argument: a shell command, with quotes,
 * a pipe and UTF-8. */
static void test_round_trip_a_command(void)
{
    const char *cmd = "bash -c 'printf \"%s\" \"$(cat)\" | jq -r .model.display_name' # caf\xC3\xA9";
    sk_buf enc = {0}, dec = {0};
    sk_base64url_encode(&enc, cmd, strlen(cmd));
    CHECK(strpbrk(enc.ptr, "+/=") == NULL);
    CHECK(sk_base64url_decode(&dec, sk_buf_str(&enc)));
    CHECK_STR(dec.ptr, cmd);
    sk_buf_free(&enc);
    sk_buf_free(&dec);
}

const sk_test sk_suite_base64[] = {
    T(test_rfc4648_vectors),
    T(test_url_alphabet),
    T(test_rejects),
    T(test_round_trip_a_command),
    {0},
};
