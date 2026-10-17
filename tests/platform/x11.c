/* X11's wire protocol without a display (decision 0036): the display
 * name, Xauthority, the bytes of requests, the setup, events, Xft.dpi,
 * INCR's chunks, STRING's Latin-1, the files of a text/uri-list, and a
 * connection to the fake server. */
#include "../../src/platform/x11/x11.h"
#include "../test.h"
#include "x11_fake.h"

#include <stdio.h>
#include <unistd.h>

static void test_display_names(void)
{
    sk_x11_display d;
    char *err = NULL;
    CHECK(sk_x11_display_parse(":0", &d, &err));
    CHECK_STR(d.number, "0");
    CHECK_INT(d.screen, 0);
#ifdef __linux__
    CHECK_INT(d.n_sockets, 2);
    CHECK_STR(d.sockets[0], "@/tmp/.X11-unix/X0");
#else
    CHECK_INT(d.n_sockets, 1);
#endif
    CHECK_STR(d.sockets[d.n_sockets - 1], "/tmp/.X11-unix/X0");
    sk_x11_display_free(&d);

    CHECK(sk_x11_display_parse("unix:12.1", &d, &err));
    CHECK_STR(d.number, "12");
    CHECK_INT(d.screen, 1);
    CHECK_STR(d.sockets[d.n_sockets - 1], "/tmp/.X11-unix/X12");
    sk_x11_display_free(&d);

    CHECK(sk_x11_display_parse("/tmp/x/sock:3", &d, &err));
    CHECK_STR(d.number, "3");
    CHECK_INT(d.n_sockets, 2);
    CHECK_STR(d.sockets[0], "/tmp/x/sock");
    CHECK_STR(d.sockets[1], "/tmp/x/sock:3");
    sk_x11_display_free(&d);

    const char *bad[] = {"", "0", ":", ":x", ":1.", "localhost:10.0", "host:0"};
    for (size_t i = 0; i < SK_LEN(bad); i++) {
        err = NULL;
        CHECK(!sk_x11_display_parse(bad[i], &d, &err));
        CHECK(err != NULL);
        if (i >= 5)
            CHECK(err && strstr(err, "TCP"));
        free(err);
    }
    err = NULL;
    CHECK(!sk_x11_display_parse(NULL, &d, &err));
    free(err);
}

static void put_field(sk_buf *b, const char *s, size_t n)
{
    sk_buf_putc(b, (char)(n >> 8));
    sk_buf_putc(b, (char)n);
    sk_buf_append(b, s, n);
}

static void put_entry(sk_buf *b, unsigned family, const char *addr, const char *num,
                      const char *name, const char *data)
{
    sk_buf_putc(b, (char)(family >> 8));
    sk_buf_putc(b, (char)family);
    put_field(b, addr, strlen(addr));
    put_field(b, num, strlen(num));
    put_field(b, name, strlen(name));
    put_field(b, data, strlen(data));
}

static void test_xauthority_matches_a_local_display(void)
{
    sk_buf b = {0};
    put_entry(&b, 0, "\x7f\0\0\x01", "0", "MIT-MAGIC-COOKIE-1", "internet........");
    put_entry(&b, 256, "otherhost", "0", "MIT-MAGIC-COOKIE-1", "otherhost.......");
    put_entry(&b, 256, "myhost", "1", "MIT-MAGIC-COOKIE-1", "display one.....");
    put_entry(&b, 256, "myhost", "0", "XDM-AUTHORIZATION-1", "another scheme..");
    put_entry(&b, 256, "myhost", "0", "MIT-MAGIC-COOKIE-1", "the right one...");
    put_entry(&b, 65535, "", "", "MIT-MAGIC-COOKIE-1", "wild............");
    sk_x11_cookie k;
    CHECK(sk_x11_xauth_find((const uint8_t *)b.ptr, b.len, "myhost", "0", &k));
    CHECK_INT(k.name_len, 18);
    CHECK(!memcmp(k.name, "MIT-MAGIC-COOKIE-1", 18));
    CHECK_INT(k.data_len, 16);
    CHECK(!memcmp(k.data, "the right one...", 16));
    CHECK(sk_x11_xauth_find((const uint8_t *)b.ptr, b.len, "myhost", "1", &k));
    CHECK(!memcmp(k.data, "display one.....", 16));
    /* Another host, or another display: the wildcard. */
    CHECK(sk_x11_xauth_find((const uint8_t *)b.ptr, b.len, "elsewhere", "5", &k));
    CHECK(!memcmp(k.data, "wild............", 16));
    /* Cut short: nothing past the cut. */
    CHECK(!sk_x11_xauth_find((const uint8_t *)b.ptr, 30, "myhost", "0", &k));
    sk_buf_free(&b);
}

static void test_xauthority_file_and_its_variable(void)
{
    char host[256];
    CHECK(gethostname(host, sizeof host) == 0);
    host[sizeof host - 1] = 0;
    sk_buf b = {0};
    put_entry(&b, 256, host, "7", "MIT-MAGIC-COOKIE-1", "0123456789abcdef");
    sk_buf path = {0};
    sk_buf_printf(&path, "%s/Xauthority", sk_test_tmpdir());
    FILE *f = fopen(path.ptr, "wb");
    CHECK(f && fwrite(b.ptr, 1, b.len, f) == b.len);
    if (f)
        fclose(f);
    char *saved = getenv("XAUTHORITY") ? sk_xstrdup(getenv("XAUTHORITY")) : NULL;
    setenv("XAUTHORITY", path.ptr, 1);
    sk_x11_cookie k;
    CHECK(sk_x11_xauth_load("7", &k));
    CHECK(k.data_len == 16 && !memcmp(k.data, "0123456789abcdef", 16));
    CHECK(!sk_x11_xauth_load("8", &k));
    if (saved)
        setenv("XAUTHORITY", saved, 1);
    else
        unsetenv("XAUTHORITY");
    free(saved);
    sk_buf_free(&path);
    sk_buf_free(&b);
}

static bool bytes_are(const sk_buf *b, size_t at, const uint8_t *want, size_t n)
{
    if (b->len < at + n)
        return false;
    for (size_t i = 0; i < n; i++) {
        if ((uint8_t)b->ptr[at + i] != want[i]) {
            fprintf(stderr, "    byte %zu: %02x, not %02x\n", at + i, (uint8_t)b->ptr[at + i],
                    want[i]);
            return false;
        }
    }
    return true;
}

static void test_setup_request_bytes(void)
{
    sk_x11_cookie k = {"MIT-MAGIC-COOKIE-1",
                       {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16},
                       18,
                       16};
    sk_buf b = {0};
    sk_x11_setup_request(&b, &k);
    static const uint8_t head[12] = {'l', 0, 11, 0, 0, 0, 18, 0, 16, 0, 0, 0};
    CHECK_INT(b.len, 12 + 20 + 16);
    CHECK(bytes_are(&b, 0, head, 12));
    CHECK(!memcmp(b.ptr + 12, "MIT-MAGIC-COOKIE-1\0\0", 20));
    CHECK(bytes_are(&b, 32, k.data, 16));
    sk_buf_free(&b);
    sk_x11_setup_request(&b, NULL);
    static const uint8_t bare[12] = {'l', 0, 11, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    CHECK_INT(b.len, 12);
    CHECK(bytes_are(&b, 0, bare, 12));
    sk_buf_free(&b);
}

static void test_request_bytes(void)
{
    sk_x11 c = {0};
    c.max_request = 65535 * 4;
    CHECK_INT(sk_x11_intern_atom(&c, "WM_PROTOCOLS", false), 1);
    static const uint8_t intern[] = {16, 0, 5, 0, 12, 0, 0, 0, 'W', 'M', '_', 'P', 'R', 'O',
                                     'T', 'O', 'C', 'O', 'L', 'S'};
    CHECK_INT(c.out.len, sizeof intern);
    CHECK(bytes_are(&c.out, 0, intern, sizeof intern));
    sk_buf_clear(&c.out);

    CHECK_INT(sk_x11_change_property(&c, 0, 0x200001, 39, 31, 8, "hi!", 3), 2);
    static const uint8_t change[] = {18, 0, 7, 0, 1, 0, 0x20, 0, 39, 0, 0, 0, 31, 0, 0, 0,
                                     8, 0, 0, 0, 3, 0, 0, 0, 'h', 'i', '!', 0};
    CHECK_INT(c.out.len, sizeof change);
    CHECK(bytes_are(&c.out, 0, change, sizeof change));
    sk_buf_clear(&c.out);

    uint32_t values[] = {0, 1, 0x628077};
    sk_x11_create_window(&c, 24, 0x200002, 0x100, 0, 0, 640, 480, 1, 0x21,
                         SK_X11_CW_BORDER_PIXEL | SK_X11_CW_BIT_GRAVITY | SK_X11_CW_EVENT_MASK,
                         values);
    static const uint8_t create[] = {1, 24, 11, 0, 2, 0, 0x20, 0, 0, 1, 0, 0, 0, 0, 0, 0,
                                     0x80, 2, 0xe0, 1, 0, 0, 1, 0, 0x21, 0, 0, 0, 0x18, 8, 0, 0,
                                     0, 0, 0, 0, 1, 0, 0, 0, 0x77, 0x80, 0x62, 0};
    CHECK_INT(c.out.len, sizeof create);
    CHECK(bytes_are(&c.out, 0, create, sizeof create));
    sk_buf_clear(&c.out);

    uint8_t ev[32] = {33, 32};
    sk_x11_send_event(&c, false, 0x100, 0x180000, ev);
    static const uint8_t send[] = {25, 0, 11, 0, 0, 1, 0, 0, 0, 0, 0x18, 0, 33, 32};
    CHECK_INT(c.out.len, 44);
    CHECK(bytes_are(&c.out, 0, send, sizeof send));
    sk_buf_clear(&c.out);

    /* Two pixels, swapped for a server of the other byte order. */
    uint32_t px[] = {0xff112233, 0x80445566};
    sk_x11_put_image(&c, 0x200003, 0x200004, 5, 6, 2, 1, 24, px, 2, true);
    static const uint8_t put[] = {72, 2, 8, 0, 3, 0, 0x20, 0, 4, 0, 0x20, 0, 2, 0, 1, 0,
                                  5, 0, 6, 0, 0, 24, 0, 0, 0xff, 0x11, 0x22, 0x33, 0x80, 0x44,
                                  0x55, 0x66};
    CHECK_INT(c.out.len, sizeof put);
    CHECK(bytes_are(&c.out, 0, put, sizeof put));
    sk_buf_clear(&c.out);

    c.shm_opcode = 131;
    sk_x11_shm_put_image(&c, 0x200003, 0x200004, 640, 480, 10, 20, 30, 40, 10, 20, 24, true,
                         0x200005, 0);
    static const uint8_t shm[] = {131, 3, 10, 0, 3, 0, 0x20, 0, 4, 0, 0x20, 0, 0x80, 2, 0xe0, 1,
                                  10, 0, 20, 0, 30, 0, 40, 0, 10, 0, 20, 0, 24, 2, 1, 0,
                                  5, 0, 0x20, 0, 0, 0, 0, 0};
    CHECK_INT(c.out.len, sizeof shm);
    CHECK(bytes_are(&c.out, 0, shm, sizeof shm));
    sk_buf_clear(&c.out);

    CHECK_INT(sk_x11_get_keyboard_mapping(&c, 8, 248), 7);
    static const uint8_t keymap[] = {101, 0, 2, 0, 8, 248, 0, 0};
    CHECK(bytes_are(&c.out, 0, keymap, sizeof keymap));
    CHECK_INT(c.seq, 7);
    sk_buf_free(&c.out);
}

static void test_big_requests_form(void)
{
    sk_x11 c = {0};
    c.max_request = 0x3fffff * 4;
    c.big_opcode = 133;
    size_t n = 300000;
    char *data = sk_xcalloc(n, 1);
    data[0] = 'a';
    data[n - 1] = 'z';
    sk_x11_change_property(&c, 0, 1, 2, 31, 8, data, (uint32_t)n);
    size_t words = (24 + n) / 4 + 1;
    CHECK_INT(c.out.len, 24 + n + 4);
    CHECK_INT(sk_x11_get16((const uint8_t *)c.out.ptr + 2), 0);
    CHECK_INT(sk_x11_get32((const uint8_t *)c.out.ptr + 4), words);
    CHECK_INT(sk_x11_get32((const uint8_t *)c.out.ptr + 8), 1); /* the window, moved along */
    CHECK_INT(c.out.ptr[28], 'a');
    CHECK_INT(c.out.ptr[28 + n - 1], 'z');
    free(data);
    sk_buf_free(&c.out);
}

/* A setup whose root is 8-bit PseudoColor, with a 24-bit TrueColor
 * visual beside it. */
static size_t craft_setup(uint8_t *b, bool with_truecolor)
{
    memset(b, 0, 256);
    b[0] = 1;
    b[2] = 11;
    sk_x11_set32(b + 12, 0x400000);
    sk_x11_set32(b + 16, 0x3fffff);
    sk_x11_set16(b + 24, 1);
    sk_x11_set16(b + 26, 4096);
    b[28] = 1;
    b[29] = 2;
    b[30] = 1; /* MSBFirst */
    b[34] = 8;
    b[35] = 255;
    b[40] = 'v';
    uint8_t *p = b + 44;
    memcpy(p, "\x08\x08\x20", 3);
    p += 8;
    memcpy(p, "\x18\x20\x20", 3);
    p += 8;
    sk_x11_set32(p, 0x55);
    sk_x11_set32(p + 32, 0x40);
    p[38] = 8;
    p[39] = 2;
    p += 40;
    p[0] = 8;
    sk_x11_set16(p + 2, 1);
    p += 8;
    sk_x11_set32(p, 0x40);
    p[4] = 3; /* PseudoColor */
    p += 24;
    p[0] = 24;
    sk_x11_set16(p + 2, 1);
    p += 8;
    sk_x11_set32(p, 0x41);
    p[4] = with_truecolor ? 4 : 5; /* TrueColor, or DirectColor */
    sk_x11_set32(p + 8, 0xff0000);
    sk_x11_set32(p + 12, 0xff00);
    sk_x11_set32(p + 16, 0xff);
    p += 24;
    size_t n = (size_t)(p - b);
    sk_x11_set16(b + 6, (uint32_t)(n - 8) / 4);
    return n;
}

static void test_setup_finds_a_truecolor_visual(void)
{
    uint8_t b[256];
    size_t n = craft_setup(b, true);
    sk_x11 c = {0};
    char *err = NULL;
    CHECK(sk_x11_setup_parse(&c, b, n, 0, &err));
    CHECK_INT(c.root, 0x55);
    CHECK_INT(c.root_visual, 0x40);
    CHECK_INT(c.root_depth, 8);
    CHECK_INT(c.visual, 0x41);
    CHECK_INT(c.bpp24, 32);
    CHECK(!c.image_lsb);
    CHECK_INT(c.max_request, 4096 * 4);
    CHECK_INT(c.min_keycode, 8);
    CHECK_STR(c.vendor, "v");

    sk_x11 d = {0};
    n = craft_setup(b, false);
    CHECK(!sk_x11_setup_parse(&d, b, n, 0, &err));
    CHECK(err && strstr(err, "TrueColor"));
    free(err);
    sk_x11 e = {0};
    err = NULL;
    CHECK(!sk_x11_setup_parse(&e, b, n - 30, 0, &err));
    free(err);
    sk_x11 g = {0};
    err = NULL;
    n = craft_setup(b, true);
    CHECK(!sk_x11_setup_parse(&g, b, n, 1, &err));
    free(err);
}

static void test_events_decode(void)
{
    uint8_t b[32] = {SK_X11_KEY_PRESS | 0x80, 38, 7, 0, 0x10, 0x27, 0, 0};
    sk_x11_set32(b + 12, 0x200002);
    sk_x11_set16(b + 24, (uint32_t)-5);
    sk_x11_set16(b + 26, 40);
    sk_x11_set16(b + 28, 0x41);
    sk_x11_event ev;
    CHECK(sk_x11_event_decode(b, 0, &ev));
    CHECK_INT(ev.type, SK_X11_KEY_PRESS);
    CHECK(ev.sent);
    CHECK_INT(ev.detail, 38);
    CHECK_INT(ev.seq, 7);
    CHECK_INT(ev.time, 10000);
    CHECK_INT(ev.window, 0x200002);
    CHECK_INT(ev.x, -5);
    CHECK_INT(ev.y, 40);
    CHECK_INT(ev.state, 0x41);

    uint8_t c[32] = {SK_X11_CONFIGURE_NOTIFY};
    sk_x11_set32(c + 4, 0x200002);
    sk_x11_set32(c + 8, 0x200003);
    sk_x11_set16(c + 20, 800);
    sk_x11_set16(c + 22, 600);
    CHECK(sk_x11_event_decode(c, 0, &ev));
    CHECK(!ev.sent);
    CHECK_INT(ev.window, 0x200003);
    CHECK_INT(ev.width, 800);
    CHECK_INT(ev.height, 600);

    uint8_t r[32] = {SK_X11_SELECTION_REQUEST};
    for (int i = 0; i < 6; i++)
        sk_x11_set32(r + 4 + 4 * i, (uint32_t)(100 + i));
    CHECK(sk_x11_event_decode(r, 0, &ev));
    CHECK_INT(ev.time, 100);
    CHECK_INT(ev.owner, 101);
    CHECK_INT(ev.requestor, 102);
    CHECK_INT(ev.selection, 103);
    CHECK_INT(ev.target, 104);
    CHECK_INT(ev.property, 105);

    uint8_t m[32] = {SK_X11_CLIENT_MESSAGE, 32};
    sk_x11_set32(m + 4, 9);
    sk_x11_set32(m + 8, 300);
    sk_x11_set32(m + 12, 301);
    sk_x11_set32(m + 16, 302);
    CHECK(sk_x11_event_decode(m, 0, &ev));
    CHECK_INT(ev.atom, 300);
    CHECK_INT(ev.data[0], 301);
    CHECK_INT(ev.data[1], 302);

    uint8_t p[32] = {SK_X11_PROPERTY_NOTIFY};
    sk_x11_set32(p + 4, 9);
    sk_x11_set32(p + 8, 77);
    p[16] = 1;
    CHECK(sk_x11_event_decode(p, 0, &ev));
    CHECK_INT(ev.atom, 77);
    CHECK_INT(ev.mode, 1);

    uint8_t s[32] = {65};
    sk_x11_set32(s + 4, 0x200003);
    sk_x11_set32(s + 12, 0x200009);
    CHECK(!sk_x11_event_decode(s, 0, &ev));
    CHECK(sk_x11_event_decode(s, 65, &ev));
    CHECK_INT(ev.window, 0x200003);
    CHECK_INT(ev.shmseg, 0x200009);
}

static void test_xft_dpi(void)
{
    const char *rm = "Xft.antialias:\t1\nXft.dpi:\t144\nXft.hinting:\t1\n";
    CHECK(sk_x11_xft_dpi(rm, strlen(rm)) == 144);
    rm = "*background: grey\n Xft.dpi :  96.5 \n";
    CHECK(sk_x11_xft_dpi(rm, strlen(rm)) == 96.5);
    rm = "Xft.dpi:\tabc\n";
    CHECK(sk_x11_xft_dpi(rm, strlen(rm)) == 0);
    rm = "*dpi:\t120\nXcursor.size: 24";
    CHECK(sk_x11_xft_dpi(rm, strlen(rm)) == 0);
    CHECK(sk_x11_xft_dpi("", 0) == 0);
    rm = "Xft.dpi:\t192";
    CHECK(sk_x11_xft_dpi(rm, strlen(rm)) == 192);
}

static void test_incr_chunks(void)
{
    sk_x11 c = {0};
    c.max_request = 65535 * 4;
    CHECK_INT(sk_x11_incr_threshold(&c), 65535 * 4 - 24);
    c.max_request = 0x3fffff * 4;
    CHECK_INT(sk_x11_incr_threshold(&c), 1 << 18);
    c.max_request = 4096 * 4;
    CHECK_INT(sk_x11_incr_threshold(&c), 4096 * 4 - 24);

    char text[] = "abcdefghij";
    sk_x11_incr t = {1, 2, 3, text, 10, 0, false};
    const char *want[] = {"abcd", "efgh", "ij", ""};
    for (size_t i = 0; i < SK_LEN(want); i++) {
        CHECK(!t.done);
        sk_str s = sk_x11_incr_next(&t, 4);
        CHECK_MEM(s, want[i]);
    }
    CHECK(t.done);
}

static void test_latin1(void)
{
    sk_buf b = {0};
    const char *s = "h\xc3\xa9llo \xe2\x82\xac";
    sk_x11_latin1_from_utf8(&b, s, strlen(s));
    CHECK_STR(sk_buf_cstr(&b), "h\xe9llo ?");
    sk_buf_clear(&b);
    sk_x11_utf8_from_latin1(&b, (const uint8_t *)"h\xe9\xff", 3);
    CHECK_STR(sk_buf_cstr(&b), "h\xc3\xa9\xc3\xbf");
    sk_buf_free(&b);
}

/* The paths of a text/uri-list, one a line, joined by '|' to check. */
static char *uri_files(const char *list)
{
    sk_strv v = {0};
    sk_x11_uri_list_files(list, strlen(list), &v);
    sk_buf b = {0};
    for (size_t i = 0; i < v.len; i++)
        sk_buf_printf(&b, "%s%s", i ? "|" : "", v.ptr[i]);
    sk_strv_free(&v);
    sk_buf_reserve(&b, 0);
    return b.ptr;
}

#define CHECK_URIS(list, want)                                               \
    do {                                                                     \
        char *got_ = uri_files(list);                                        \
        CHECK_STR(got_, want);                                               \
        free(got_);                                                          \
    } while (0)

static void test_uri_list_lines_and_comments(void)
{
    CHECK_URIS("file:///home/u/a.txt\r\nfile:///srv/b\r\n", "/home/u/a.txt|/srv/b");
    /* The last line may lack its CRLF; a bare LF ends a line too. */
    CHECK_URIS("file:///a\r\nfile:///b", "/a|/b");
    CHECK_URIS("file:///a\nfile:///b\n", "/a|/b");
    CHECK_URIS("# dragged from a file manager\r\nfile:///a\r\n#file:///not\r\n\r\nfile:///b\r\n",
               "/a|/b");
    CHECK_URIS("", "");
    CHECK_URIS("\r\n", "");
}

static void test_uri_list_decodes_local_files_only(void)
{
    CHECK_URIS("file:///home/u/two%20words/it's%2a%3F.txt", "/home/u/two words/it's*?.txt");
    CHECK_URIS("file:///tmp/%c3%a9t%C3%A9", "/tmp/\xc3\xa9t\xc3\xa9");
    CHECK_URIS("file://localhost/etc/hosts\r\nFILE://LocalHost/x\r\n", "/etc/hosts|/x");
    /* Another host's file, another scheme, and what is not a URI. */
    CHECK_URIS("file://elsewhere/etc/hosts\r\n"
               "https://example.com/a\r\n"
               "file:/no/authority\r\n"
               "/plain/path\r\n"
               "file://localhost\r\n",
               "");
    /* A bad escape, an escaped NUL and a fragment name no file. */
    CHECK_URIS("file:///a%2\r\nfile:///a%zz\r\nfile:///a%00b\r\nfile:///a#b\r\nfile:///ok", "/ok");
    /* What is not UTF-8 becomes U+FFFD. */
    CHECK_URIS("file:///tmp/%e9t%e9", "/tmp/\xef\xbf\xbdt\xef\xbf\xbd");
}

static void test_connection_to_the_fake_server(void)
{
    sk_xfake_opts o = {.resources = "Xft.dpi:\t120\n", .big_requests = true};
    sk_xfake *f = sk_xfake_start(&o);
    char *err = NULL;
    sk_x11 *c = sk_x11_open(sk_xfake_display(f), &err);
    CHECK(c != NULL);
    if (!c) {
        fprintf(stderr, "    %s\n", err ? err : "?");
        free(err);
        sk_xfake_stop(f);
        return;
    }
    CHECK_STR(c->vendor, "skindeep's fake");
    CHECK_INT(c->root, SK_XFAKE_ROOT);
    CHECK_INT(c->visual, SK_XFAKE_VISUAL);
    CHECK_INT(c->root_visual, SK_XFAKE_VISUAL);
    CHECK_INT(c->min_keycode, 8);
    CHECK_INT(c->max_keycode, 255);
    CHECK_INT(c->max_request, 65535 * 4);
    CHECK(c->image_lsb);
    CHECK_INT(sk_x11_id(c), 0x200001);
    CHECK_INT(sk_x11_id(c), 0x200002);

    sk_x11_extensions(c);
    CHECK_INT(c->big_opcode, 130);
    CHECK_INT(c->max_request, 0x3fffff * 4);
    CHECK_INT(c->shm_opcode, 0);

    const char *names[] = {"WM_PROTOCOLS", "CLIPBOARD", "WM_PROTOCOLS", "PRIMARY"};
    uint32_t atoms[4];
    CHECK(sk_x11_intern(c, names, 4, atoms));
    CHECK(atoms[0] != 0 && atoms[0] == atoms[2] && atoms[1] != atoms[0]);
    CHECK_INT(atoms[3], SK_X11_PRIMARY);

    sk_x11_reply r;
    sk_x11_prop p;
    CHECK(sk_x11_prop_get(c, false, c->root, SK_X11_RESOURCE_MANAGER, SK_X11_STRING, 1 << 16, &r,
                          &p));
    CHECK_INT(p.format, 8);
    CHECK(sk_x11_xft_dpi((const char *)p.value, p.len) == 120);
    sk_x11_reply_free(&r);

    /* An event that comes while a reply is awaited waits in the queue. */
    uint8_t ev[32] = {SK_X11_FOCUS_IN, 0};
    sk_x11_set32(ev + 4, 0x200001);
    sk_xfake_event(f, ev);
    CHECK(sk_x11_sync(c));
    uint8_t got[32];
    CHECK(sk_x11_next_event(c, got));
    CHECK_INT(got[0], SK_X11_FOCUS_IN);
    CHECK_INT(sk_x11_get32(got + 4), 0x200001);
    CHECK(!sk_x11_next_event(c, got));

    /* An error answers the request it came from. */
    uint32_t win = sk_x11_id(c);
    uint32_t mask = 0;
    sk_x11_create_window(c, 24, win, c->root, 0, 0, 4, 4, 1, c->visual, SK_X11_CW_EVENT_MASK,
                         &mask);
    uint8_t code = 0;
    CHECK(!sk_x11_reply_wait(c, sk_x11_get_image(c, win, 0, 0, 4, 4), &r, &code));
    CHECK_INT(code, 8);
    CHECK_INT(sk_x11_check(c, sk_x11_get_image(c, win, 0, 0, 4, 4)), 8);
    CHECK_INT(sk_x11_check(c, sk_x11_map_window(c, win)), 0);
    CHECK(sk_x11_reply_wait(c, sk_x11_get_image(c, win, 0, 0, 4, 4), &r, &code));
    CHECK_INT(r.len, 32 + 64);
    sk_x11_reply_free(&r);

    /* A request past 256 KiB goes in BIG-REQUESTS's form. */
    size_t n = 300000;
    char *data = sk_xmalloc(n);
    for (size_t i = 0; i < n; i++)
        data[i] = (char)('a' + i % 26);
    sk_x11_change_property(c, 0, win, SK_X11_WM_NAME, SK_X11_STRING, 8, data, (uint32_t)n);
    CHECK(sk_x11_sync(c));
    CHECK_INT(sk_xfake_big_count(f), 1);
    size_t len = 0;
    uint8_t *stored = sk_xfake_property(f, win, SK_X11_WM_NAME, &len, NULL);
    CHECK(stored && len == n && !memcmp(stored, data, n));
    free(stored);
    free(data);

    sk_x11_free(c);
    sk_xfake_stop(f);
}

static void test_the_cookie_reaches_the_server(void)
{
    char host[256];
    CHECK(gethostname(host, sizeof host) == 0);
    host[sizeof host - 1] = 0;
    sk_buf b = {0};
    put_entry(&b, 256, host, "7", "MIT-MAGIC-COOKIE-1", "\x01\x02\x03\x04\x05\x06\x07\x08"
                                                       "\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10");
    sk_buf path = {0};
    sk_buf_printf(&path, "%s/Xauthority", sk_test_tmpdir());
    FILE *fp = fopen(path.ptr, "wb");
    CHECK(fp && fwrite(b.ptr, 1, b.len, fp) == b.len);
    if (fp)
        fclose(fp);
    char *saved = getenv("XAUTHORITY") ? sk_xstrdup(getenv("XAUTHORITY")) : NULL;
    setenv("XAUTHORITY", path.ptr, 1);
    sk_xfake *f = sk_xfake_start(NULL);
    char *err = NULL;
    sk_x11 *c = sk_x11_open(sk_xfake_display(f), &err);
    CHECK(c != NULL);
    char *auth = sk_xfake_auth(f);
    CHECK_STR(auth, "MIT-MAGIC-COOKIE-1 0102030405060708090a0b0c0d0e0f10");
    free(auth);
    sk_x11_free(c);
    free(err);
    sk_xfake_stop(f);
    if (saved)
        setenv("XAUTHORITY", saved, 1);
    else
        unsetenv("XAUTHORITY");
    free(saved);
    sk_buf_free(&path);
    sk_buf_free(&b);
}

static void test_a_refused_setup_says_why(void)
{
    sk_xfake_opts o = {.refuse = "No protocol specified\n"};
    sk_xfake *f = sk_xfake_start(&o);
    char *err = NULL;
    sk_x11 *c = sk_x11_open(sk_xfake_display(f), &err);
    CHECK(c == NULL);
    CHECK_STR(err, "the X server refused the connection: No protocol specified");
    free(err);
    sk_xfake_stop(f);
    err = NULL;
    CHECK(sk_x11_open("/nonexistent/socket:3", &err) == NULL);
    CHECK(err && strstr(err, "cannot reach"));
    free(err);
}

const sk_test sk_suite_x11[] = {
    T(test_display_names),
    T(test_xauthority_matches_a_local_display),
    T(test_xauthority_file_and_its_variable),
    T(test_setup_request_bytes),
    T(test_request_bytes),
    T(test_big_requests_form),
    T(test_setup_finds_a_truecolor_visual),
    T(test_events_decode),
    T(test_xft_dpi),
    T(test_incr_chunks),
    T(test_latin1),
    T(test_uri_list_lines_and_comments),
    T(test_uri_list_decodes_local_files_only),
    T(test_connection_to_the_fake_server),
    T(test_the_cookie_reaches_the_server),
    T(test_a_refused_setup_says_why),
    {0},
};
