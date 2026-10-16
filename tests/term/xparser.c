/* xterm.js 6.0.0's own EscapeSequenceParser tests (src/common/parser/
 * EscapeSequenceParser.test.ts), the state transitions and actions and
 * the sequence examples, ported onto the C parser: from each state, each
 * byte class, the next state, the parameters, the collected bytes and
 * the calls are xterm.js's. Its harness hears every ESC dispatch, where
 * xterm.js's own ESC \ handler swallows the one it registers; and DCS
 * payloads come whole at a successful end rather than piece by piece. */
#include "../../src/base/utf8.h"
#include "../../src/term/parser.h"
#include "../test.h"

#include <stdlib.h>

enum {
    GROUND,
    ESCAPE,
    ESCAPE_INTERMEDIATE,
    CSI_ENTRY,
    CSI_PARAM,
    CSI_INTERMEDIATE,
    CSI_IGNORE,
    SOS_PM_APC_STRING,
    OSC_STRING,
    DCS_ENTRY,
    DCS_PARAM,
    DCS_IGNORE,
    DCS_INTERMEDIATE,
    DCS_PASSTHROUGH,
    N_STATES
};

typedef struct {
    sk_vt_parser p;
    sk_buf log;
} rig;

static void on_print(void *ud, const uint32_t *cp, size_t n)
{
    rig *r = ud;
    sk_buf_append_c(&r->log, "print ");
    for (size_t i = 0; i < n; i++)
        sk_utf8_append(&r->log, cp[i]);
    sk_buf_putc(&r->log, '\n');
}

static void on_exec(void *ud, uint32_t code)
{
    sk_buf_printf(&((rig *)ud)->log, "exe %x\n", code);
}

static void params_of(sk_buf *b, const sk_vt_params *p)
{
    for (int i = 0; i < p->n; i++) {
        sk_buf_printf(b, " %d", p->v[i]);
        const int32_t *sub;
        int ns = sk_vt_subs(p, i, &sub);
        for (int k = 0; k < ns; k++)
            sk_buf_printf(b, ":%d", sub[k]);
    }
}

static void on_csi(void *ud, uint32_t ident, const sk_vt_params *p)
{
    rig *r = ud;
    sk_buf_printf(&r->log, "csi %x", ident);
    params_of(&r->log, p);
    sk_buf_putc(&r->log, '\n');
}

static void on_esc(void *ud, uint32_t ident)
{
    sk_buf_printf(&((rig *)ud)->log, "esc %x\n", ident);
}

static bool wanted(void *ud, int64_t id)
{
    (void)ud;
    return id >= 0;
}

static bool wanted_dcs(void *ud, uint32_t ident)
{
    (void)ud;
    (void)ident;
    return true;
}

static void on_osc(void *ud, int64_t id, const char *data, size_t len)
{
    sk_buf_printf(&((rig *)ud)->log, "osc %lld;%.*s\n", (long long)id, (int)len, data);
}

static void on_dcs(void *ud, uint32_t ident, const sk_vt_params *p, const char *data, size_t len)
{
    rig *r = ud;
    sk_buf_printf(&r->log, "dcs %x", ident);
    params_of(&r->log, p);
    sk_buf_printf(&r->log, " [%.*s]\n", (int)len, data);
}

static void rig_open(rig *r)
{
    memset(r, 0, sizeof *r);
    sk_vt_handler h = {r, on_print, on_exec, on_csi, on_esc, wanted, wanted_dcs, on_osc, on_dcs};
    sk_vt_init(&r->p, &h);
}

static void rig_reset(rig *r)
{
    sk_vt_reset(&r->p);
    sk_buf_clear(&r->log);
}

static void rig_close(rig *r)
{
    sk_vt_free(&r->p);
    sk_buf_free(&r->log);
}

/* parse() of codepoints, fed as UTF-8. */
static void cps(rig *r, const uint32_t *cp, size_t n)
{
    sk_buf b = {0};
    for (size_t i = 0; i < n; i++)
        sk_utf8_append(&b, cp[i]);
    sk_vt_feed(&r->p, b.ptr, b.len);
    sk_buf_free(&b);
}

static void one(rig *r, uint32_t c)
{
    cps(r, &c, 1);
}

static void str(rig *r, const char *s)
{
    sk_vt_feed(&r->p, s, strlen(s));
}

static void set_params(rig *r, const int32_t *v, int n)
{
    r->p.params.n = 0;
    r->p.params.nsub = 0;
    for (int i = 0; i < n; i++) {
        r->p.params.subidx[i] = 0;
        r->p.params.v[r->p.params.n++] = v[i];
    }
}

static bool params_are(rig *r, const int32_t *v, int n)
{
    if (r->p.params.n != n)
        return false;
    for (int i = 0; i < n; i++)
        if (r->p.params.v[i] != v[i])
            return false;
    return true;
}

/* The executables, in xterm.js's order: 0x00-0x17, 0x19, 0x1c-0x1f. */
static int executables(uint32_t *out)
{
    int n = 0;
    for (uint32_t c = 0; c < 0x18; c++)
        out[n++] = c;
    out[n++] = 0x19;
    for (uint32_t c = 0x1c; c < 0x20; c++)
        out[n++] = c;
    return n;
}

#define EXPECT(cond, ...)                                                    \
    do {                                                                     \
        if (!(cond))                                                         \
            sk_test_fail(__FILE__, __LINE__, __VA_ARGS__);                   \
    } while (0)

/* From state s each code in [lo, hi) stays in, or moves to, state to,
 * logging what as its call. */
enum { NOTHING, EXE, ESC_D, CSI_D };

static void sweep(int s, uint32_t lo, uint32_t hi, int to, int what)
{
    rig r;
    rig_open(&r);
    for (uint32_t c = lo; c < hi; c++) {
        rig_reset(&r);
        r.p.state = (uint8_t)s;
        one(&r, c);
        EXPECT(r.p.state == to, "state %d code %x: went to %d, want %d", s, c, r.p.state, to);
        sk_buf want = {0};
        if (what == EXE)
            sk_buf_printf(&want, "exe %x\n", c);
        else if (what == ESC_D)
            sk_buf_printf(&want, "esc %x\n", c);
        else if (what == CSI_D)
            sk_buf_printf(&want, "csi %x 0\n", c);
        EXPECT(!strcmp(sk_buf_cstr(&r.log), sk_buf_cstr(&want)), "state %d code %x: logged [%s], want [%s]", s, c,
               sk_buf_cstr(&r.log), sk_buf_cstr(&want));
        sk_buf_free(&want);
    }
    rig_close(&r);
}

static void sweep_exec(int s)
{
    uint32_t ex[32];
    int n = executables(ex);
    for (int i = 0; i < n; i++)
        sweep(s, ex[i], ex[i] + 1, s, EXE);
}

static void test_ground(void)
{
    sweep_exec(GROUND);
    rig r;
    rig_open(&r);
    for (uint32_t c = 0x20; c < 0x7f; c++) {
        rig_reset(&r);
        one(&r, c);
        sk_buf want = {0};
        sk_buf_printf(&want, "print %c\n", (char)c);
        CHECK_STR(sk_buf_cstr(&r.log), want.ptr);
        CHECK_INT(r.p.state, GROUND);
        sk_buf_free(&want);
    }
    rig_close(&r);
}

static void test_anywhere(void)
{
    const uint32_t exes[] = {0x18, 0x1a, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b,
                             0x8c, 0x8d, 0x8e, 0x8f, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x99, 0x9a};
    for (int s = 0; s < N_STATES; s++) {
        for (size_t i = 0; i < SK_LEN(exes); i++) {
            /* CAN and SUB end an OSC or DCS string instead. */
            bool ends = (s == OSC_STRING || s == DCS_PASSTHROUGH) && (exes[i] == 0x18 || exes[i] == 0x1a);
            sweep(s, exes[i], exes[i] + 1, GROUND, ends ? NOTHING : EXE);
        }
    }
    sweep(GROUND, 0x9c, 0x9d, GROUND, NOTHING);
    rig r;
    rig_open(&r);
    int32_t p23 = 23, zero = 0;
    for (int s = 0; s < N_STATES; s++) {
        rig_reset(&r);
        r.p.state = (uint8_t)s;
        set_params(&r, &p23, 1);
        r.p.collect = '#';
        one(&r, 0x1b);
        CHECK_INT(r.p.state, ESCAPE);
        CHECK(params_are(&r, &zero, 1));
        CHECK_INT(r.p.collect, 0);
        /* C1 CSI from anywhere clears too. */
        rig_reset(&r);
        r.p.state = (uint8_t)s;
        int32_t p123 = 123;
        set_params(&r, &p123, 1);
        r.p.collect = '#';
        one(&r, 0x9b);
        CHECK_INT(r.p.state, CSI_ENTRY);
        CHECK(params_are(&r, &zero, 1));
        CHECK_INT(r.p.collect, 0);
        const uint32_t sos[] = {0x98, 0x9e, 0x9f};
        for (int k = 0; k < 3; k++) {
            rig_reset(&r);
            r.p.state = (uint8_t)s;
            one(&r, sos[k]);
            CHECK_INT(r.p.state, SOS_PM_APC_STRING);
        }
        rig_reset(&r);
        r.p.state = (uint8_t)s;
        one(&r, 0x9d);
        CHECK_INT(r.p.state, OSC_STRING);
        rig_reset(&r);
        r.p.state = (uint8_t)s;
        one(&r, 0x90);
        CHECK_INT(r.p.state, DCS_ENTRY);
    }
    rig_close(&r);
}

static void test_escape(void)
{
    sweep_exec(ESCAPE);
    sweep(ESCAPE, 0x7f, 0x80, ESCAPE, NOTHING);
    sweep(ESCAPE, 0x30, 0x50, GROUND, ESC_D);
    sweep(ESCAPE, 0x51, 0x58, GROUND, ESC_D);
    sweep(ESCAPE, 0x59, 0x5b, GROUND, ESC_D);
    sweep(ESCAPE, 0x5c, 0x5d, GROUND, ESC_D);
    sweep(ESCAPE, 0x60, 0x7f, GROUND, ESC_D);
    sweep(ESCAPE, 0x20, 0x30, ESCAPE_INTERMEDIATE, NOTHING);
    sweep_exec(ESCAPE_INTERMEDIATE);
    sweep(ESCAPE_INTERMEDIATE, 0x7f, 0x80, ESCAPE_INTERMEDIATE, NOTHING);
    sweep(ESCAPE_INTERMEDIATE, 0x20, 0x30, ESCAPE_INTERMEDIATE, NOTHING);
    sweep(ESCAPE_INTERMEDIATE, 0x30, 0x7f, GROUND, ESC_D);
    rig r;
    rig_open(&r);
    for (uint32_t c = 0x20; c < 0x30; c++) {
        rig_reset(&r);
        r.p.state = ESCAPE;
        one(&r, c);
        CHECK_INT(r.p.collect, c);
    }
    /* ESC [ clears. */
    rig_reset(&r);
    r.p.state = ESCAPE;
    int32_t p123 = 123, zero = 0;
    set_params(&r, &p123, 1);
    r.p.collect = '#';
    str(&r, "[");
    CHECK_INT(r.p.state, CSI_ENTRY);
    CHECK(params_are(&r, &zero, 1));
    CHECK_INT(r.p.collect, 0);
    const char *sos[] = {"\x1bX", "\x1b^", "\x1b_"};
    for (int k = 0; k < 3; k++) {
        rig_reset(&r);
        str(&r, sos[k]);
        CHECK_INT(r.p.state, SOS_PM_APC_STRING);
    }
    rig_reset(&r);
    str(&r, "\x1b]");
    CHECK_INT(r.p.state, OSC_STRING);
    rig_reset(&r);
    str(&r, "\x1bP");
    CHECK_INT(r.p.state, DCS_ENTRY);
    rig_close(&r);
}

static void test_csi(void)
{
    sweep_exec(CSI_ENTRY);
    sweep(CSI_ENTRY, 0x7f, 0x80, CSI_ENTRY, NOTHING);
    sweep(CSI_ENTRY, 0x40, 0x7f, GROUND, CSI_D);
    sweep(CSI_ENTRY, 0x3a, 0x3b, CSI_PARAM, NOTHING);
    sweep_exec(CSI_PARAM);
    sweep(CSI_PARAM, 0x7f, 0x80, CSI_PARAM, NOTHING);
    sweep_exec(CSI_INTERMEDIATE);
    sweep(CSI_INTERMEDIATE, 0x7f, 0x80, CSI_INTERMEDIATE, NOTHING);
    sweep(CSI_INTERMEDIATE, 0x20, 0x30, CSI_INTERMEDIATE, NOTHING);
    sweep(CSI_INTERMEDIATE, 0x30, 0x40, CSI_IGNORE, NOTHING);
    sweep_exec(CSI_IGNORE);
    sweep(CSI_IGNORE, 0x20, 0x40, CSI_IGNORE, NOTHING);
    sweep(CSI_IGNORE, 0x7f, 0x80, CSI_IGNORE, NOTHING);
    sweep(CSI_IGNORE, 0x40, 0x7f, GROUND, NOTHING);
    sweep(CSI_ENTRY, 0x20, 0x30, CSI_INTERMEDIATE, NOTHING);
    sweep(CSI_PARAM, 0x20, 0x30, CSI_INTERMEDIATE, NOTHING);

    rig r;
    rig_open(&r);
    int32_t zz[] = {0, 0}, z1[] = {0, 1};
    for (int s = CSI_ENTRY; s <= CSI_PARAM; s++) {
        for (uint32_t c = '0'; c <= '9'; c++) {
            rig_reset(&r);
            r.p.state = (uint8_t)s;
            one(&r, c);
            int32_t d = (int32_t)(c - '0');
            CHECK_INT(r.p.state, CSI_PARAM);
            CHECK(params_are(&r, &d, 1));
        }
        rig_reset(&r);
        r.p.state = (uint8_t)s;
        one(&r, ';');
        CHECK_INT(r.p.state, CSI_PARAM);
        CHECK(params_are(&r, zz, 2));
    }
    for (uint32_t c = 0x3c; c < 0x40; c++) {
        rig_reset(&r);
        r.p.state = CSI_ENTRY;
        one(&r, c);
        CHECK_INT(r.p.state, CSI_PARAM);
        CHECK_INT(r.p.collect, c);
        /* A prefix after a parameter ignores the sequence. */
        rig_reset(&r);
        r.p.state = CSI_PARAM;
        uint32_t two[] = {';', c};
        cps(&r, two, 2);
        CHECK_INT(r.p.state, CSI_IGNORE);
        CHECK(params_are(&r, zz, 2));
    }
    for (int s = CSI_PARAM; s <= CSI_INTERMEDIATE; s++) {
        for (uint32_t c = 0x40; c < 0x7f; c++) {
            rig_reset(&r);
            r.p.state = (uint8_t)s;
            set_params(&r, z1, 2);
            one(&r, c);
            sk_buf want = {0};
            sk_buf_printf(&want, "csi %x 0 1\n", c);
            CHECK_STR(sk_buf_cstr(&r.log), want.ptr);
            CHECK_INT(r.p.state, GROUND);
            sk_buf_free(&want);
        }
    }
    /* Non-ASCII in CSI_IGNORE stays ignored. */
    rig_reset(&r);
    r.p.state = CSI_IGNORE;
    str(&r, "\xE2\x82\xAC\xC3\xB6\xC3\xA4\xC3\xBC");
    CHECK_INT(r.p.state, CSI_IGNORE);
    CHECK_STR(sk_buf_cstr(&r.log), "");
    rig_close(&r);
}

static void test_strings(void)
{
    uint32_t ex[32];
    int n = executables(ex);
    for (int i = 0; i < n; i++)
        sweep(SOS_PM_APC_STRING, ex[i], ex[i] + 1, SOS_PM_APC_STRING, NOTHING);
    sweep(SOS_PM_APC_STRING, 0x20, 0x80, SOS_PM_APC_STRING, NOTHING);
    for (int i = 0; i < n; i++)
        if (ex[i] != 0x07)
            sweep(OSC_STRING, ex[i], ex[i] + 1, OSC_STRING, NOTHING);
    sweep(OSC_STRING, 0x20, 0x80, OSC_STRING, NOTHING);
}

static void test_dcs(void)
{
    uint32_t ex[32];
    int n = executables(ex);
    const int quiet[] = {DCS_ENTRY, DCS_PARAM, DCS_INTERMEDIATE};
    for (int k = 0; k < 3; k++) {
        for (int i = 0; i < n; i++)
            sweep(quiet[k], ex[i], ex[i] + 1, quiet[k], NOTHING);
        sweep(quiet[k], 0x7f, 0x80, quiet[k], NOTHING);
    }
    for (int i = 0; i < n; i++)
        sweep(DCS_IGNORE, ex[i], ex[i] + 1, DCS_IGNORE, NOTHING);
    sweep(DCS_IGNORE, 0x20, 0x80, DCS_IGNORE, NOTHING);
    sweep(DCS_ENTRY, 0x3a, 0x3b, DCS_PARAM, NOTHING);
    sweep(DCS_INTERMEDIATE, 0x30, 0x40, DCS_IGNORE, NOTHING);
    sweep(DCS_ENTRY, 0x20, 0x30, DCS_INTERMEDIATE, NOTHING);
    sweep(DCS_PARAM, 0x20, 0x30, DCS_INTERMEDIATE, NOTHING);
    sweep(DCS_INTERMEDIATE, 0x20, 0x30, DCS_INTERMEDIATE, NOTHING);
    for (int k = 0; k < 3; k++)
        sweep(quiet[k], 0x40, 0x7f, DCS_PASSTHROUGH, NOTHING);
    for (int i = 0; i < n; i++)
        if (ex[i] != 0x18 && ex[i] != 0x1a)
            sweep(DCS_PASSTHROUGH, ex[i], ex[i] + 1, DCS_PASSTHROUGH, NOTHING);
    sweep(DCS_PASSTHROUGH, 0x20, 0x80, DCS_PASSTHROUGH, NOTHING);

    rig r;
    rig_open(&r);
    int32_t zz[] = {0, 0};
    for (int s = DCS_ENTRY; s <= DCS_PARAM; s++) {
        for (uint32_t c = '0'; c <= '9'; c++) {
            rig_reset(&r);
            r.p.state = (uint8_t)s;
            one(&r, c);
            int32_t d = (int32_t)(c - '0');
            CHECK_INT(r.p.state, DCS_PARAM);
            CHECK(params_are(&r, &d, 1));
        }
        rig_reset(&r);
        r.p.state = (uint8_t)s;
        one(&r, ';');
        CHECK(params_are(&r, zz, 2));
    }
    for (uint32_t c = 0x3c; c < 0x40; c++) {
        rig_reset(&r);
        r.p.state = DCS_ENTRY;
        one(&r, c);
        CHECK_INT(r.p.state, DCS_PARAM);
        CHECK_INT(r.p.collect, c);
        rig_reset(&r);
        r.p.state = DCS_PARAM;
        uint32_t two[] = {';', c};
        cps(&r, two, 2);
        CHECK_INT(r.p.state, DCS_IGNORE);
        CHECK(params_are(&r, zz, 2));
    }
    for (uint32_t c = 0x30; c < 0x40; c++) {
        rig_reset(&r);
        r.p.state = DCS_INTERMEDIATE;
        uint32_t two[] = {' ', c};
        cps(&r, two, 2);
        CHECK_INT(r.p.state, DCS_IGNORE);
        CHECK_INT(r.p.collect, ' ');
    }
    rig_reset(&r);
    r.p.state = DCS_IGNORE;
    str(&r, "\xE2\x82\xAC\xC3\xB6\xC3\xA4\xC3\xBC");
    CHECK_INT(r.p.state, DCS_IGNORE);
    rig_reset(&r);
    r.p.state = DCS_PASSTHROUGH;
    str(&r, "\xC2\x90" "1;2;3+$a\xE2\x82\xAC\xC3\xB6\xC3\xA4\xC3\xBC");
    CHECK_INT(r.p.state, DCS_PASSTHROUGH);
    rig_close(&r);
}

#define EXAMPLE(input, want)                                                 \
    do {                                                                     \
        rig r_;                                                              \
        rig_open(&r_);                                                       \
        str(&r_, input);                                                     \
        CHECK_STR(sk_buf_cstr(&r_.log), want);                               \
        rig_close(&r_);                                                      \
    } while (0)

#define UMLAUTS "\xC3\xB6\xC3\xA4\xC3\xBC"
#define EURO "\xE2\x82\xAC"

static void test_examples(void)
{
    EXAMPLE("\x1b[<31;5mHello World! " UMLAUTS EURO "\nabc",
            "csi 3c6d 31 5\nprint Hello World! " UMLAUTS EURO "\nexe a\nprint abc\n");
    EXAMPLE("\x1b]0;abc123" EURO UMLAUTS "\x07", "osc 0;abc123" EURO UMLAUTS "\n");
    EXAMPLE("\x1bP1;2;3+$a\xC3\xA4" "bc;d\xC3\xA4" "e\xC2\x9C", "dcs 2b2461 1 2 3 [\xC3\xA4" "bc;d\xC3\xA4" "e]\n");
    {
        rig r;
        rig_open(&r);
        str(&r, "\x1bP1;2;3+$abc;de");
        CHECK_STR(sk_buf_cstr(&r.log), "");
        str(&r, "abc\xC2\x9C");
        CHECK_STR(sk_buf_cstr(&r.log), "dcs 2b2461 1 2 3 [bc;deabc]\n");
        rig_close(&r);
    }
    EXAMPLE("abc\xC2\x90" "1;2;3+$abc;de\xC2\x9C", "print abc\ndcs 2b2461 1 2 3 [bc;de]\n");
    EXAMPLE("abc\xC2\x98" "123tzf\xC2\x9C" "defg", "print abc\nprint defg\n");
    EXAMPLE("abc\xC2\x9D" "123;tzf\xC2\x9C" "defg", "print abc\nosc 123;tzf\nprint defg\n");
    EXAMPLE("\x1b[1" EURO "abcdefg\xC2\x9B<;c", "print abcdefg\ncsi 3c63 0 0\n");
    EXAMPLE("abc\xC2\x9D" "123;tzf\x1b\\defg", "print abc\nosc 123;tzf\nesc 5c\nprint defg\n");
    EXAMPLE("\x1b[<31;5::123:;8mHello World! " UMLAUTS EURO "\nabc",
            "csi 3c6d 31 5:-1:123:-1 8\nprint Hello World! " UMLAUTS EURO "\nexe a\nprint abc\n");
    EXAMPLE("abc\xC2\x90" "1;2::55;3+$abc;de\xC2\x9C", "print abc\ndcs 2b2461 1 2:-1:55 3 [bc;de]\n");
    EXAMPLE("abc\xC2\x90" "1;2::55;3+$abc;de\x18", "print abc\n");
    EXAMPLE("abc\xC2\x90" "1;2::55;3+$abc;de\x1a", "print abc\n");
    EXAMPLE("\x1b]0;abc123" EURO UMLAUTS "\x18", "");
    EXAMPLE("\x1b]0;abc123" EURO UMLAUTS "\x1a", "");
}

const sk_test sk_suite_term_xparser[] = {
    T(test_ground),
    T(test_anywhere),
    T(test_escape),
    T(test_csi),
    T(test_strings),
    T(test_dcs),
    T(test_examples),
    {0},
};
