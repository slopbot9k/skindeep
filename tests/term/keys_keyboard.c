/* Keys to bytes as xterm.js 6.0.0 sent them in the Electron app, with
 * macOptionIsMeta false. Each row gives the bytes on Linux and on macOS;
 * both are checked on every system through sk_term_key_as, and the
 * build's column through sk_term_key. "" is nothing at all. The rows
 * include every case of xterm.js's Keyboard.test.ts. */
#include "../../src/term/keys.h"
#include "../test.h"

#include <stdio.h>

#define PGUP "<page up>"
#define PGDN "<page down>"
#define S SK_MOD_SHIFT
#define A SK_MOD_ALT
#define C SK_MOD_CTRL
#define M SK_MOD_SUPER
#define CAPS SK_MOD_CAPS
#define E "\x1b"

typedef struct {
    int line;
    sk_key key;
    uint32_t base;
    const char *text;
    uint32_t mods;
    bool app_cursor;
    const char *on_linux, *on_mac;
} key_case;

/* A named key, in normal or application cursor mode. */
#define K(key, mods, lin, mac) {__LINE__, key, 0, "", mods, false, lin, mac}
#define K2(key, mods, both) K(key, mods, both, both)
#define KA(key, mods, lin, mac) {__LINE__, key, 0, "", mods, true, lin, mac}
#define KA2(key, mods, both) KA(key, mods, both, both)
/* A key that types text: its unshifted character and what it types. */
#define TX(base, text, mods, lin, mac) {__LINE__, SK_KEY_NONE, base, text, mods, false, lin, mac}
#define TX2(base, text, mods, both) TX(base, text, mods, both, both)
#define KP(key, text, mods, lin, mac) {__LINE__, key, 0, text, mods, false, lin, mac}
#define KP2(key, text, mods, both) KP(key, text, mods, both, both)

/* The arrows: CSI 1;m X with modifiers, nothing with Super or Cmd. */
#define ARROW(key, f)                                                                                         \
    K2(key, 0, E "[" f), K2(key, S, E "[1;2" f), K2(key, A, E "[1;3" f), K2(key, C, E "[1;5" f),              \
        K2(key, S | A, E "[1;4" f), K2(key, S | C, E "[1;6" f), K2(key, A | C, E "[1;7" f),                    \
        K2(key, S | A | C, E "[1;8" f), K2(key, M, ""), K2(key, M | C, ""), K2(key, CAPS, E "[" f),            \
        KA2(key, 0, E "O" f), KA2(key, S, E "[1;2" f), KA2(key, C, E "[1;5" f), KA2(key, M, "")

/* Home and End: as the arrows, but Super counts as a modifier. */
#define HOME_END(key, f)                                                                                      \
    K2(key, 0, E "[" f), K2(key, S, E "[1;2" f), K2(key, A, E "[1;3" f), K2(key, C, E "[1;5" f),              \
        K2(key, S | A, E "[1;4" f), K2(key, S | C, E "[1;6" f), K2(key, A | C, E "[1;7" f),                    \
        K2(key, S | A | C, E "[1;8" f), K(key, M, E "[1;9" f, ""), K(key, M | C, E "[1;13" f, ""),             \
        KA2(key, 0, E "O" f), KA2(key, A, E "[1;3" f)

/* F1 to F4: SS3 P to S, CSI 1;m P to S with modifiers. Option alone on
 * macOS is a third-level shift there, which sends nothing. */
#define F_SS3(key, f)                                                                                         \
    K2(key, 0, E "O" f), K2(key, S, E "[1;2" f), K(key, A, E "[1;3" f, ""), K2(key, C, E "[1;5" f),           \
        K(key, S | A, E "[1;4" f, ""), K2(key, S | C, E "[1;6" f), K2(key, A | C, E "[1;7" f),                 \
        K2(key, S | A | C, E "[1;8" f), K(key, M, E "[1;9" f, ""), K(key, M | C, E "[1;13" f, ""),             \
        KA2(key, 0, E "O" f)

/* F5 to F12: CSI n ~ and CSI n;m ~. */
#define F_CSI(key, n)                                                                                         \
    K2(key, 0, E "[" n "~"), K2(key, S, E "[" n ";2~"), K(key, A, E "[" n ";3~", ""),                          \
        K2(key, C, E "[" n ";5~"), K(key, S | A, E "[" n ";4~", ""), K2(key, S | C, E "[" n ";6~"),            \
        K2(key, A | C, E "[" n ";7~"), K2(key, S | A | C, E "[" n ";8~"), K(key, M, E "[" n ";9~", ""),        \
        K(key, M | C, E "[" n ";13~", "")

static const key_case named_keys[] = {
    ARROW(SK_KEY_UP, "A"),
    ARROW(SK_KEY_DOWN, "B"),
    ARROW(SK_KEY_RIGHT, "C"),
    ARROW(SK_KEY_LEFT, "D"),
    HOME_END(SK_KEY_HOME, "H"),
    HOME_END(SK_KEY_END, "F"),
    F_SS3(SK_KEY_F1, "P"),
    F_SS3(SK_KEY_F1 + 1, "Q"),
    F_SS3(SK_KEY_F1 + 2, "R"),
    F_SS3(SK_KEY_F1 + 3, "S"),
    F_CSI(SK_KEY_F1 + 4, "15"),
    F_CSI(SK_KEY_F1 + 5, "17"),
    F_CSI(SK_KEY_F1 + 6, "18"),
    F_CSI(SK_KEY_F1 + 7, "19"),
    F_CSI(SK_KEY_F1 + 8, "20"),
    F_CSI(SK_KEY_F1 + 9, "21"),
    F_CSI(SK_KEY_F1 + 10, "23"),
    F_CSI(SK_KEY_F1 + 11, "24"),

    /* Insert: nothing with Shift or Ctrl, which copy and paste on some
     * systems; no modifier parameter otherwise. */
    K2(SK_KEY_INSERT, 0, E "[2~"),
    K2(SK_KEY_INSERT, S, ""),
    K2(SK_KEY_INSERT, A, E "[2~"),
    K2(SK_KEY_INSERT, C, ""),
    K2(SK_KEY_INSERT, S | A, ""),
    K2(SK_KEY_INSERT, S | C, ""),
    K2(SK_KEY_INSERT, A | C, ""),
    K2(SK_KEY_INSERT, S | A | C, ""),
    K(SK_KEY_INSERT, M, E "[2~", ""),
    K2(SK_KEY_INSERT, M | C, ""),
    KA2(SK_KEY_INSERT, 0, E "[2~"),

    K2(SK_KEY_DELETE, 0, E "[3~"),
    K2(SK_KEY_DELETE, S, E "[3;2~"),
    K2(SK_KEY_DELETE, A, E "[3;3~"),
    K2(SK_KEY_DELETE, C, E "[3;5~"),
    K2(SK_KEY_DELETE, S | A, E "[3;4~"),
    K2(SK_KEY_DELETE, S | C, E "[3;6~"),
    K2(SK_KEY_DELETE, A | C, E "[3;7~"),
    K2(SK_KEY_DELETE, S | A | C, E "[3;8~"),
    K(SK_KEY_DELETE, M, E "[3;9~", ""),
    K(SK_KEY_DELETE, M | C, E "[3;13~", ""),

    /* Page Up and Down: Shift scrolls the view; only Ctrl brings a
     * modifier parameter. */
    K2(SK_KEY_PAGE_UP, 0, E "[5~"),
    K2(SK_KEY_PAGE_UP, S, PGUP),
    K2(SK_KEY_PAGE_UP, A, E "[5~"),
    K2(SK_KEY_PAGE_UP, C, E "[5;5~"),
    K2(SK_KEY_PAGE_UP, S | A, PGUP),
    K2(SK_KEY_PAGE_UP, S | C, PGUP),
    K2(SK_KEY_PAGE_UP, A | C, E "[5;7~"),
    K2(SK_KEY_PAGE_UP, S | A | C, PGUP),
    K(SK_KEY_PAGE_UP, M, E "[5~", ""),
    K(SK_KEY_PAGE_UP, M | C, E "[5;13~", ""),
    K(SK_KEY_PAGE_UP, M | S, PGUP, ""),
    K2(SK_KEY_PAGE_DOWN, 0, E "[6~"),
    K2(SK_KEY_PAGE_DOWN, S, PGDN),
    K2(SK_KEY_PAGE_DOWN, A, E "[6~"),
    K2(SK_KEY_PAGE_DOWN, C, E "[6;5~"),
    K2(SK_KEY_PAGE_DOWN, S | A, PGDN),
    K2(SK_KEY_PAGE_DOWN, S | C, PGDN),
    K2(SK_KEY_PAGE_DOWN, A | C, E "[6;7~"),
    K2(SK_KEY_PAGE_DOWN, S | A | C, PGDN),
    K(SK_KEY_PAGE_DOWN, M, E "[6~", ""),
    K(SK_KEY_PAGE_DOWN, M | C, E "[6;13~", ""),

    /* Backspace: DEL, ^H with Ctrl, ESC before either with Alt. */
    K2(SK_KEY_BACKSPACE, 0, "\x7f"),
    K2(SK_KEY_BACKSPACE, S, "\x7f"),
    K2(SK_KEY_BACKSPACE, A, E "\x7f"),
    K2(SK_KEY_BACKSPACE, C, "\b"),
    K2(SK_KEY_BACKSPACE, S | A, E "\x7f"),
    K2(SK_KEY_BACKSPACE, S | C, "\b"),
    K2(SK_KEY_BACKSPACE, A | C, E "\b"),
    K2(SK_KEY_BACKSPACE, S | A | C, E "\b"),
    K(SK_KEY_BACKSPACE, M, "\x7f", ""),
    K(SK_KEY_BACKSPACE, M | C, "\b", ""),

    K2(SK_KEY_TAB, 0, "\t"),
    K2(SK_KEY_TAB, S, E "[Z"),
    K2(SK_KEY_TAB, A, "\t"),
    K2(SK_KEY_TAB, C, "\t"),
    K2(SK_KEY_TAB, S | A, E "[Z"),
    K2(SK_KEY_TAB, S | C, E "[Z"),
    K2(SK_KEY_TAB, A | C, "\t"),
    K2(SK_KEY_TAB, S | A | C, E "[Z"),
    K(SK_KEY_TAB, M, "\t", ""),
    K(SK_KEY_TAB, M | C, "\t", ""),

    K2(SK_KEY_ENTER, 0, "\r"),
    K2(SK_KEY_ENTER, S, "\r"),
    K2(SK_KEY_ENTER, A, E "\r"),
    K2(SK_KEY_ENTER, C, "\r"),
    K2(SK_KEY_ENTER, S | A, E "\r"),
    K2(SK_KEY_ENTER, S | C, "\r"),
    K2(SK_KEY_ENTER, A | C, E "\r"),
    K2(SK_KEY_ENTER, S | A | C, E "\r"),
    K(SK_KEY_ENTER, M, "\r", ""),
    K(SK_KEY_ENTER, M | C, "\r", ""),
    K2(SK_KEY_KP_ENTER, 0, "\r"),
    K2(SK_KEY_KP_ENTER, S, "\r"),
    K2(SK_KEY_KP_ENTER, A, E "\r"),
    K2(SK_KEY_KP_ENTER, C, "\r"),

    K2(SK_KEY_ESCAPE, 0, E),
    K2(SK_KEY_ESCAPE, S, E),
    K2(SK_KEY_ESCAPE, A, E E),
    K2(SK_KEY_ESCAPE, C, E),
    K2(SK_KEY_ESCAPE, S | A, E E),
    K2(SK_KEY_ESCAPE, S | C, E),
    K2(SK_KEY_ESCAPE, A | C, E E),
    K2(SK_KEY_ESCAPE, S | A | C, E E),
    K(SK_KEY_ESCAPE, M, E, ""),
    K(SK_KEY_ESCAPE, M | C, E, ""),

    K2(SK_KEY_MENU, 0, ""),
    K2(SK_KEY_MENU, S, ""),
    K2(SK_KEY_MENU, A, ""),
    K2(SK_KEY_MENU, C, ""),
};

static const key_case text_keys[] = {
    /* Letters. Option types the layout's character on macOS; Alt is an
     * ESC before the letter on Linux, whatever the layout types. */
    TX2('a', "a", 0, "a"),
    TX2('a', "A", S, "A"),
    TX2('a', "A", CAPS, "A"),
    TX2('a', "a", S | CAPS, "a"),
    TX2('a', "a", C, "\x01"),
    TX2('a', "A", C | CAPS, "\x01"),
    TX2('a', "A", S | C, ""),
    TX('a', "å", A, E "a", "å"),
    TX('a', "Å", S | A, E "A", "Å"),
    TX('a', "å", A | CAPS, E "a", "å"),
    TX('a', "a", A | C, E "\x01", ""),
    TX('a', "A", S | A | C, E "\x01", ""),
    TX2('a', "a", M, ""),
    TX2('a', "a", M | S, ""),
    TX2('a', "a", M | C, ""),
    TX2('a', "a", M | A, ""),
    TX('h', "˙", A, E "h", "˙"),
    TX('h', "Ó", S | A, E "H", "Ó"),
    TX('z', "Ω", A, E "z", "Ω"),
    TX('z', "¸", S | A, E "Z", "¸"),
    /* Option+E, U, I, N and ` are dead keys on the US layout. */
    TX('e', "", A, E "e", ""),
    TX('n', "", A, E "n", ""),
    TX('e', "", 0, "", ""),
    TX2('c', "c", C, "\x03"),
    TX2('c', "C", S | C, ""),
    TX2('v', "v", C, "\x16"),
    TX2('v', "V", S | C, ""),
    TX2('z', "z", C, "\x1a"),

    /* Digits: Ctrl+3 to Ctrl+8 are ESC, FS, GS, RS, US and DEL; Ctrl+2,
     * 9 and 0 send nothing. */
    TX2('1', "1", 0, "1"),
    TX2('1', "!", S, "!"),
    TX2('1', "1", C, ""),
    TX2('2', "2", C, ""),
    TX2('3', "3", C, "\x1b"),
    TX2('4', "4", C, "\x1c"),
    TX2('5', "5", C, "\x1d"),
    TX2('6', "6", C, "\x1e"),
    TX2('7', "7", C, "\x1f"),
    TX2('8', "8", C, "\x7f"),
    TX2('9', "9", C, ""),
    TX2('0', "0", C, ""),
    TX2('6', "^", S | C, ""),
    TX2('3', "#", S | C, ""),
    TX('3', "3", A | C, E "3", ""),
    TX('3', "#", S | A | C, E "#", ""),
    TX('0', "º", A, E "0", "º"),
    TX('0', "‚", S | A, E ")", "‚"),
    TX('1', "¡", A, E "1", "¡"),
    TX('1', "⁄", S | A, E "!", "⁄"),
    TX('2', "™", A, E "2", "™"),
    TX('2', "€", S | A, E "@", "€"),
    TX('3', "£", A, E "3", "£"),
    TX('3', "‹", S | A, E "#", "‹"),
    TX('4', "¢", A, E "4", "¢"),
    TX('4', "›", S | A, E "$", "›"),
    TX('5', "∞", A, E "5", "∞"),
    TX('5', "ﬁ", S | A, E "%", "ﬁ"),
    TX('6', "§", A, E "6", "§"),
    TX('6', "ﬂ", S | A, E "^", "ﬂ"),
    TX('7', "¶", A, E "7", "¶"),
    TX('7', "‡", S | A, E "&", "‡"),
    TX('8', "•", A, E "8", "•"),
    TX('8', "°", S | A, E "*", "°"),
    TX('9', "ª", A, E "9", "ª"),
    TX('9', "·", S | A, E "(", "·"),

    /* Punctuation. Ctrl+[ \ ] are ESC, FS, GS; Ctrl+Shift+- is US. */
    TX2(';', ";", 0, ";"),
    TX2(';', ":", S, ":"),
    TX2(';', ";", C, ""),
    TX(';', "…", A, E ";", "…"),
    TX(';', "Ú", S | A, E ":", "Ú"),
    TX2('=', "=", C, ""),
    TX('=', "≠", A, E "=", "≠"),
    TX('=', "±", S | A, E "+", "±"),
    TX2(',', ",", C, ""),
    TX(',', "≤", A, E ",", "≤"),
    TX(',', "¯", S | A, E "<", "¯"),
    TX2('-', "-", 0, "-"),
    TX2('-', "_", S, "_"),
    TX2('-', "-", C, ""),
    TX2('-', "_", S | C, "\x1f"),
    TX('-', "–", A, E "-", "–"),
    TX('-', "—", S | A, E "_", "—"),
    TX2('.', ".", C, ""),
    TX('.', "≥", A, E ".", "≥"),
    TX('.', "˘", S | A, E ">", "˘"),
    TX2('/', "/", C, ""),
    TX2('/', "?", S | C, ""),
    TX('/', "÷", A, E "/", "÷"),
    TX('/', "¿", S | A, E "?", "¿"),
    TX2('`', "`", C, ""),
    TX('`', "", A, E "`", ""),
    TX('`', "`", S | A, E "~", "`"),
    TX2('[', "[", C, "\x1b"),
    TX2('[', "{", S | C, ""),
    TX('[', "“", A, E "[", "“"),
    TX('[', "”", S | A, E "{", "”"),
    TX2('\\', "\\", C, "\x1c"),
    TX('\\', "«", A, E "\\", "«"),
    TX('\\', "»", S | A, E "|", "»"),
    TX2(']', "]", C, "\x1d"),
    TX(']', "‘", A, E "]", "‘"),
    TX(']', "’", S | A, E "}", "’"),
    TX2('\'', "'", C, ""),
    TX('\'', "æ", A, E "'", "æ"),
    TX('\'', "Æ", S | A, E "\"", "Æ"),
    TX('[', "[", A | C, E "[", ""),

    /* Space. Option+Space types a no-break space on macOS. */
    TX2(' ', " ", 0, " "),
    TX2(' ', " ", S, " "),
    TX(' ', "\xc2\xa0", A, E " ", "\xc2\xa0"),
    TX(' ', " ", S | A, E " ", " "),
    TX2(' ', " ", S | C, ""),
    TX2(' ', " ", M, ""),

    /* The keypad, which application keypad mode does not change. */
    KP2(SK_KEY_KP_0, "0", 0, "0"),
    KP2(SK_KEY_KP_0 + 1, "1", 0, "1"),
    KP2(SK_KEY_KP_9, "9", 0, "9"),
    KP2(SK_KEY_KP_0 + 1, "1", S, "1"),
    KP2(SK_KEY_KP_0 + 1, "1", C, ""),
    KP(SK_KEY_KP_0 + 1, "1", A, "", "1"),
    KP(SK_KEY_KP_0 + 1, "1", S | A, "", "1"),
    KP2(SK_KEY_KP_0 + 1, "1", A | C, ""),
    KP2(SK_KEY_KP_0 + 1, "1", M, ""),
    KP2(SK_KEY_KP_DECIMAL, ".", 0, "."),
    KP(SK_KEY_KP_DECIMAL, ".", A, "", "."),
    KP2(SK_KEY_KP_DIVIDE, "/", 0, "/"),
    KP2(SK_KEY_KP_DIVIDE, "/", C, ""),
    KP2(SK_KEY_KP_MULTIPLY, "*", 0, "*"),
    KP2(SK_KEY_KP_SUBTRACT, "-", 0, "-"),
    KP2(SK_KEY_KP_SUBTRACT, "-", C, ""),
    KP2(SK_KEY_KP_ADD, "+", 0, "+"),
    KP2(SK_KEY_KP_ADD, "+", S, "+"),
    KP(SK_KEY_KP_ADD, "+", A, "", "+"),
    KP2(SK_KEY_KP_EQUAL, "=", 0, "="),
    KP(SK_KEY_KP_EQUAL, "=", A, E "=", "="),
    KP(SK_KEY_KP_EQUAL, "=", S | A, E "+", "="),

    /* Beyond ASCII: a key with no US keyCode types its text, and Ctrl
     * or Alt on Linux leaves it nothing. AltGr is not Alt. */
    TX2(0xE9, "é", 0, "é"),
    TX2(0xE9, "É", S, "É"),
    TX2(0xE9, "é", C, ""),
    TX(0xE9, "é", A, "", "é"),
    TX2(0x441, "с", 0, "с"),
    TX2(0x441, "с", C, ""),
    TX2('q', "@", 0, "@"),
    TX2('e', "€", 0, "€"),
    TX2(0, "😀", 0, "😀"),
    TX2(0, "ab", 0, "ab"),
    TX2(0, "", 0, ""),
    TX2(0xB4, "", 0, ""),
};

/* The bytes, escaped for a message. */
static const char *show(const char *s, size_t n, char *buf, size_t cap)
{
    size_t k = 0;
    buf[0] = 0;
    for (size_t i = 0; i < n && k + 5 < cap; i++) {
        unsigned char c = (unsigned char)s[i];
        k += (size_t)snprintf(buf + k, cap - k, c >= 0x20 && c < 0x7f ? "%c" : "\\x%02x", c);
    }
    return buf;
}

static void check_one(int line, const char *where, sk_term_key_result r, const sk_buf *out, const char *want)
{
    const char *got = r == SK_TERM_KEY_PAGE_UP     ? PGUP
                      : r == SK_TERM_KEY_PAGE_DOWN ? PGDN
                      : r == SK_TERM_KEY_NONE      ? ""
                                                   : sk_buf_cstr(out);
    bool ok = !strcmp(got, want) && (r == SK_TERM_KEY_SEND) == (out->len > 0) &&
              (r != SK_TERM_KEY_SEND || out->len == strlen(want));
    if (!ok) {
        char a[128], b[128];
        sk_test_fail(__FILE__, line, "%s: got \"%s\" (%d), want \"%s\"", where,
                     show(out->ptr ? out->ptr : "", out->len, a, sizeof a), (int)r, show(want, strlen(want), b, sizeof b));
    }
}

static void run(const key_case *cases, size_t n, bool app_keypad)
{
    for (size_t i = 0; i < n; i++) {
        const key_case *c = &cases[i];
        sk_key_event ev = {.key = c->key, .mods = c->mods, .base = c->base};
        snprintf(ev.text, sizeof ev.text, "%s", c->text);
        sk_term_input_modes m = {.app_cursor = c->app_cursor, .app_keypad = app_keypad};
        sk_buf out = {0};
        check_one(c->line, "linux", sk_term_key_as(false, &ev, &m, &out), &out, c->on_linux);
        sk_buf_clear(&out);
        check_one(c->line, "macos", sk_term_key_as(true, &ev, &m, &out), &out, c->on_mac);
        sk_buf_clear(&out);
#ifdef __APPLE__
        check_one(c->line, "build", sk_term_key(&ev, &m, &out), &out, c->on_mac);
#else
        check_one(c->line, "build", sk_term_key(&ev, &m, &out), &out, c->on_linux);
#endif
        sk_buf_free(&out);
    }
}

static void test_named_keys(void)
{
    run(named_keys, SK_LEN(named_keys), false);
}

static void test_text_keys(void)
{
    run(text_keys, SK_LEN(text_keys), false);
}

/* xterm.js reads DECKPAM but never asks it when it encodes a key. */
static void test_app_keypad_changes_nothing(void)
{
    run(text_keys, SK_LEN(text_keys), true);
    run(named_keys, SK_LEN(named_keys), true);
}

/* Bytes with a NUL in them. */
static void test_nul_bytes(void)
{
    static const struct {
        uint32_t base;
        const char *text;
        uint32_t mods;
        size_t nl, nm;
        const char *lin, *mac;
    } cases[] = {
        {' ', " ", C, 1, 1, "\0", "\0"},
        {' ', " ", C | CAPS, 1, 1, "\0", "\0"},
        {' ', " ", A | C, 2, 0, E "\0", ""},
        {' ', " ", S | A | C, 2, 0, E "\0", ""},
        {'2', "@", S | C, 1, 1, "\0", "\0"},
        {'2', "@", S | C | M, 1, 0, "\0", ""},
    };
    for (size_t i = 0; i < SK_LEN(cases); i++) {
        sk_key_event ev = {.mods = cases[i].mods, .base = cases[i].base};
        snprintf(ev.text, sizeof ev.text, "%s", cases[i].text);
        sk_term_input_modes m = {0};
        for (int mac = 0; mac < 2; mac++) {
            sk_buf out = {0};
            sk_term_key_result r = sk_term_key_as(mac, &ev, &m, &out);
            size_t n = mac ? cases[i].nm : cases[i].nl;
            const char *want = mac ? cases[i].mac : cases[i].lin;
            CHECK_INT(r, n ? SK_TERM_KEY_SEND : SK_TERM_KEY_NONE);
            CHECK_INT(out.len, n);
            CHECK(out.len != n || !n || memcmp(out.ptr, want, n) == 0);
            sk_buf_free(&out);
        }
    }
}

/* Every letter with Ctrl, Alt, Alt+Shift and Ctrl+Alt. */
static void test_every_letter(void)
{
    sk_term_input_modes m = {0};
    for (char l = 'a'; l <= 'z'; l++) {
        char up = (char)(l - 'a' + 'A');
        struct {
            uint32_t mods;
            const char *text;
            char lin[3], mac[3];
        } cases[] = {
            {0, (char[]){l, 0}, {l}, {l}},
            {S, (char[]){up, 0}, {up}, {up}},
            {C, (char[]){l, 0}, {(char)(l - 'a' + 1)}, {(char)(l - 'a' + 1)}},
            {A, "\xc3\xb8", {0x1b, l}, "\xc3\xb8"},
            {S | A, "\xc3\x98", {0x1b, up}, "\xc3\x98"},
            {A | C, (char[]){l, 0}, {0x1b, (char)(l - 'a' + 1)}, ""},
            {S | C, (char[]){up, 0}, "", ""},
            {M, (char[]){l, 0}, "", ""},
        };
        for (size_t i = 0; i < SK_LEN(cases); i++) {
            sk_key_event ev = {.mods = cases[i].mods, .base = (uint32_t)l};
            snprintf(ev.text, sizeof ev.text, "%s", cases[i].text);
            for (int mac = 0; mac < 2; mac++) {
                sk_buf out = {0};
                sk_term_key_as(mac, &ev, &m, &out);
                const char *want = mac ? cases[i].mac : cases[i].lin;
                if (out.len != strlen(want) || memcmp(sk_buf_cstr(&out), want, out.len))
                    sk_test_fail(__FILE__, __LINE__, "%c with mods %u on %s", l, (unsigned)cases[i].mods,
                                 mac ? "macos" : "linux");
                sk_buf_free(&out);
            }
        }
    }
}

/* xterm.js has no case for F13 to F24: nothing with any modifier. */
static void test_f13_to_f24_send_nothing(void)
{
    sk_term_input_modes m = {0};
    for (sk_key k = SK_KEY_F1 + 12; k <= SK_KEY_F24; k++) {
        for (uint32_t mods = 0; mods < 16; mods++) {
            sk_key_event ev = {.key = k, .mods = mods};
            for (int mac = 0; mac < 2; mac++) {
                sk_buf out = {0};
                CHECK_INT(sk_term_key_as(mac, &ev, &m, &out), SK_TERM_KEY_NONE);
                CHECK_INT(out.len, 0);
                sk_buf_free(&out);
            }
        }
    }
}

/* Every Cmd chord is the app's on macOS. */
static void test_cmd_chords_are_the_apps(void)
{
    sk_term_input_modes m = {0};
    for (sk_key k = SK_KEY_NONE; k <= SK_KEY_MENU; k++) {
        for (uint32_t mods = 0; mods < 16; mods++) {
            sk_key_event ev = {.key = k, .mods = mods | M, .base = 'a', .text = "a"};
            sk_buf out = {0};
            CHECK_INT(sk_term_key_as(true, &ev, &m, &out), SK_TERM_KEY_NONE);
            CHECK_INT(out.len, 0);
            sk_buf_free(&out);
        }
    }
}

/* Text that is not UTF-8 goes out as U+FFFD. */
static void test_text_is_utf8(void)
{
    sk_key_event ev = {.text = "a\xff"};
    sk_term_input_modes m = {0};
    sk_buf out = {0};
    CHECK_INT(sk_term_key_as(false, &ev, &m, &out), SK_TERM_KEY_SEND);
    CHECK_STR(sk_buf_cstr(&out), "a\xef\xbf\xbd");
    sk_buf_free(&out);
}

const sk_test sk_suite_keys[] = {
    T(test_named_keys),
    T(test_text_keys),
    T(test_app_keypad_changes_nothing),
    T(test_nul_bytes),
    T(test_every_letter),
    T(test_f13_to_f24_send_nothing),
    T(test_cmd_chords_are_the_apps),
    T(test_text_is_utf8),
    {0},
};
