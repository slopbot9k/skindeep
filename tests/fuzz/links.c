/* Links: the URL scan walks a line once, each link it gives begins with
 * the scheme, ends where the regex may end, and is found again alone;
 * every link of a buffer lies in it, its end after its start. The first
 * byte picks the width, the rest is the text written to the buffer. */
#include "../term/links_grid.h"
#include "fuzz.h"

#include <stdlib.h>

const char *const sk_fuzz_dict[] = {
    "http://", "https://", "HTTP://", "HTTPS://", "xn--", "[::1]", "[::ffff:1.2.3.4]", ":8080", "user:pass@",
    "%41", "127.0.0.1", "0x7f.1", "\xe2\x84\xaa", "\xef\xbf\xa5", "\xcc\x81", "\r\n", " ", "\"", ".", ":", NULL,
};

static bool starts(const uint32_t *s, size_t n, const char *w)
{
    for (size_t i = 0; w[i]; i++)
        if (i >= n || s[i] != (uint32_t)w[i])
            return false;
    return true;
}

static void check_line(const uint32_t *s, size_t n)
{
    size_t from = 0, start, end, last = 0;
    while (sk_term_url_next(s, n, &from, &start, &end)) {
        if (start < last || end < start + 8 || end > from || from > n)
            abort();
        if (!starts(s + start, end - start, "http") && !starts(s + start, end - start, "HTTP"))
            abort();
        uint32_t c = s[end - 1];
        if (c == ' ' || c == '.' || c == ',' || c == ':' || c == ')' || c == ']' || c == '"')
            abort();
        size_t f2 = 0, s2, e2;
        if (!sk_term_url_next(s + start, end - start, &f2, &s2, &e2) || s2 != 0 || e2 != end - start)
            abort();
        last = end;
    }
    if (from != n)
        abort();
}

static void check_buffer(grid *g)
{
    sk_term_link_src src = grid_src(g);
    int rows = (int)g->rows.len;
    int step = rows > 64 ? rows / 64 : 1;
    for (int y = 0; y < rows; y += step) {
        sk_term_links links = {0};
        sk_term_links_find(&src, y, &links);
        for (size_t i = 0; i < links.len; i++) {
            const sk_term_link *l = &links.ptr[i];
            size_t len = strlen(l->uri);
            if (len < 8 || (strncmp(l->uri, "http", 4) && strncmp(l->uri, "HTTP", 4)) ||
                !sk_utf8_valid(l->uri, len))
                abort();
            if (l->x0 < 0 || l->x0 >= g->cols || l->y0 < 0 || l->y0 >= rows)
                abort();
            if (l->x1 < 0 || l->x1 > g->cols || l->y1 < l->y0 || l->y1 > rows)
                abort();
            if (l->y1 == l->y0 && l->x1 <= l->x0)
                abort();
        }
        sk_term_links_free(&links);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (!size)
        return 0;
    int cols = 1 + data[0] % 48;
    const char *text = (const char *)data + 1;
    size_t n = size - 1;

    SK_VEC(uint32_t) cps = {0};
    for (size_t i = 0; i < n;) {
        uint32_t cp;
        i += sk_utf8_decode(text + i, n - i, &cp, NULL);
        sk_vec_push(&cps, cp);
    }
    check_line(cps.ptr, cps.len);

    grid g;
    grid_init(&g, cols);
    for (size_t i = 0; i < cps.len; i++)
        grid_print(&g, cps.ptr[i]);
    check_buffer(&g);
    grid_free(&g);
    sk_vec_free(&cps);
    return 0;
}
