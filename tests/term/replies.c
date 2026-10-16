/* What the terminal answers, byte for byte as xterm.js 6.0.0 answers:
 * DA1, DA2, DSR and CPR, DECRQM, DECRQSS and the OSC colour reports. */
#include "screen.h"

static void test_device_attributes(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "\x1b[c");
    CHECK_REPLY(&x, "\x1b[?1;2c");
    tt_feed(&x, "\x1b[0c");
    CHECK_REPLY(&x, "\x1b[?1;2c");
    tt_feed(&x, "\x1b[1c");
    CHECK_REPLY(&x, "");
    tt_feed(&x, "\x1b[>c\x1b[>0c");
    CHECK_REPLY(&x, "\x1b[>0;276;0c\x1b[>0;276;0c");
    tt_feed(&x, "\x1b[>1c");
    CHECK_REPLY(&x, "");
    tt_close(&x);
}

static void test_status(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "\x1b[5n");
    CHECK_REPLY(&x, "\x1b[0n");
    tt_feed(&x, "\x1b[6n");
    CHECK_REPLY(&x, "\x1b[1;1R");
    tt_feed(&x, "ab\r\ncde\x1b[6n");
    CHECK_REPLY(&x, "\x1b[2;4R");
    tt_feed(&x, "\x1b[?6n");
    CHECK_REPLY(&x, "\x1b[?2;4R");
    /* The pending wrap reports one past the last column. */
    tt_feed(&x, "\r0123456789\x1b[6n");
    CHECK_REPLY(&x, "\x1b[2;11R");
    /* Other DEC status requests stay unanswered. */
    tt_feed(&x, "\x1b[?15n\x1b[?26n\x1b[7n");
    CHECK_REPLY(&x, "");
    tt_close(&x);
}

static void test_decrqm(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "\x1b[?25$p\x1b[?2004$p\x1b[?2004h\x1b[?2004$p");
    CHECK_REPLY(&x, "\x1b[?25;1$y\x1b[?2004;2$y\x1b[?2004;1$y");
    tt_feed(&x, "\x1b[?1049$p\x1b[?1049h\x1b[?47$p\x1b[?1049l");
    CHECK_REPLY(&x, "\x1b[?1049;2$y\x1b[?47;1$y");
    tt_feed(&x, "\x1b[?1$p\x1b[?7$p\x1b[?8$p\x1b[?67$p\x1b[?1048$p\x1b[?1005$p\x1b[?3$p\x1b[?9999$p");
    CHECK_REPLY(&x, "\x1b[?1;2$y\x1b[?7;1$y\x1b[?8;3$y\x1b[?67;4$y\x1b[?1048;1$y\x1b[?1005;4$y\x1b[?3;0$y"
                    "\x1b[?9999;0$y");
    tt_feed(&x, "\x1b[?1002h\x1b[?1000$p\x1b[?1002$p\x1b[?1006h\x1b[?1006$p\x1b[?1016$p\x1b[?2026$p");
    CHECK_REPLY(&x, "\x1b[?1000;2$y\x1b[?1002;1$y\x1b[?1006;1$y\x1b[?1016;2$y\x1b[?2026;2$y");
    /* ANSI modes. */
    tt_feed(&x, "\x1b[4$p\x1b[4h\x1b[4$p\x1b[2$p\x1b[12$p\x1b[20$p\x1b[3$p");
    CHECK_REPLY(&x, "\x1b[4;2$y\x1b[4;1$y\x1b[2;4$y\x1b[12;3$y\x1b[20;2$y\x1b[3;0$y");
    tt_close(&x);
}

static void test_decrqss(void)
{
    tt x;
    tt_open(&x, 10, 24);
    tt_feed(&x, "\x1bP$qm\x1b\\");
    CHECK_REPLY(&x, "\x1bP1$r0m\x1b\\");
    tt_feed(&x, "\x1b[3;20r\x1bP$qr\x1b\\");
    CHECK_REPLY(&x, "\x1bP1$r3;20r\x1b\\");
    tt_feed(&x, "\x1b[5 q\x1bP$q q\x1b\\");
    CHECK_REPLY(&x, "\x1bP1$r2 q\x1b\\");
    tt_feed(&x, "\x1b[?12h\x1bP$q q\x1b\\");
    CHECK_REPLY(&x, "\x1bP1$r1 q\x1b\\");
    tt_feed(&x, "\x1bP$q\"q\x1b\\\x1b[1\"q\x1bP$q\"q\x1b\\");
    CHECK_REPLY(&x, "\x1bP1$r0\"q\x1b\\\x1bP1$r1\"q\x1b\\");
    tt_feed(&x, "\x1bP$q\"p\x1b\\\x1bP$qx\x1b\\");
    CHECK_REPLY(&x, "\x1bP1$r61;1\"p\x1b\\\x1bP0$r\x1b\\");
    /* Other DCS strings are ignored. */
    tt_feed(&x, "\x1bP+q544e\x1b\\\x1bPq#0;2;0;0;0\x1b\\");
    CHECK_REPLY(&x, "");
    tt_close(&x);
}

static void test_color_reports(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "\x1b]10;?\x07\x1b]11;?\x07\x1b]12;?\x07");
    CHECK_REPLY(&x, "\x1b]10;rgb:ffff/ffff/ffff\x1b\\\x1b]11;rgb:0000/0000/0000\x1b\\\x1b]12;rgb:ffff/ffff/ffff\x1b\\");
    tt_feed(&x, "\x1b]4;1;?\x07");
    CHECK_REPLY(&x, "\x1b]4;1;rgb:cccc/0000/0000\x1b\\");
    tt_feed(&x, "\x1b]4;196;?;232;?\x07");
    CHECK_REPLY(&x, "\x1b]4;196;rgb:ffff/0000/0000\x1b\\\x1b]4;232;rgb:0808/0808/0808\x1b\\");
    /* Stacked: 10 then 11, and nothing past 12. */
    tt_feed(&x, "\x1b]10;?;?\x07\x1b]12;?;?\x07");
    CHECK_REPLY(&x, "\x1b]10;rgb:ffff/ffff/ffff\x1b\\\x1b]11;rgb:0000/0000/0000\x1b\\\x1b]12;rgb:ffff/ffff/ffff\x1b\\");
    tt_close(&x);
}

static void test_color_set(void)
{
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "\x1b]4;1;#123456;1;?\x07");
    CHECK_REPLY(&x, "\x1b]4;1;rgb:1212/3434/5656\x1b\\");
    tt_feed(&x, "\x1b]4;2;rgb:f/0/8;2;?\x07");
    CHECK_REPLY(&x, "\x1b]4;2;rgb:ffff/0000/8888\x1b\\");
    tt_feed(&x, "\x1b]4;3;#fff;3;?\x07");
    CHECK_REPLY(&x, "\x1b]4;3;rgb:f0f0/f0f0/f0f0\x1b\\");
    tt_feed(&x, "\x1b]4;4;#123456789;4;?\x07");
    CHECK_REPLY(&x, "\x1b]4;4;rgb:1212/4545/7878\x1b\\");
    tt_feed(&x, "\x1b]4;5;RGB:FFFF/8000/0000;5;?\x07");
    CHECK_REPLY(&x, "\x1b]4;5;rgb:ffff/8080/0000\x1b\\");
    /* Bad specs and indexes change nothing. */
    tt_feed(&x, "\x1b]4;6;red;6;?;256;#000000;x;#000000;7\x07");
    CHECK_REPLY(&x, "\x1b]4;6;rgb:0606/9898/9a9a\x1b\\");
    tt_feed(&x, "\x1b]4;6;rgb:1/22/3;6;?\x07");
    CHECK_REPLY(&x, "\x1b]4;6;rgb:0606/9898/9a9a\x1b\\");
    tt_feed(&x, "\x1b]10;#010203\x07\x1b]11;rgb:0a/0b/0c\x07\x1b]10;?;?\x07");
    CHECK_REPLY(&x, "\x1b]10;rgb:0101/0202/0303\x1b\\\x1b]11;rgb:0a0a/0b0b/0c0c\x1b\\");
    sk_term_palette p;
    sk_term_palette_of(x.t, &p);
    CHECK_INT(p.ansi[1], 0x123456);
    CHECK_INT(p.fg, 0x010203);
    tt_close(&x);
}

static void test_color_restore(void)
{
    tt x;
    tt_open(&x, 10, 3);
    sk_term_palette p;
    sk_term_default_palette(&p);
    p.ansi[1] = 0xaa0000;
    p.bg = 0x101010;
    sk_term_set_palette(x.t, &p);
    tt_feed(&x, "\x1b]4;1;#000001;2;#000002\x07\x1b]11;#202020\x07");
    tt_feed(&x, "\x1b]104;1\x07\x1b]4;1;?;2;?\x07");
    CHECK_REPLY(&x, "\x1b]4;1;rgb:aaaa/0000/0000\x1b\\\x1b]4;2;rgb:0000/0000/0202\x1b\\");
    tt_feed(&x, "\x1b]104\x07\x1b]4;2;?\x07");
    CHECK_REPLY(&x, "\x1b]4;2;rgb:4e4e/9a9a/0606\x1b\\");
    /* 104 leaves the special colours; 111 restores the background. */
    tt_feed(&x, "\x1b]11;?\x07\x1b]111\x07\x1b]11;?\x07");
    CHECK_REPLY(&x, "\x1b]11;rgb:2020/2020/2020\x1b\\\x1b]11;rgb:1010/1010/1010\x1b\\");
    tt_close(&x);
}

static void test_no_window_ops(void)
{
    /* xterm.js answers no CSI t: windowOptions are all off. */
    tt x;
    tt_open(&x, 10, 3);
    tt_feed(&x, "\x1b[18t\x1b[14t\x1b[22;0t\x1b[23;0t");
    CHECK_REPLY(&x, "");
    tt_close(&x);
}

const sk_test sk_suite_term_replies[] = {
    T(test_device_attributes),
    T(test_status),
    T(test_decrqm),
    T(test_decrqss),
    T(test_color_reports),
    T(test_color_set),
    T(test_color_restore),
    T(test_no_window_ops),
    {0},
};
