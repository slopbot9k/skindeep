/* UTF-8: decoding walks every byte exactly once, sanitizing always gives
 * valid text and leaves valid text alone, and a chunked reader holds
 * back at most an unfinished character of 3 bytes. */
#include "../../src/base/utf8.h"
#include "fuzz.h"

#include <stdlib.h>
#include <string.h>

const char *const sk_fuzz_dict[] = {
    "\xC3\xA9", "\xE2\x82\xAC", "\xF0\x9F\x98\x80", "\xED\xA0\x80", "\xE0\x80", "\xF4\x90", NULL,
};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    const char *s = (const char *)data;
    size_t walked = 0;
    while (walked < size) {
        uint32_t cp;
        size_t k = sk_utf8_decode(s + walked, size - walked, &cp, NULL);
        if (k == 0 || k > 4 || cp > 0x10FFFF)
            abort();
        walked += k;
    }
    if (walked != size)
        abort();

    sk_buf out = {0};
    sk_utf8_sanitize(&out, s, size);
    if (!sk_utf8_valid(out.ptr, out.len))
        abort();
    if (sk_utf8_valid(s, size) && (out.len != size || (size && memcmp(out.ptr, s, size))))
        abort();
    sk_buf_free(&out);

    size_t whole = sk_utf8_whole(s, size);
    if (whole > size || size - whole > 3)
        abort();
    return 0;
}
