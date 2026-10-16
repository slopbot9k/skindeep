/* A key goes through xterm.js as a browser delivers it: keydown runs
 * evaluateKeyboardEvent on the DOM keyCode and sends what it gives;
 * when it gives nothing, or the key is a third-level shift (Option on
 * macOS), keypress or the input event sends the typed character, unless
 * Alt, Ctrl or Meta is held without being that shift. key_code is the
 * keyCode Chromium gives a key; evaluate is evaluateKeyboardEvent.
 *
 * The mouse goes through bindMouse's listeners, MouseService's cell
 * arithmetic and CoreMouseService. Ported from xterm.js 6.0.0's
 * Keyboard.ts, CoreBrowserTerminal.ts, CoreMouseService.ts,
 * MouseService.ts and Clipboard.ts, (c) 2014-2019 The xterm.js authors
 * and (c) 2012-2013 Christopher Jeffrey, MIT licence. */
#include "keys.h"

#include "../base/utf8.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define ESC "\x1b"

/* ---- Keys ---- */

/* The keyCode of a key the US layout's way: Chromium's Windows virtual
 * key codes. */
static int key_code(const sk_key_event *ev)
{
    switch (ev->key) {
    case SK_KEY_BACKSPACE:
        return 8;
    case SK_KEY_TAB:
        return 9;
    case SK_KEY_ENTER:
    case SK_KEY_KP_ENTER:
        return 13;
    case SK_KEY_ESCAPE:
        return 27;
    case SK_KEY_PAGE_UP:
        return 33;
    case SK_KEY_PAGE_DOWN:
        return 34;
    case SK_KEY_END:
        return 35;
    case SK_KEY_HOME:
        return 36;
    case SK_KEY_LEFT:
        return 37;
    case SK_KEY_UP:
        return 38;
    case SK_KEY_RIGHT:
        return 39;
    case SK_KEY_DOWN:
        return 40;
    case SK_KEY_INSERT:
        return 45;
    case SK_KEY_DELETE:
        return 46;
    case SK_KEY_MENU:
        return 93;
    case SK_KEY_KP_MULTIPLY:
        return 106;
    case SK_KEY_KP_ADD:
        return 107;
    case SK_KEY_KP_SUBTRACT:
        return 109;
    case SK_KEY_KP_DECIMAL:
        return 110;
    case SK_KEY_KP_DIVIDE:
        return 111;
    case SK_KEY_KP_EQUAL:
        return 187; /* no key code of its own: OEM_PLUS, the = key's */
    case SK_KEY_NONE:
        break;
    default:
        if (ev->key >= SK_KEY_F1 && ev->key <= SK_KEY_F24)
            return 112 + (int)(ev->key - SK_KEY_F1);
        if (ev->key >= SK_KEY_KP_0 && ev->key <= SK_KEY_KP_9)
            return 96 + (int)(ev->key - SK_KEY_KP_0);
        return 0;
    }
    uint32_t c = ev->base;
    if (c >= 'a' && c <= 'z')
        return (int)(c - 'a' + 65);
    if (c >= 'A' && c <= 'Z')
        return (int)c;
    if ((c >= '0' && c <= '9') || c == ' ')
        return (int)c;
    switch (c) {
    case ';':
        return 186;
    case '=':
        return 187;
    case ',':
        return 188;
    case '-':
        return 189;
    case '.':
        return 190;
    case '/':
        return 191;
    case '`':
        return 192;
    case '[':
        return 219;
    case '\\':
        return 220;
    case ']':
        return 221;
    case '\'':
        return 222;
    default:
        return 0;
    }
}

/* KEYCODE_KEY_MAPPINGS: a digit or punctuation key's character without
 * and with Shift, on the US layout whatever the layout. */
static const char *key_mapping(int kc)
{
    if (kc >= 48 && kc <= 57)
        return &"0)1!2@3#4$5%6^7&8*9("[(kc - 48) * 2];
    switch (kc) {
    case 186:
        return ";:";
    case 187:
        return "=+";
    case 188:
        return ",<";
    case 189:
        return "-_";
    case 190:
        return ".>";
    case 191:
        return "/?";
    case 192:
        return "`~";
    case 219:
        return "[{";
    case 220:
        return "\\|";
    case 221:
        return "]}";
    case 222:
        return "'\"";
    default:
        return NULL;
    }
}

/* KeyboardEvent.key where the rules read it: a character key's text, or
 * NULL for a named key, whose key is a name of several letters. */
static const char *dom_key(const sk_key_event *ev)
{
    if (ev->key == SK_KEY_NONE || (ev->key >= SK_KEY_KP_0 && ev->key <= SK_KEY_KP_EQUAL &&
                                   ev->key != SK_KEY_KP_ENTER))
        return ev->text;
    return NULL;
}

/* key.length === 1: one UTF-16 unit. */
static bool one_unit(const char *s, size_t n)
{
    uint32_t cp;
    return n && sk_utf8_decode(s, n, &cp, NULL) == n && cp <= 0xFFFF;
}

typedef struct {
    char b[32];
    size_t n;
} key_bytes;

static void put(key_bytes *k, const char *s)
{
    size_t n = strlen(s);
    memcpy(k->b + k->n, s, n);
    k->n += n;
}

static void putc_(key_bytes *k, int c)
{
    k->b[k->n++] = (char)c;
}

/* CSI <n> ; <mods + 1> <final>, or without the modifier. */
static void csi(key_bytes *k, int n, int mods, char final)
{
    char s[32];
    if (mods)
        snprintf(s, sizeof s, ESC "[%d;%d%c", n, mods + 1, final);
    else
        snprintf(s, sizeof s, ESC "[%d%c", n, final);
    put(k, s);
}

/* The arrows, Home and End: CSI 1 ; m X with a modifier, else SS3 X in
 * application cursor mode, CSI X out of it. */
static void cursor(key_bytes *k, int mods, bool app, char final)
{
    if (mods) {
        csi(k, 1, mods, final);
    } else {
        putc_(k, 0x1b);
        putc_(k, app ? 'O' : '[');
        putc_(k, final);
    }
}

/* evaluateKeyboardEvent, with macOptionIsMeta false. */
static sk_term_key_result evaluate(bool mac, const sk_key_event *ev, int kc, const char *key, bool app,
                                   key_bytes *k)
{
    bool shift = ev->mods & SK_MOD_SHIFT, alt = ev->mods & SK_MOD_ALT;
    bool ctrl = ev->mods & SK_MOD_CTRL, meta = ev->mods & SK_MOD_SUPER;
    int mods = (shift ? 1 : 0) | (alt ? 2 : 0) | (ctrl ? 4 : 0) | (meta ? 8 : 0);
    static const int fkey_number[] = {15, 17, 18, 19, 20, 21, 23, 24};
    switch (kc) {
    case 8:
        if (alt)
            putc_(k, 0x1b);
        putc_(k, ctrl ? '\b' : 0x7f);
        break;
    case 9:
        put(k, shift ? ESC "[Z" : "\t");
        break;
    case 13:
        put(k, alt ? ESC "\r" : "\r");
        break;
    case 27:
        put(k, alt ? ESC ESC : ESC);
        break;
    case 37:
    case 38:
    case 39:
    case 40:
        if (!meta)
            cursor(k, mods, app, "DACB"[kc - 37]);
        break;
    case 45:
        if (!shift && !ctrl)
            put(k, ESC "[2~");
        break;
    case 46:
        csi(k, 3, mods, '~');
        break;
    case 36:
        cursor(k, mods, app, 'H');
        break;
    case 35:
        cursor(k, mods, app, 'F');
        break;
    case 33:
    case 34:
        if (shift)
            return kc == 33 ? SK_TERM_KEY_PAGE_UP : SK_TERM_KEY_PAGE_DOWN;
        csi(k, kc == 33 ? 5 : 6, ctrl ? mods : 0, '~');
        break;
    case 112:
    case 113:
    case 114:
    case 115:
        if (mods) {
            csi(k, 1, mods, "PQRS"[kc - 112]);
        } else {
            put(k, ESC "O");
            putc_(k, "PQRS"[kc - 112]);
        }
        break;
    case 116:
    case 117:
    case 118:
    case 119:
    case 120:
    case 121:
    case 122:
    case 123:
        csi(k, fkey_number[kc - 116], mods, '~');
        break;
    default:
        if (ctrl && !shift && !alt && !meta) {
            if (kc >= 65 && kc <= 90)
                putc_(k, kc - 64);
            else if (kc == 32)
                putc_(k, 0);
            else if (kc >= 51 && kc <= 55)
                putc_(k, kc - 51 + 27); /* ESC, FS, GS, RS, US */
            else if (kc == 56)
                putc_(k, 0x7f);
            else if (kc == 219)
                putc_(k, 0x1b);
            else if (kc == 220)
                putc_(k, 0x1c);
            else if (kc == 221)
                putc_(k, 0x1d);
        } else if (!mac && alt && !meta) {
            /* On macOS, Option is a third-level shift instead. A dead
             * key's branch is not needed: its keyCode here is its
             * letter's. */
            const char *map = key_mapping(kc);
            if (map) {
                putc_(k, 0x1b);
                putc_(k, map[shift ? 1 : 0]);
            } else if (kc >= 65 && kc <= 90) {
                putc_(k, 0x1b);
                putc_(k, ctrl ? kc - 64 : shift ? kc : kc + 32);
            } else if (kc == 32) {
                putc_(k, 0x1b);
                putc_(k, ctrl ? 0 : ' ');
            }
        } else if (key && !ctrl && !alt && !meta && kc >= 48 && one_unit(key, strlen(key))) {
            /* Cmd+A's branch on macOS comes before this: unreachable,
             * every Cmd chord is the app's. */
            put(k, key);
        } else if (key && ctrl) {
            if (!strcmp(key, "_"))
                putc_(k, 0x1f);
            if (!strcmp(key, "@"))
                putc_(k, 0);
        }
        break;
    }
    return SK_TERM_KEY_SEND;
}

sk_term_key_result sk_term_key_as(bool mac, const sk_key_event *ev, const sk_term_input_modes *m, sk_buf *out)
{
    bool alt = ev->mods & SK_MOD_ALT, ctrl = ev->mods & SK_MOD_CTRL, meta = ev->mods & SK_MOD_SUPER;
    if (mac && meta)
        return SK_TERM_KEY_NONE; /* Cmd chords are the app's */
    int kc = key_code(ev);
    key_bytes k = {.n = 0};
    sk_term_key_result r = evaluate(mac, ev, kc, dom_key(ev), m->app_cursor, &k);
    if (r != SK_TERM_KEY_SEND)
        return r;
    /* Option with a key past 47 is a third-level shift: keydown leaves
     * the key to keypress, so Option+F1 sends nothing. */
    bool third = mac && alt && !ctrl && !meta;
    if (k.n && !(third && (kc == 0 || kc > 47))) {
        sk_buf_append(out, k.b, k.n);
        return SK_TERM_KEY_SEND;
    }
    size_t n = strnlen(ev->text, sizeof ev->text);
    if (!n || ((alt || ctrl || meta) && !third))
        return SK_TERM_KEY_NONE;
    sk_utf8_sanitize(out, ev->text, n);
    return SK_TERM_KEY_SEND;
}

sk_term_key_result sk_term_key(const sk_key_event *ev, const sk_term_input_modes *m, sk_buf *out)
{
    return sk_term_key_as(SK_TERM_MAC, ev, m, out);
}

/* ---- Mouse ---- */

/* CoreMouseEventType: what each protocol listens to. */
enum { EV_DOWN = 1, EV_UP = 2, EV_DRAG = 4, EV_MOVE = 8, EV_WHEEL = 16 };

static unsigned protocol_events(sk_mouse_protocol p)
{
    switch (p) {
    case SK_MOUSE_X10:
        return EV_DOWN;
    case SK_MOUSE_VT200:
        return EV_DOWN | EV_UP | EV_WHEEL;
    case SK_MOUSE_DRAG:
        return EV_DOWN | EV_UP | EV_WHEEL | EV_DRAG;
    case SK_MOUSE_ANY:
        return EV_DOWN | EV_UP | EV_WHEEL | EV_DRAG | EV_MOVE;
    default:
        return 0;
    }
}

/* The protocol's restrict: whether it reports e, which X10 strips of
 * its modifiers. */
static bool restrict_to(sk_mouse_protocol p, sk_term_mouse_event *e)
{
    switch (p) {
    case SK_MOUSE_X10:
        if (e->button == SK_TERM_BUTTON_WHEEL || e->action != SK_TERM_MOUSE_DOWN)
            return false;
        e->ctrl = e->alt = e->shift = false;
        return true;
    case SK_MOUSE_VT200:
        return e->action != SK_TERM_MOUSE_MOVE;
    case SK_MOUSE_DRAG:
        return !(e->action == SK_TERM_MOUSE_MOVE && e->button == SK_TERM_BUTTON_NONE);
    case SK_MOUSE_ANY:
        return true;
    default:
        return false;
    }
}

static int event_code(const sk_term_mouse_event *e, bool sgr)
{
    int code = (e->ctrl ? 16 : 0) | (e->shift ? 4 : 0) | (e->alt ? 8 : 0);
    if (e->button == SK_TERM_BUTTON_WHEEL) {
        code |= 64 | (int)e->action;
    } else {
        code |= (int)e->button & 3;
        if (e->action == SK_TERM_MOUSE_MOVE)
            code |= 32;
        else if (e->action == SK_TERM_MOUSE_UP && !sgr)
            code |= SK_TERM_BUTTON_NONE; /* only SGR names the button released */
    }
    return code;
}

static void encode(sk_mouse_encoding enc, const sk_term_mouse_event *e, sk_buf *out)
{
    if (enc == SK_MOUSE_ENC_DEFAULT) {
        /* One byte each, value + 32: a cell past 223 is not reported,
         * as vte and konsole do it (xterm sends 0 instead). */
        int p[3] = {event_code(e, false) + 32, e->col + 32, e->row + 32};
        if (p[0] > 255 || p[1] > 255 || p[2] > 255)
            return;
        sk_buf_append_c(out, ESC "[M");
        for (int i = 0; i < 3; i++)
            sk_buf_putc(out, (char)p[i]);
        return;
    }
    char final = e->action == SK_TERM_MOUSE_UP && e->button != SK_TERM_BUTTON_WHEEL ? 'm' : 'M';
    if (enc == SK_MOUSE_ENC_SGR_PIXELS)
        sk_buf_printf(out, ESC "[<%d;%d;%d%c", event_code(e, true), e->x, e->y, final);
    else
        sk_buf_printf(out, ESC "[<%d;%d;%d%c", event_code(e, true), e->col, e->row, final);
}

static bool same_event(const sk_term_mouse_event *a, const sk_term_mouse_event *b, bool pixels)
{
    if (pixels ? a->x != b->x || a->y != b->y : a->col != b->col || a->row != b->row)
        return false;
    return a->button == b->button && a->action == b->action && a->ctrl == b->ctrl && a->alt == b->alt &&
           a->shift == b->shift;
}

void sk_term_mouse_init(sk_term_mouse *m)
{
    *m = (sk_term_mouse){.mac = SK_TERM_MAC};
}

void sk_term_mouse_reset(sk_term_mouse *m)
{
    m->has_last = false;
    m->wheel_partial = 0;
}

bool sk_term_mouse_report(sk_term_mouse *m, const sk_term_input_modes *modes, int cols, int rows,
                          sk_term_mouse_event e, sk_buf *out)
{
    if (e.col < 0 || e.col >= cols || e.row < 0 || e.row >= rows)
        return false;
    if (e.button == SK_TERM_BUTTON_WHEEL && e.action == SK_TERM_MOUSE_MOVE)
        return false;
    if (e.button == SK_TERM_BUTTON_NONE && e.action != SK_TERM_MOUSE_MOVE)
        return false;
    e.col++;
    e.row++;
    if (e.action == SK_TERM_MOUSE_MOVE && m->has_last &&
        same_event(&m->last, &e, modes->mouse_encoding == SK_MOUSE_ENC_SGR_PIXELS))
        return false;
    if (!restrict_to(modes->mouse, &e))
        return false;
    encode(modes->mouse_encoding, &e, out);
    m->last = e;
    m->has_last = true;
    return true;
}

/* sendEvent: the pointer clamped to the grid, its cell and point. */
static bool send(sk_term_mouse *m, const sk_term_input_modes *modes, const sk_term_grid *g, double x, double y,
                 sk_term_button button, sk_term_mouse_action action, uint32_t mods, sk_buf *out)
{
    if (g->cols <= 0 || g->rows <= 0 || !(g->cell_w > 0) || !(g->cell_h > 0))
        return false;
    double w = g->cols * g->cell_w, h = g->rows * g->cell_h;
    if (!(w < 1e9 && h < 1e9))
        return false;
    x = x > 0 ? x : 0; /* NaN too */
    y = y > 0 ? y : 0;
    x = x < w - 1 ? x : w - 1;
    y = y < h - 1 ? y : h - 1;
    sk_term_mouse_event e = {
        .col = (int)floor(x / g->cell_w),
        .row = (int)floor(y / g->cell_h),
        .x = (int)floor(x),
        .y = (int)floor(y),
        .button = button,
        .action = action,
        .ctrl = mods & SK_MOD_CTRL,
        .alt = mods & SK_MOD_ALT,
        .shift = mods & SK_MOD_SHIFT,
    };
    return sk_term_mouse_report(m, modes, g->cols, g->rows, e, out);
}

/* MouseEvent.buttons' bit for a button. */
static unsigned button_bit(int button)
{
    switch (button) {
    case 1:
        return 1;
    case 2:
        return 4;
    case 3:
        return 2;
    default:
        return button > 3 && button <= 8 ? 1u << (button - 1) : 0;
    }
}

/* MouseEvent.button < 3 names a button; any other is none. */
static sk_term_button dom_button(int button)
{
    return button >= 1 && button <= 3 ? (sk_term_button)(button - 1) : SK_TERM_BUTTON_NONE;
}

/* A protocol without up or drag events drops those listeners. */
static unsigned listening(sk_term_mouse *m, const sk_term_input_modes *modes)
{
    unsigned ev = protocol_events(modes->mouse);
    if (!(ev & EV_UP))
        m->up_listening = false;
    if (!(ev & EV_DRAG))
        m->drag_listening = false;
    return ev;
}

bool sk_term_mouse_down(sk_term_mouse *m, const sk_term_input_modes *modes, const sk_term_grid *g, double x,
                        double y, int button, uint32_t mods, sk_buf *out)
{
    m->buttons |= button_bit(button);
    unsigned ev = listening(m, modes);
    /* SelectionService.shouldForceSelection: Shift on Linux. On macOS
     * Option would, with macOptionClickForcesSelection, which is off. */
    if (!ev || (!m->mac && (mods & SK_MOD_SHIFT)))
        return false;
    send(m, modes, g, x, y, dom_button(button), SK_TERM_MOUSE_DOWN, mods, out);
    m->up_listening = ev & EV_UP;
    m->drag_listening = ev & EV_DRAG;
    return true;
}

bool sk_term_mouse_up(sk_term_mouse *m, const sk_term_input_modes *modes, const sk_term_grid *g, double x,
                      double y, int button, uint32_t mods, sk_buf *out)
{
    m->buttons &= ~button_bit(button);
    listening(m, modes);
    if (!m->up_listening)
        return false;
    send(m, modes, g, x, y, dom_button(button), SK_TERM_MOUSE_UP, mods, out);
    if (!m->buttons)
        m->up_listening = m->drag_listening = false;
    return true;
}

bool sk_term_mouse_move(sk_term_mouse *m, const sk_term_input_modes *modes, const sk_term_grid *g, double x,
                        double y, bool over, uint32_t mods, sk_buf *out)
{
    unsigned ev = listening(m, modes);
    if (m->buttons) {
        /* Only the document's drag listener takes a move with a button
         * held: the left one first, then the middle, then the right. */
        if (!m->drag_listening)
            return false;
        sk_term_button b = m->buttons & 1   ? SK_TERM_BUTTON_LEFT
                           : m->buttons & 4 ? SK_TERM_BUTTON_MIDDLE
                           : m->buttons & 2 ? SK_TERM_BUTTON_RIGHT
                                            : SK_TERM_BUTTON_NONE;
        send(m, modes, g, x, y, b, SK_TERM_MOUSE_MOVE, mods, out);
        return true;
    }
    if (!over || !(ev & EV_MOVE))
        return false;
    send(m, modes, g, x, y, SK_TERM_BUTTON_NONE, SK_TERM_MOUSE_MOVE, mods, out);
    return true;
}

/* Chromium's pixels for a line of a wheel that is not precise: 53 on
 * X11, 40 on macOS (kScrollbarPixelsPerCocoaTick). */
#define LINE_PX(mac) ((mac) ? 40.0 : 53.0)

/* consumeWheelEvent: the whole lines a wheel event adds up to, keeping
 * the fraction. Shift gives none. */
static double wheel_lines(sk_term_mouse *m, double delta, uint32_t mods, double cell_h)
{
    if (delta == 0 || (mods & SK_MOD_SHIFT) || !(cell_h > 0))
        return 0;
    double amount = delta * ((mods & (SK_MOD_ALT | SK_MOD_CTRL)) ? 5.0 : 1.0);
    amount /= cell_h;
    if (fabs(delta) < 50)
        amount *= 0.3; /* likely a trackpad */
    m->wheel_partial += amount;
    amount = floor(fabs(m->wheel_partial)) * (m->wheel_partial > 0 ? 1 : -1);
    m->wheel_partial = fmod(m->wheel_partial, 1);
    return amount;
}

bool sk_term_mouse_wheel(sk_term_mouse *m, const sk_term_input_modes *modes, const sk_term_grid *g, double x,
                         double y, double dy, bool precise, uint32_t mods, sk_buf *out)
{
    double delta = -(precise ? dy : dy * LINE_PX(m->mac)); /* WheelEvent.deltaY */
    if (protocol_events(modes->mouse) & EV_WHEEL) {
        if (wheel_lines(m, delta, mods, g->cell_h) != 0)
            send(m, modes, g, x, y, SK_TERM_BUTTON_WHEEL, delta < 0 ? SK_TERM_MOUSE_UP : SK_TERM_MOUSE_DOWN,
                 mods, out);
        return true;
    }
    /* A buffer without scrollback, the alternate one, gets arrows. */
    if (!modes->alt_screen || delta == 0)
        return false;
    if (wheel_lines(m, delta, mods, g->cell_h) != 0) {
        sk_buf_append_c(out, modes->app_cursor ? ESC "O" : ESC "[");
        sk_buf_putc(out, delta < 0 ? 'A' : 'B');
    }
    return true;
}

/* ---- Focus and paste ---- */

bool sk_term_focus(const sk_term_input_modes *m, bool focused, sk_buf *out)
{
    if (!m->focus_events)
        return false;
    sk_buf_append_c(out, focused ? ESC "[I" : ESC "[O");
    return true;
}

void sk_term_paste(const sk_term_input_modes *m, const char *text, size_t n, sk_buf *out)
{
    sk_buf clean = {0};
    sk_utf8_sanitize(&clean, text, n);
    if (m->bracketed_paste)
        sk_buf_append_c(out, ESC "[200~");
    for (size_t i = 0; i < clean.len; i++) {
        char c = clean.ptr[i];
        if (c == '\r' && i + 1 < clean.len && clean.ptr[i + 1] == '\n')
            continue; /* \r?\n becomes \r */
        sk_buf_putc(out, c == '\n' ? '\r' : c);
    }
    if (m->bracketed_paste)
        sk_buf_append_c(out, ESC "[201~");
    sk_buf_free(&clean);
}
