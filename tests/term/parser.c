/* The VT parser: xterm.js's transition table, parameters, strings and
 * UTF-8 decoding, each case worked out from xterm.js 6.0.0's
 * EscapeSequenceParser.ts and TextDecoder.ts. */
#include "../../src/base/utf8.h"
#include "../../src/term/parser.h"
#include "../test.h"

#include <stdlib.h>

typedef struct {
    sk_buf log; /* one line per event */
    sk_vt_params last;
} capture;

static void cap_print(void *ud, const uint32_t *cp, size_t n)
{
    capture *c = ud;
    sk_buf_append_c(&c->log, "print ");
    for (size_t i = 0; i < n; i++)
        sk_buf_printf(&c->log, i ? " %x" : "%x", cp[i]);
    sk_buf_putc(&c->log, '\n');
}

static void cap_exec(void *ud, uint32_t code)
{
    sk_buf_printf(&((capture *)ud)->log, "exec %x\n", code);
}

static void cap_csi(void *ud, uint32_t ident, const sk_vt_params *p)
{
    capture *c = ud;
    c->last = *p;
    sk_buf_printf(&c->log, "csi %x", ident);
    for (int i = 0; i < p->n; i++) {
        sk_buf_printf(&c->log, " %d", p->v[i]);
        const int32_t *sub;
        int ns = sk_vt_subs(p, i, &sub);
        for (int k = 0; k < ns; k++)
            sk_buf_printf(&c->log, ":%d", sub[k]);
    }
    sk_buf_putc(&c->log, '\n');
}

static void cap_esc(void *ud, uint32_t ident)
{
    sk_buf_printf(&((capture *)ud)->log, "esc %x\n", ident);
}

static bool want_all(void *ud, int64_t id)
{
    (void)ud;
    return id >= 0;
}

static bool want_dcs(void *ud, uint32_t ident)
{
    (void)ud;
    (void)ident;
    return true;
}

static void cap_osc(void *ud, int64_t id, const char *data, size_t len)
{
    sk_buf_printf(&((capture *)ud)->log, "osc %lld [%.*s]\n", (long long)id, (int)len, data);
}

static void cap_dcs(void *ud, uint32_t ident, const sk_vt_params *p, const char *data, size_t len)
{
    sk_buf_printf(&((capture *)ud)->log, "dcs %x %d [%.*s]\n", ident, p->v[0], (int)len, data);
}

typedef struct {
    sk_vt_parser p;
    capture c;
} rig;

static void rig_open(rig *r)
{
    memset(r, 0, sizeof *r);
    sk_vt_handler h = {&r->c, cap_print, cap_exec, cap_csi, cap_esc, want_all, want_dcs, cap_osc, cap_dcs};
    sk_vt_init(&r->p, &h);
}

static void rig_close(rig *r)
{
    sk_vt_free(&r->p);
    sk_buf_free(&r->c.log);
}

#define CHECK_PARSE(input, want)                                             \
    do {                                                                     \
        rig r_;                                                              \
        rig_open(&r_);                                                       \
        sk_vt_feed(&r_.p, input, sizeof(input) - 1);                         \
        CHECK_STR(sk_buf_cstr(&r_.c.log), want);                             \
        rig_close(&r_);                                                      \
    } while (0)

static size_t decode(sk_vt_parser *p, const char *s, size_t n, uint32_t *out)
{
    return sk_vt_decode(p, (const unsigned char *)s, n, out);
}

static void test_utf8_valid(void)
{
    rig r;
    rig_open(&r);
    uint32_t out[8];
    CHECK_INT(decode(&r.p, "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80", 10, out), 4);
    CHECK_INT(out[0], 'a');
    CHECK_INT(out[1], 0xE9);
    CHECK_INT(out[2], 0x20AC);
    CHECK_INT(out[3], 0x1F600);
    CHECK_INT(decode(&r.p, "\xE0\xA0\x80", 3, out), 1);
    CHECK_INT(out[0], 0x800);
    rig_close(&r);
}

static void test_utf8_invalid(void)
{
    /* Each case: the input and what Utf8ToUtf32 keeps of it. */
    struct {
        const char *in;
        size_t n;
        const char *keep;
    } cases[] = {
        {"a\x80" "b", 3, "ab"},            /* a lone continuation is skipped */
        {"\xC3(", 2, "("},                 /* a wrong continuation drops the lead */
        {"\xE2\x82(", 3, "("},             /* and what came with it */
        {"\xC0\xAF", 2, ""},               /* overlong two bytes: both go */
        {"\xE0\x80\x80", 3, ""},           /* overlong three bytes */
        {"\xED\xA0\x80", 3, ""},           /* a surrogate */
        {"\xEF\xBB\xBF" "x", 4, "x"},      /* the BOM is dropped */
        {"\xF4\x90\x80\x80", 4, ""},       /* past U+10FFFF */
        {"\xF8x", 2, "x"},                 /* never a lead */
        {"\xF0\x9F\x98" "a", 4, "a"},      /* cut short */
    };
    for (size_t i = 0; i < SK_LEN(cases); i++) {
        rig r;
        rig_open(&r);
        uint32_t out[8];
        size_t k = decode(&r.p, cases[i].in, cases[i].n, out);
        sk_buf got = {0};
        for (size_t j = 0; j < k; j++)
            sk_buf_putc(&got, (char)out[j]);
        CHECK_STR(sk_buf_cstr(&got), cases[i].keep);
        sk_buf_free(&got);
        rig_close(&r);
    }
}

static void test_utf8_every_codepoint(void)
{
    /* xterm.js's full codepoint test: each one decodes to itself, but a
     * surrogate, which UTF-8 cannot hold, and the BOM, which is dropped. */
    rig r;
    rig_open(&r);
    uint32_t out[4];
    for (uint32_t cp = 1; cp <= 0x10FFFF; cp++) {
        if (cp >= 0xD800 && cp <= 0xDFFF)
            continue;
        char enc[4];
        size_t n = sk_utf8_encode(cp, enc);
        size_t k = sk_vt_decode(&r.p, (const unsigned char *)enc, n, out);
        if (cp == 0xFEFF) {
            CHECK_INT(k, 0);
        } else if (k != 1 || out[0] != cp) {
            sk_test_fail(__FILE__, __LINE__, "U+%04X decoded to %zu codepoints", cp, k);
            break;
        }
    }
    rig_close(&r);
}

static void test_utf8_split(void)
{
    /* A character cut by the end of a feed decodes with the next. */
    const char *s = "\xF0\x9F\x98\x80\xE2\x82\xAC\xC3\xA9";
    for (size_t cut = 0; cut <= 9; cut++) {
        rig r;
        rig_open(&r);
        uint32_t out[8];
        size_t k = decode(&r.p, s, cut, out);
        k += decode(&r.p, s + cut, 9 - cut, out + k);
        CHECK_INT(k, 3);
        CHECK_INT(out[0], 0x1F600);
        CHECK_INT(out[1], 0x20AC);
        CHECK_INT(out[2], 0xE9);
        rig_close(&r);
    }
    rig r;
    rig_open(&r);
    uint32_t out[4];
    CHECK_INT(decode(&r.p, "\xE2", 1, out), 0);
    CHECK_INT(decode(&r.p, "\x82", 1, out), 0);
    CHECK_INT(decode(&r.p, "(", 1, out), 1);
    CHECK_INT(out[0], '(');
    rig_close(&r);
}

static void test_params(void)
{
    CHECK_PARSE("\x1b[1;2:3:4;5::6m", "csi 6d 1 2:3:4 5:-1:6\n");
    CHECK_PARSE("\x1b[;5H", "csi 48 0 5\n");
    CHECK_PARSE("\x1b[m", "csi 6d 0\n");
    CHECK_PARSE("\x1b[99999999999m", "csi 6d 2147483647\n");
    /* A sub-parameter before any digit belongs to the first parameter. */
    CHECK_PARSE("\x1b[:3m", "csi 6d 0:3\n");
}

static void test_params_limit(void)
{
    sk_buf in = {0}, want = {0};
    sk_buf_append_c(&in, "\x1b[");
    sk_buf_append_c(&want, "csi 6d");
    for (int i = 1; i <= 40; i++) {
        sk_buf_printf(&in, i > 1 ? ";%d" : "%d", i);
        if (i <= 32)
            sk_buf_printf(&want, " %d", i);
    }
    sk_buf_append_c(&in, "m");
    sk_buf_append_c(&want, "\n");
    rig r;
    rig_open(&r);
    sk_vt_feed(&r.p, in.ptr, in.len);
    CHECK_STR(sk_buf_cstr(&r.c.log), want.ptr);
    rig_close(&r);
    sk_buf_free(&in);
    sk_buf_free(&want);
}

static void test_identifiers(void)
{
    CHECK_PARSE("\x1b[?1$p", "csi 3f2470 1\n");
    CHECK_PARSE("\x1b[>c", "csi 3e63 0\n");
    CHECK_PARSE("\x1b[3 q", "csi 2071 3\n");
    CHECK_PARSE("\x1b(0", "esc 2830\n");
    CHECK_PARSE("\x1b#8", "esc 2338\n");
    /* C1 CSI, as the codepoint U+009B. */
    CHECK_PARSE("\xC2\x9B" "2J", "csi 4a 2\n");
}

static void test_csi_ignored(void)
{
    /* A prefix byte after a digit ignores the sequence. */
    CHECK_PARSE("\x1b[1?mx", "print 78\n");
    /* A digit after an intermediate ignores it too. */
    CHECK_PARSE("\x1b[ 1qx", "print 78\n");
    /* A non-ASCII character aborts it: the rest prints. */
    CHECK_PARSE("\x1b[1\xC3\xA9" "2m", "print 32 6d\n");
    /* ESC starts over. */
    CHECK_PARSE("\x1b[12\x1b[3H", "csi 48 3\n");
}

static void test_execute_inside(void)
{
    /* C0 controls run in the middle of a sequence. */
    CHECK_PARSE("\x1b[1\n2H", "exec a\ncsi 48 12\n");
    CHECK_PARSE("ab\x07" "c", "print 61 62\nexec 7\nprint 63\n");
    /* DEL is no character: it ends a run. */
    CHECK_PARSE("a\x7f" "b", "print 61\nprint 62\n");
    /* C1 controls execute, IND as U+0084. */
    CHECK_PARSE("\xC2\x84", "exec 84\n");
}

static void test_osc(void)
{
    CHECK_PARSE("\x1b]2;hello\x07", "osc 2 [hello]\n");
    CHECK_PARSE("\x1b]2;hi\x1b\\", "osc 2 [hi]\nesc 5c\n");
    CHECK_PARSE("\x1b]0;a\xC2\x9C", "osc 0 [a]\n");
    CHECK_PARSE("\x1b]2;caf\xC3\xA9\x07", "osc 2 [caf\xC3\xA9]\n");
    /* No payload: the handler sees an empty string. */
    CHECK_PARSE("\x1b]2\x07", "osc 2 []\n");
    /* C0 inside is ignored, DEL kept. */
    CHECK_PARSE("\x1b]2;a\x01" "b\x07", "osc 2 [ab]\n");
    CHECK_PARSE("\x1b]2;a\x7f" "b\x07", "osc 2 [a\x7f" "b]\n");
    /* Only the first ; ends the id. */
    CHECK_PARSE("\x1b]8;;http://x/?a;b\x07", "osc 8 [;http://x/?a;b]\n");
}

static void test_osc_aborted(void)
{
    /* CAN and SUB end it without success. */
    CHECK_PARSE("\x1b]2;x\x18", "");
    CHECK_PARSE("\x1b]2;x\x1a" "y", "print 79\n");
    /* A non-digit id aborts it. */
    CHECK_PARSE("\x1b]2a;x\x07", "");
    /* No id at all reaches no handler. */
    CHECK_PARSE("\x1b];x\x07", "");
    /* A C1 control executes and leaves the string unfinished. */
    CHECK_PARSE("\x1b]2;a\xC2\x84" "b\x07", "exec 84\nprint 62\nexec 7\n");
}

static void test_osc_limit(void)
{
    /* 10,000,000 UTF-16 units pass; one more drops the string. */
    for (int over = 0; over <= 1; over++) {
        size_t n = 10000000 + (size_t)over;
        char *s = sk_xmalloc(n + 6);
        memcpy(s, "\x1b]2;", 4);
        memset(s + 4, 'a', n);
        s[4 + n] = '\x07';
        rig r;
        rig_open(&r);
        sk_vt_feed(&r.p, s, n + 5);
        CHECK_INT(r.c.log.len > 0, !over);
        rig_close(&r);
        free(s);
    }
}

static void test_dcs(void)
{
    CHECK_PARSE("\x1bP$qm\x1b\\", "dcs 2471 0 [m]\nesc 5c\n");
    CHECK_PARSE("\x1bP1$qr\x1b\\", "dcs 2471 1 [r]\nesc 5c\n");
    /* C0 goes into the payload. */
    CHECK_PARSE("\x1bP$qa\x01" "b\x1b\\", "dcs 2471 0 [a\x01" "b]\nesc 5c\n");
    /* DEL is dropped when it comes first, kept when it follows. */
    CHECK_PARSE("\x1bP$q\x7f" "ab\x1b\\", "dcs 2471 0 [ab]\nesc 5c\n");
    CHECK_PARSE("\x1bP$qa\x7f" "b\x1b\\", "dcs 2471 0 [a\x7f" "b]\nesc 5c\n");
    /* CAN aborts. */
    CHECK_PARSE("\x1bP$qm\x18", "");
}

static void test_apc_pm_sos(void)
{
    CHECK_PARSE("\x1b_junk\x1b\\x", "esc 5c\nprint 78\n");
    CHECK_PARSE("\x1b^pm\x1b\\x", "esc 5c\nprint 78\n");
    CHECK_PARSE("\x1bXsos\x1b\\x", "esc 5c\nprint 78\n");
    /* A non-ASCII character ends the string and prints what follows. */
    CHECK_PARSE("\x1b_a\xC3\xA9 b", "print 20 62\n");
}

static void test_split_feeds(void)
{
    /* Any cut of a stream parses as the whole. */
    const char *s = "a\x1b[1;31mb\x1b]2;t\xC3\xA9\x07\xE6\x97\xA5\x1bP$qm\x1b\\c";
    size_t n = strlen(s);
    rig whole;
    rig_open(&whole);
    sk_vt_feed(&whole.p, s, n);
    CHECK_STR(sk_buf_cstr(&whole.c.log),
              "print 61\ncsi 6d 1 31\nprint 62\nosc 2 [t\xC3\xA9]\nprint 65e5\ndcs 2471 0 [m]\nesc 5c\nprint 63\n");
    /* Every print here is one character, so no cut can split a run. */
    for (size_t cut = 0; cut <= n; cut++) {
        rig r;
        rig_open(&r);
        sk_vt_feed(&r.p, s, cut);
        sk_vt_feed(&r.p, s + cut, n - cut);
        CHECK_STR(sk_buf_cstr(&r.c.log), sk_buf_cstr(&whole.c.log));
        rig_close(&r);
    }
    rig bytes;
    rig_open(&bytes);
    for (size_t k = 0; k < n; k++)
        sk_vt_feed(&bytes.p, s + k, 1);
    CHECK_STR(sk_buf_cstr(&bytes.c.log), sk_buf_cstr(&whole.c.log));
    rig_close(&bytes);
    rig_close(&whole);
}

const sk_test sk_suite_term_parser[] = {
    T(test_utf8_valid),
    T(test_utf8_invalid),
    T(test_utf8_every_codepoint),
    T(test_utf8_split),
    T(test_params),
    T(test_params_limit),
    T(test_identifiers),
    T(test_csi_ignored),
    T(test_execute_inside),
    T(test_osc),
    T(test_osc_aborted),
    T(test_osc_limit),
    T(test_dcs),
    T(test_apc_pm_sos),
    T(test_split_feeds),
    {0},
};
