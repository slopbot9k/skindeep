/* base64url: any bytes survive encoding and decoding, and whatever
 * decodes encodes back to text that decodes to the same bytes. */
#include "../../src/base/base64.h"
#include "fuzz.h"

#include <stdlib.h>
#include <string.h>

const char *const sk_fuzz_dict[] = {"=", "==", "-", "_", "+", "/", NULL};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    sk_buf enc = {0}, dec = {0};
    sk_base64url_encode(&enc, data, size);
    if (!sk_base64url_decode(&dec, sk_buf_str(&enc)) || dec.len != size ||
        (size && memcmp(dec.ptr, data, size)))
        abort();

    sk_buf first = {0}, again = {0}, second = {0};
    if (sk_base64url_decode(&first, sk_str_n((const char *)data, size))) {
        sk_base64url_encode(&again, first.ptr, first.len);
        if (!sk_base64url_decode(&second, sk_buf_str(&again)) || second.len != first.len ||
            (first.len && memcmp(first.ptr, second.ptr, first.len)))
            abort();
    }
    sk_buf_free(&enc);
    sk_buf_free(&dec);
    sk_buf_free(&first);
    sk_buf_free(&again);
    sk_buf_free(&second);
    return 0;
}
