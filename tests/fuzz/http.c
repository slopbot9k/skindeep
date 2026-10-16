/* HTTP: the parser ends in the same state with the same body however the
 * bytes are split, never keeps more body than its cap, and never claims
 * more bytes than it was given. The first byte picks the chunk size. */
#include "../../src/base/http.h"
#include "fuzz.h"

#include <stdlib.h>
#include <string.h>

const char *const sk_fuzz_dict[] = {
    "POST /hook HTTP/1.1\r\n", "Content-Length: ", "content-length: 0\r\n",
    "Transfer-Encoding: chunked\r\n", "\r\n\r\n", "\n\n", "HTTP/1.0", "18446744073709551615", NULL,
};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (!size)
        return 0;
    size_t chunk = 1 + data[0] % 64;
    const char *p = (const char *)data + 1;
    size_t n = size - 1;

    sk_http_req whole;
    sk_http_req_init(&whole, 64);
    size_t used = sk_http_req_feed(&whole, p, n);
    if (used > n)
        abort();

    sk_http_req parts;
    sk_http_req_init(&parts, 64);
    size_t at = 0;
    while (at < n && parts.state != SK_HTTP_DONE && parts.state != SK_HTTP_ERROR) {
        size_t k = n - at < chunk ? n - at : chunk;
        size_t u = sk_http_req_feed(&parts, p + at, k);
        if (u > k || (u == 0 && k && parts.state != SK_HTTP_DONE && parts.state != SK_HTTP_ERROR))
            abort();
        at += u;
        if (u < k)
            break;
    }
    if (parts.state != whole.state || at != used)
        abort();
    if (whole.body.len > 64 || whole.body.len != parts.body.len ||
        (whole.body.len && memcmp(whole.body.ptr, parts.body.ptr, whole.body.len)))
        abort();
    if (whole.state == SK_HTTP_DONE && !whole.oversize && whole.body.len != whole.content_length)
        abort();
    sk_http_req_free(&whole);
    sk_http_req_free(&parts);
    return 0;
}
