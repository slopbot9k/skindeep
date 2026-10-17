/* What a drop of files over XDND brings: the paths a text/uri-list names.
 * platform/x11/xui.c holds the protocol itself. */
#include "x11.h"

#include "../../base/utf8.h"

#include <string.h>

static int hex(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* The path of a file:// URI whose host is empty or localhost, decoded
 * into out; false for any other URI, and for one with a bad escape, an
 * escaped NUL or a fragment, which no file's path holds. The scheme and
 * the host are read in any case, as URIs have them. */
static bool file_path(sk_str uri, sk_buf *out)
{
    if (!sk_str_eq_nocase(sk_str_sub(uri, 0, 7), SK_STR("file://")))
        return false;
    sk_str rest = sk_str_sub(uri, 7, uri.len);
    size_t slash = sk_str_find(rest, '/');
    if (slash == SIZE_MAX)
        return false;
    sk_str host = sk_str_sub(rest, 0, slash), path = sk_str_sub(rest, slash, rest.len);
    if (host.len && !sk_str_eq_nocase(host, SK_STR("localhost")))
        return false;
    if (sk_str_find(path, '#') != SIZE_MAX)
        return false;
    for (size_t i = 0; i < path.len; i++) {
        char c = path.ptr[i];
        if (c == '%') {
            int hi = i + 2 < path.len ? hex(path.ptr[i + 1]) : -1;
            int lo = i + 2 < path.len ? hex(path.ptr[i + 2]) : -1;
            if (hi < 0 || lo < 0 || (hi == 0 && lo == 0))
                return false;
            c = (char)(hi << 4 | lo);
            i += 2;
        }
        sk_buf_putc(out, c);
    }
    return true;
}

void sk_x11_uri_list_files(const char *s, size_t n, sk_strv *out)
{
    sk_buf path = {0}, text = {0};
    for (size_t at = 0; at < n;) {
        const char *nl = memchr(s + at, '\n', n - at);
        size_t end = nl ? (size_t)(nl - s) : n, next = nl ? end + 1 : n;
        if (end > at && s[end - 1] == '\r')
            end--;
        sk_buf_clear(&path);
        if (end > at && s[at] != '#' && file_path(sk_str_n(s + at, end - at), &path)) {
            sk_buf_clear(&text);
            sk_utf8_sanitize(&text, path.ptr, path.len);
            sk_strv_push_n(out, text.ptr, text.len);
        }
        at = next;
    }
    sk_buf_free(&path);
    sk_buf_free(&text);
}
