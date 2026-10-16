/* Links found in a buffer as addon-web-links 0.12.0's LinkComputer finds
 * them: its own offset cases from WebLinksAddon.test.ts on 40 columns,
 * with the ranges in its 1-based form, then the wrapped rows it joins
 * and where it stops. */
#include "../test.h"
#include "links_grid.h"

#include <stdio.h>

static void write_text(grid *g, const char *s)
{
    grid_write(g, s, strlen(s));
}

typedef struct {
    char uri[256];
    int sx, sy, ex, ey; /* xterm.js's range: 1-based, the end inclusive */
} hit;

/* The link under cell (col, row), from 0, as the Linkifier picks it from
 * the links of the row. */
static bool hover(grid *g, int col, int row, hit *h)
{
    sk_term_link_src src = grid_src(g);
    sk_term_links links = {0};
    sk_term_links_find(&src, row, &links);
    bool found = false;
    int pos = (row + 1) * g->cols + col + 1;
    for (size_t i = 0; i < links.len && !found; i++) {
        const sk_term_link *l = &links.ptr[i];
        hit c = {.sx = l->x0 + 1, .sy = l->y0 + 1, .ex = l->x1, .ey = l->y1 + 1};
        if (c.sy * g->cols + c.sx <= pos && pos <= c.ey * g->cols + c.ex) {
            snprintf(c.uri, sizeof c.uri, "%s", l->uri);
            *h = c;
            found = true;
        }
    }
    sk_term_links_free(&links);
    return found;
}

static void check_hover(int line, grid *g, int col, int row, const char *uri, int sx, int sy, int ex, int ey)
{
    hit h;
    if (!hover(g, col, row, &h)) {
        sk_test_fail(__FILE__, line, "no link at %d,%d", col, row);
        return;
    }
    if (strcmp(h.uri, uri) || h.sx != sx || h.sy != sy || h.ex != ex || h.ey != ey)
        sk_test_fail(__FILE__, line, "at %d,%d: %s %d,%d-%d,%d; want %s %d,%d-%d,%d", col, row, h.uri, h.sx, h.sy,
                     h.ex, h.ey, uri, sx, sy, ex, ey);
}

#define HOVER(g, col, row, uri, sx, sy, ex, ey) check_hover(__LINE__, g, col, row, uri, sx, sy, ex, ey)

static void test_all_half_width(void)
{
    grid g;
    grid_init(&g, 40);
    write_text(&g, "aaa http://example.com aaa http://example.com aaa");
    HOVER(&g, 5, 0, "http://example.com", 5, 1, 22, 1);
    HOVER(&g, 1, 1, "http://example.com", 28, 1, 5, 2);
    grid_free(&g);
}

static void test_url_after_full_width(void)
{
    grid g;
    grid_init(&g, 40);
    write_text(&g, "￥￥￥ http://example.com ￥￥￥ http://example.com aaa");
    HOVER(&g, 8, 0, "http://example.com", 8, 1, 25, 1);
    HOVER(&g, 1, 1, "http://example.com", 34, 1, 11, 2);
    grid_free(&g);
}

/* 위 does not fit in the last column: it wraps early, and the empty
 * cell it leaves is counted back. */
static void test_full_width_within_url_and_before(void)
{
    grid g;
    grid_init(&g, 40);
    write_text(&g, "￥￥￥ https://ko.wikipedia.org/wiki/위키백과:대문 aaa https://ko.wikipedia.org/wiki/위키백과:대문 ￥￥￥");
    HOVER(&g, 8, 0, "https://ko.wikipedia.org/wiki/위키백과:대문", 8, 1, 11, 2);
    HOVER(&g, 1, 1, "https://ko.wikipedia.org/wiki/위키백과:대문", 8, 1, 11, 2);
    HOVER(&g, 17, 1, "https://ko.wikipedia.org/wiki/위키백과:대문", 17, 2, 19, 3);
    grid_free(&g);
}

static void test_credentials_after_full_width_and_combining(void)
{
    grid g;
    grid_init(&g, 40);
    write_text(&g, "￥￥￥cafe\xcc\x81 http://test:password@example.com/some_path");
    HOVER(&g, 12, 0, "http://test:password@example.com/some_path", 12, 1, 13, 2);
    HOVER(&g, 5, 1, "http://test:password@example.com/some_path", 12, 1, 13, 2);
    grid_free(&g);
}

static void test_encoded_params(void)
{
    grid g;
    grid_init(&g, 40);
    write_text(&g, "￥￥￥cafe\xcc\x81 http://test:password@example.com/some_path?param=1%202%3");
    HOVER(&g, 12, 0, "http://test:password@example.com/some_path?param=1%202%3", 12, 1, 27, 2);
    HOVER(&g, 5, 1, "http://test:password@example.com/some_path?param=1%202%3", 12, 1, 27, 2);
    grid_free(&g);
}

static void test_upper_case_rows(void)
{
    static const char *const links[] = {
        "HTTP://EXAMPLE.COM",
        "HTTPS://Example.com",
        "HTTP://Example.com:80",
        "HTTP://Example.com:80/staysUpper",
        "HTTP://Ab:xY@abc.com:80/staysUpper",
    };
    grid g;
    grid_init(&g, 40);
    for (size_t i = 0; i < SK_LEN(links); i++) {
        char line[64];
        snprintf(line, sizeof line, "  %s  \r\n", links[i]);
        write_text(&g, line);
    }
    for (size_t i = 0; i < SK_LEN(links); i++)
        HOVER(&g, 3, (int)i, links[i], 3, (int)i + 1, 2 + (int)strlen(links[i]), (int)i + 1);
    grid_free(&g);
}

/* The addon's host-name rows, each link hovered where the test hovers. */
static void test_host_name_rows(void)
{
    static const char *const hosts[] = {"foo.com", "foo.de", "foo.com.au", "foo.io", "foo.co"};
    for (size_t i = 0; i < SK_LEN(hosts); i++) {
        const char *h = hosts[i];
        char text[512], want[7][64];
        snprintf(text, sizeof text,
                 "  http://%s  \r\n  http://%s/a~b#c~d?e~f  \r\n  http://%s/colon:test  \r\n"
                 "  http://%s/colon:test:  \r\n\"http://%s/\"\r\n'http://%s/'\r\nhttp://%s/subpath/+/id",
                 h, h, h, h, h, h, h);
        snprintf(want[0], 64, "http://%s", h);
        snprintf(want[1], 64, "http://%s/a~b#c~d?e~f", h);
        snprintf(want[2], 64, "http://%s/colon:test", h);
        snprintf(want[3], 64, "http://%s/colon:test", h);
        snprintf(want[4], 64, "http://%s/", h);
        snprintf(want[5], 64, "http://%s/", h);
        snprintf(want[6], 64, "http://%s/subpath/+/id", h);
        grid g;
        grid_init(&g, 40);
        write_text(&g, text);
        for (int row = 0; row < 7; row++) {
            int col = row < 4 ? 3 : row < 6 ? 2 : 1;
            int start = row < 4 ? 3 : row < 6 ? 2 : 1;
            HOVER(&g, col, row, want[row], start, row + 1, start - 1 + (int)strlen(want[row]), row + 1);
        }
        grid_free(&g);
    }
}

/* Every link of the joined rows comes back, the hovered row's or not. */
static void test_links_of_the_window(void)
{
    grid g;
    grid_init(&g, 10);
    write_text(&g, "http://a.b\"http://cd.e");
    sk_term_link_src src = grid_src(&g);
    sk_term_links links = {0};
    sk_term_links_find(&src, 2, &links);
    CHECK_INT(links.len, 2);
    if (links.len == 2) {
        CHECK_STR(links.ptr[0].uri, "http://a.b");
        CHECK_INT(links.ptr[0].x0, 0);
        CHECK_INT(links.ptr[0].y0, 0);
        CHECK_INT(links.ptr[0].x1, 0); /* it ends its row */
        CHECK_INT(links.ptr[0].y1, 1);
        CHECK_STR(links.ptr[1].uri, "http://cd.e");
        CHECK_INT(links.ptr[1].x0, 1);
        CHECK_INT(links.ptr[1].y0, 1);
        CHECK_INT(links.ptr[1].x1, 2);
        CHECK_INT(links.ptr[1].y1, 2);
    }
    sk_term_links_free(&links);
    grid_free(&g);
}

/* A link over three rows, from any of them. */
static void test_three_rows(void)
{
    grid g;
    grid_init(&g, 10);
    write_text(&g, "go https://example.com/abc ok");
    for (int row = 0; row < 3; row++)
        HOVER(&g, row == 0 ? 5 : 2, row, "https://example.com/abc", 4, 1, 6, 3);
    grid_free(&g);
}

/* Joining stops at a row with a space, and a wrapped row that starts
 * with a space joins nothing above. */
static void test_where_joining_stops(void)
{
    grid g;
    hit h;
    grid_init(&g, 11);
    write_text(&g, "http://abcd efg");
    CHECK(!hover(&g, 2, 1, &h));
    HOVER(&g, 2, 0, "http://abcd", 1, 1, 0, 2);
    grid_free(&g);

    grid_init(&g, 10);
    write_text(&g, "xxxxxxxxx http://a.co/bb y");
    HOVER(&g, 1, 1, "http://a.co/bb", 1, 2, 4, 3);
    HOVER(&g, 2, 2, "http://a.co/bb", 1, 2, 4, 3);
    grid_free(&g);

    /* Rows that do not wrap are never joined. */
    grid_init(&g, 10);
    write_text(&g, "http://abc\r\ndefgh");
    HOVER(&g, 3, 0, "http://abc", 1, 1, 0, 2);
    CHECK(!hover(&g, 2, 1, &h));
    grid_free(&g);
}

/* A wrapped first row, its start lost: the addon steps above row 0 and
 * keeps no link. */
static void test_wrapped_first_row(void)
{
    grid g;
    hit h;
    grid_init(&g, 20);
    write_text(&g, "http://example.com/");
    g.rows.ptr[0].wrapped = true;
    CHECK(!hover(&g, 2, 0, &h));
    grid_free(&g);
}

/* Joining upward stops past 2048 units, and the addon's top row is then
 * one above the text it joined: the link it maps comes out a row high. */
static void test_upward_limit(void)
{
    grid g;
    grid_init(&g, 100);
    for (int i = 0; i < 2500; i++)
        grid_print(&g, 'x');
    write_text(&g, "http://b.com/");
    for (int i = 0; i < 600; i++)
        grid_print(&g, 'y');
    CHECK_INT(g.rows.len, 32);
    sk_term_link_src src = grid_src(&g);
    sk_term_links links = {0};
    sk_term_links_find(&src, 31, &links);
    CHECK_INT(links.len, 1);
    if (links.len == 1) {
        CHECK_INT(links.ptr[0].x0, 0);
        CHECK_INT(links.ptr[0].y0, 24);
        CHECK_INT(links.ptr[0].x1, 13);
        CHECK_INT(links.ptr[0].y1, 30);
        CHECK_INT(strlen(links.ptr[0].uri), 613);
    }
    sk_term_links_free(&links);
    /* Row 25, the link's own, also reaches the limit going up. */
    sk_term_links_find(&src, 25, &links);
    CHECK_INT(links.len, 1);
    if (links.len == 1)
        CHECK_INT(links.ptr[0].y0, 24);
    sk_term_links_free(&links);
    grid_free(&g);

    /* Joined up to row 0 within the limit, the link is where it is. */
    grid_init(&g, 100);
    for (int i = 0; i < 1000; i++)
        grid_print(&g, 'x');
    write_text(&g, "http://b.com/");
    for (int i = 0; i < 600; i++)
        grid_print(&g, 'y');
    src = grid_src(&g);
    sk_term_links_find(&src, 16, &links);
    CHECK_INT(links.len, 1);
    if (links.len == 1) {
        CHECK_INT(links.ptr[0].x0, 0);
        CHECK_INT(links.ptr[0].y0, 10);
        CHECK_INT(links.ptr[0].x1, 13);
        CHECK_INT(links.ptr[0].y1, 16);
    }
    sk_term_links_free(&links);
    grid_free(&g);
}

const sk_test sk_suite_links_buffer[] = {
    T(test_all_half_width),
    T(test_url_after_full_width),
    T(test_full_width_within_url_and_before),
    T(test_credentials_after_full_width_and_combining),
    T(test_encoded_params),
    T(test_upper_case_rows),
    T(test_host_name_rows),
    T(test_links_of_the_window),
    T(test_three_rows),
    T(test_where_joining_stops),
    T(test_wrapped_first_row),
    T(test_upward_limit),
    {0},
};
