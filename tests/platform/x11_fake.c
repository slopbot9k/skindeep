#include "x11_fake.h"

#include "../../src/platform/x11/keys.h"
#include "../test.h"

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define BIG_OPCODE SK_XFAKE_BIG_OPCODE
#define SHM_OPCODE SK_XFAKE_SHM_OPCODE
#define MAX_KEYCODE 255
#define N_KEYCODES (MAX_KEYCODE - SK_XFAKE_MIN_KEYCODE + 1)
#define CLIENT_BASE 0x00200000u
#define CLIENT_MASK 0x001fffffu
#define INCR_ATOM_NAME "INCR"

typedef struct {
    uint8_t major, minor;
    bool big;
    uint8_t *bytes;
    size_t len;
} req;

typedef struct {
    char *name;
    uint32_t id;
} atom;

typedef struct {
    uint32_t id;
    int x, y; /* a window's, as made */
    int w, h;
    bool pixmap, mapped;
    uint32_t mask;
    uint32_t *px;
} drawable;

typedef struct {
    uint32_t win, atom, type;
    uint8_t format;
    sk_buf data;
} prop;

typedef struct {
    uint32_t atom, owner;
} sel;

typedef struct {
    uint32_t id;
    const uint8_t *addr;
} segment;

struct sk_xfake {
    sk_xfake_opts o;
    char *path, *display;
    int listen_fd, fd;
    atomic_int client; /* fd, for stopping */
    pthread_t thread;
    atomic_bool stop;
    pthread_mutex_t lock;  /* the state below */
    pthread_mutex_t wlock; /* writes to the client, and seq */
    uint16_t seq;
    uint32_t time;
    bool big;
    char *auth;
    SK_VEC(req) reqs;
    size_t big_count;
    SK_VEC(atom) atoms;
    SK_VEC(drawable) draws;
    SK_VEC(prop) props;
    SK_VEC(sel) sels;
    SK_VEC(segment) segs;
    /* The other client, as owner: of UTF8_STRING and STRING, or of
     * own_target alone when it is not 0. */
    char *own_text;
    size_t own_len, own_chunk;
    uint32_t own_target;
    bool incr_on;
    uint32_t incr_win, incr_prop, incr_type;
    size_t incr_off;
    /* The other client, as requestor. */
    bool ask_on, ask_incr, ask_done, ask_refused;
    uint32_t ask_prop, ask_type;
    sk_buf ask_data;
};

/* ---- The keyboard: evdev's keycodes, a US layout as an XKB server's
 * core map gives it, with AltGr's level on q and e ---- */

static uint32_t keysyms[N_KEYCODES * SK_XFAKE_PER];
static uint8_t modifiers[8 * SK_XFAKE_PER_MOD] = {
    50, 62, 0, 0,    /* Shift */
    66, 0, 0, 0,     /* Lock: Caps_Lock */
    37, 105, 0, 0,   /* Control */
    64, 108, 0, 0,   /* Mod1: Alt */
    77, 0, 0, 0,     /* Mod2: Num_Lock */
    0, 0, 0, 0,      /* Mod3 */
    133, 134, 0, 0,  /* Mod4: Super */
    92, 203, 0, 0,   /* Mod5: ISO_Level3_Shift, Mode_switch */
};

static void key(int kc, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f)
{
    uint32_t *row = keysyms + (kc - SK_XFAKE_MIN_KEYCODE) * SK_XFAKE_PER;
    uint32_t v[6] = {a, b, c, d, e, f};
    memcpy(row, v, sizeof v);
}

/* A key of two levels, its group repeated as XKB's core map has it. */
static void key2(int kc, uint32_t a, uint32_t b)
{
    key(kc, a, b, a, b, 0, 0);
}

static void build_keyboard(void)
{
    if (keysyms[0])
        return;
    static const char rows[][11] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
    static const int starts[] = {24, 38, 52};
    for (int r = 0; r < 3; r++)
        for (int i = 0; rows[r][i]; i++)
            key2(starts[r] + i, (uint32_t)rows[r][i], (uint32_t)rows[r][i] - 32);
    key(24, 'q', 'Q', 'q', 'Q', '@', 0x7d9 /* Greek_OMEGA */);
    key(26, 'e', 'E', 'e', 'E', 0x20ac /* EuroSign */, 0);
    static const char digits[] = "1!2@3#4$5%6^7&8*9(0)";
    for (int i = 0; i < 10; i++)
        key2(10 + i, (uint32_t)digits[2 * i], (uint32_t)digits[2 * i + 1]);
    key(9, 0xff1b, 0, 0xff1b, 0, 0, 0); /* Escape, as XKB lists it */
    key2(20, '-', '_');
    key2(21, '=', '+');
    key2(22, 0xff08, 0xff08);
    key2(23, 0xff09, SK_XK_ISO_LEFT_TAB);
    key2(34, '[', '{');
    key2(35, ']', '}');
    key2(36, 0xff0d, 0xff0d);
    key2(37, 0xffe3, 0xffe3); /* Control_L */
    key2(47, ';', ':');
    key2(48, '\'', '"');
    key2(49, '`', '~');
    key2(50, 0xffe1, 0xffe1); /* Shift_L */
    key2(51, '\\', '|');
    key2(59, ',', '<');
    key2(60, '.', '>');
    key2(61, '/', '?');
    key2(62, 0xffe2, 0xffe2); /* Shift_R */
    key2(63, 0xffaa, 0xffaa); /* KP_Multiply */
    key2(64, 0xffe9, 0xffe7); /* Alt_L Meta_L */
    key2(65, ' ', ' ');
    key2(66, SK_XK_CAPS_LOCK, SK_XK_CAPS_LOCK);
    for (int i = 0; i < 10; i++)
        key2(67 + i, 0xffbe + (uint32_t)i, 0xffbe + (uint32_t)i); /* F1 to F10 */
    key2(77, SK_XK_NUM_LOCK, SK_XK_NUM_LOCK);
    static const uint32_t pad[][3] = {
        {79, 0xff95, 0xffb7}, {80, 0xff97, 0xffb8}, {81, 0xff9a, 0xffb9}, {83, 0xff96, 0xffb4},
        {84, 0xff9d, 0xffb5}, {85, 0xff98, 0xffb6}, {87, 0xff9c, 0xffb1}, {88, 0xff99, 0xffb2},
        {89, 0xff9b, 0xffb3}, {90, 0xff9e, 0xffb0}, {91, 0xff9f, 0xffae},
    };
    for (size_t i = 0; i < SK_LEN(pad); i++)
        key2((int)pad[i][0], pad[i][1], pad[i][2]);
    key2(82, 0xffad, 0xffad); /* KP_Subtract */
    key2(86, 0xffab, 0xffab); /* KP_Add */
    key2(92, SK_XK_ISO_LEVEL3_SHIFT, 0);
    key2(95, 0xffc8, 0xffc8); /* F11 */
    key2(96, 0xffc9, 0xffc9); /* F12 */
    key2(104, 0xff8d, 0xff8d); /* KP_Enter */
    key2(105, 0xffe4, 0xffe4); /* Control_R */
    key2(106, 0xffaf, 0xffaf); /* KP_Divide */
    key2(108, 0xffea, 0xffe8); /* Alt_R Meta_R */
    static const uint32_t nav[] = {0xff50, 0xff52, 0xff55, 0xff51, 0xff53,
                                   0xff57, 0xff54, 0xff56, 0xff63, 0xffff};
    for (int i = 0; i < 10; i++)
        key2(110 + i, nav[i], nav[i]);
    key2(133, 0xffeb, 0xffeb); /* Super_L */
    key2(134, 0xffec, 0xffec); /* Super_R */
    key2(135, 0xff67, 0xff67); /* Menu */
    key(203, SK_XK_MODE_SWITCH, 0, 0, 0, 0, 0);
}

const uint32_t *sk_xfake_keysyms(int *count)
{
    build_keyboard();
    *count = N_KEYCODES;
    return keysyms;
}

const uint8_t *sk_xfake_modifiers(void)
{
    return modifiers;
}

/* ---- Bytes to the client ---- */

static void write_all(sk_xfake *f, const void *p, size_t n)
{
    const char *b = p;
    while (n && f->fd >= 0) {
        ssize_t w = write(f->fd, b, n);
        if (w < 0 && errno == EINTR)
            continue;
        if (w <= 0)
            return;
        b += w;
        n -= (size_t)w;
    }
}

static void flush_out(sk_xfake *f, sk_buf *out)
{
    if (out->len) {
        pthread_mutex_lock(&f->wlock);
        write_all(f, out->ptr, out->len);
        pthread_mutex_unlock(&f->wlock);
    }
    sk_buf_free(out);
}

static void ev_out(sk_xfake *f, sk_buf *out, const uint8_t ev[32])
{
    uint8_t e[32];
    memcpy(e, ev, 32);
    if ((e[0] & 0x7f) != SK_X11_KEYMAP_NOTIFY)
        sk_x11_set16(e + 2, f->seq);
    sk_buf_append(out, e, 32);
}

static void reply_out(sk_xfake *f, sk_buf *out, uint8_t h[32], const void *extra, size_t n)
{
    static const uint8_t zero[4];
    h[0] = 1;
    sk_x11_set16(h + 2, f->seq);
    sk_x11_set32(h + 4, (uint32_t)(sk_x11_pad4(n) / 4));
    sk_buf_append(out, h, 32);
    if (n)
        sk_buf_append(out, extra, n);
    sk_buf_append(out, zero, sk_x11_pad4(n) - n);
}

static void error_out(sk_xfake *f, sk_buf *out, uint8_t code, const uint8_t *r, uint32_t resource)
{
    uint8_t e[32] = {0, code};
    sk_x11_set16(e + 2, f->seq);
    sk_x11_set32(e + 4, resource);
    sk_x11_set16(e + 8, r[1]);
    e[10] = r[0];
    sk_buf_append(out, e, 32);
}

/* ---- The state ---- */

static uint32_t intern(sk_xfake *f, const char *name, size_t n, bool only_if_exists)
{
    static const char *const predefined[] = {"PRIMARY", "SECONDARY", "ARC", "ATOM", "BITMAP",
                                             "CARDINAL"};
    for (size_t i = 0; i < SK_LEN(predefined); i++)
        if (strlen(predefined[i]) == n && !memcmp(predefined[i], name, n))
            return (uint32_t)i + 1;
    if (n == 6 && !memcmp(name, "STRING", 6))
        return SK_X11_STRING;
    for (size_t i = 0; i < f->atoms.len; i++)
        if (strlen(f->atoms.ptr[i].name) == n && !memcmp(f->atoms.ptr[i].name, name, n))
            return f->atoms.ptr[i].id;
    if (only_if_exists)
        return 0;
    atom a = {sk_xstrndup(name, n), 69 + (uint32_t)f->atoms.len};
    sk_vec_push(&f->atoms, a);
    return a.id;
}

static drawable *find_draw(sk_xfake *f, uint32_t id)
{
    for (size_t i = 0; i < f->draws.len; i++)
        if (f->draws.ptr[i].id == id)
            return &f->draws.ptr[i];
    return NULL;
}

static drawable *add_draw(sk_xfake *f, uint32_t id, int w, int h, bool pixmap)
{
    drawable d = {id, 0, 0, w, h, pixmap, false, 0, NULL};
    if (w > 0 && h > 0)
        d.px = sk_xcalloc((size_t)w * (size_t)h, sizeof *d.px);
    sk_vec_push(&f->draws, d);
    return &f->draws.ptr[f->draws.len - 1];
}

static void drop_draw(sk_xfake *f, uint32_t id)
{
    for (size_t i = 0; i < f->draws.len; i++) {
        if (f->draws.ptr[i].id == id) {
            free(f->draws.ptr[i].px);
            f->draws.ptr[i] = f->draws.ptr[--f->draws.len];
            return;
        }
    }
}

static prop *find_prop(sk_xfake *f, uint32_t win, uint32_t a)
{
    for (size_t i = 0; i < f->props.len; i++)
        if (f->props.ptr[i].win == win && f->props.ptr[i].atom == a)
            return &f->props.ptr[i];
    return NULL;
}

static void prop_notify(sk_xfake *f, sk_buf *out, uint32_t win, uint32_t a, uint8_t state)
{
    drawable *d = find_draw(f, win);
    if (!d || !(d->mask & SK_X11_PROPERTY_CHANGE_MASK))
        return;
    uint8_t ev[32] = {SK_X11_PROPERTY_NOTIFY};
    sk_x11_set32(ev + 4, win);
    sk_x11_set32(ev + 8, a);
    sk_x11_set32(ev + 12, ++f->time);
    ev[16] = state;
    ev_out(f, out, ev);
}

static void set_prop(sk_xfake *f, sk_buf *out, uint8_t mode, uint32_t win, uint32_t a,
                     uint32_t type, uint8_t format, const void *data, size_t n);

static void after_delete(sk_xfake *f, sk_buf *out, uint32_t win, uint32_t a)
{
    /* The other client sends its next INCR chunk when the requestor
     * deleted the last. */
    if (f->incr_on && win == f->incr_win && a == f->incr_prop) {
        size_t n = SK_MIN(f->own_chunk, f->own_len - f->incr_off);
        set_prop(f, out, 0, win, a, f->incr_type, 8, f->own_text + f->incr_off, n);
        f->incr_off += n;
        if (n == 0)
            f->incr_on = false;
    }
}

static void delete_prop(sk_xfake *f, sk_buf *out, uint32_t win, uint32_t a)
{
    prop *p = find_prop(f, win, a);
    if (!p)
        return;
    sk_buf_free(&p->data);
    *p = f->props.ptr[--f->props.len];
    prop_notify(f, out, win, a, 1);
    after_delete(f, out, win, a);
}

/* What the other client does as requestor when its property changes. */
static void ask_property(sk_xfake *f, sk_buf *out, prop *p)
{
    if (!f->ask_on || !f->ask_incr || f->ask_done || p->win != SK_XFAKE_REQUESTOR ||
        p->atom != f->ask_prop)
        return;
    if (p->data.len == 0) {
        f->ask_done = true;
        return;
    }
    f->ask_type = p->type;
    sk_buf_append(&f->ask_data, p->data.ptr, p->data.len);
    delete_prop(f, out, SK_XFAKE_REQUESTOR, f->ask_prop);
}

static void set_prop(sk_xfake *f, sk_buf *out, uint8_t mode, uint32_t win, uint32_t a,
                     uint32_t type, uint8_t format, const void *data, size_t n)
{
    prop *p = find_prop(f, win, a);
    if (!p) {
        prop np = {win, a, type, format, {0}};
        sk_vec_push(&f->props, np);
        p = &f->props.ptr[f->props.len - 1];
        mode = 0;
    }
    if (mode == 0) {
        sk_buf_clear(&p->data);
        sk_buf_append(&p->data, data, n);
    } else if (mode == 1) {
        sk_buf b = {0};
        sk_buf_append(&b, data, n);
        sk_buf_append(&b, p->data.ptr, p->data.len);
        sk_buf_free(&p->data);
        p->data = b;
    } else {
        sk_buf_append(&p->data, data, n);
    }
    p->type = type;
    p->format = format;
    if (!p->data.ptr)
        sk_buf_reserve(&p->data, 0);
    prop_notify(f, out, win, a, 0);
    ask_property(f, out, p);
}

static uint32_t owner_of(sk_xfake *f, uint32_t a)
{
    for (size_t i = 0; i < f->sels.len; i++)
        if (f->sels.ptr[i].atom == a)
            return f->sels.ptr[i].owner;
    return 0;
}

static void set_owner(sk_xfake *f, uint32_t a, uint32_t owner)
{
    for (size_t i = 0; i < f->sels.len; i++) {
        if (f->sels.ptr[i].atom == a) {
            f->sels.ptr[i].owner = owner;
            return;
        }
    }
    sel s = {a, owner};
    sk_vec_push(&f->sels, s);
}

static bool client_id(uint32_t id)
{
    return (id & ~CLIENT_MASK) == CLIENT_BASE;
}

/* ---- Requests ---- */

static uint32_t value_of(const uint8_t *values, uint32_t mask, uint32_t bit, bool *found)
{
    uint32_t i = 0;
    for (uint32_t b = 1; b && b <= bit; b <<= 1) {
        if (!(mask & b))
            continue;
        if (b == bit) {
            *found = true;
            return sk_x11_get32(values + 4 * i);
        }
        i++;
    }
    *found = false;
    return 0;
}

static void map_window(sk_xfake *f, sk_buf *out, uint32_t id)
{
    drawable *d = find_draw(f, id);
    if (!d || d->pixmap)
        return;
    d->mapped = true;
    if (d->mask & SK_X11_STRUCTURE_NOTIFY_MASK) {
        uint8_t ev[32] = {SK_X11_MAP_NOTIFY};
        sk_x11_set32(ev + 4, id);
        sk_x11_set32(ev + 8, id);
        ev_out(f, out, ev);
    }
    if (d->mask & SK_X11_EXPOSURE_MASK) {
        uint8_t ev[32] = {SK_X11_EXPOSE};
        sk_x11_set32(ev + 4, id);
        sk_x11_set16(ev + 12, (uint32_t)d->w);
        sk_x11_set16(ev + 14, (uint32_t)d->h);
        ev_out(f, out, ev);
    }
}

static void put_image(sk_xfake *f, const uint8_t *r, size_t n)
{
    drawable *d = find_draw(f, sk_x11_get32(r + 4));
    int w = sk_x11_get16(r + 12), h = sk_x11_get16(r + 14);
    int x = (int16_t)sk_x11_get16(r + 16), y = (int16_t)sk_x11_get16(r + 18);
    if (!d || !d->px || r[1] != 2 || r[21] != 24 || n < 24 + (size_t)w * (size_t)h * 4)
        return;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            if (x + i >= 0 && x + i < d->w && y + j >= 0 && y + j < d->h)
                d->px[(size_t)(y + j) * (size_t)d->w + (size_t)(x + i)] =
                    sk_x11_get32(r + 24 + ((size_t)j * (size_t)w + (size_t)i) * 4);
}

static void get_image(sk_xfake *f, sk_buf *out, const uint8_t *r)
{
    uint32_t id = sk_x11_get32(r + 4);
    drawable *d = find_draw(f, id);
    int x = (int16_t)sk_x11_get16(r + 8), y = (int16_t)sk_x11_get16(r + 10);
    int w = sk_x11_get16(r + 12), h = sk_x11_get16(r + 14);
    if (!d || !d->px || (!d->pixmap && !d->mapped) || x < 0 || y < 0 || x + w > d->w ||
        y + h > d->h) {
        error_out(f, out, 8 /* BadMatch */, r, id);
        return;
    }
    uint8_t *data = sk_xmalloc((size_t)w * (size_t)h * 4 + 1);
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            sk_x11_set32(data + ((size_t)j * (size_t)w + (size_t)i) * 4,
                         d->px[(size_t)(y + j) * (size_t)d->w + (size_t)(x + i)]);
    uint8_t hd[32] = {0, 24};
    sk_x11_set32(hd + 8, d->pixmap ? 0 : SK_XFAKE_VISUAL);
    reply_out(f, out, hd, data, (size_t)w * (size_t)h * 4);
    free(data);
}

static void get_property(sk_xfake *f, sk_buf *out, const uint8_t *r)
{
    uint32_t win = sk_x11_get32(r + 4), a = sk_x11_get32(r + 8), type = sk_x11_get32(r + 12);
    size_t offset = (size_t)sk_x11_get32(r + 16) * 4, length = (size_t)sk_x11_get32(r + 20) * 4;
    prop *p = find_prop(f, win, a);
    uint8_t h[32] = {0};
    if (!p) {
        reply_out(f, out, h, NULL, 0);
        return;
    }
    h[1] = p->format;
    sk_x11_set32(h + 8, p->type);
    if (type && type != p->type) {
        sk_x11_set32(h + 12, (uint32_t)p->data.len);
        reply_out(f, out, h, NULL, 0);
        return;
    }
    size_t start = SK_MIN(offset, p->data.len), end = SK_MIN(p->data.len, start + length);
    size_t unit = p->format / 8 ? p->format / 8 : 1;
    sk_x11_set32(h + 12, (uint32_t)(p->data.len - end));
    sk_x11_set32(h + 16, (uint32_t)((end - start) / unit));
    reply_out(f, out, h, p->data.ptr + start, end - start);
    if (r[1] && end == p->data.len)
        delete_prop(f, out, win, a);
}

static void convert_selection(sk_xfake *f, sk_buf *out, const uint8_t *r)
{
    uint32_t requestor = sk_x11_get32(r + 4), selection = sk_x11_get32(r + 8);
    uint32_t target = sk_x11_get32(r + 12), property = sk_x11_get32(r + 16);
    uint32_t time = sk_x11_get32(r + 20), owner = owner_of(f, selection);
    if (owner == SK_XFAKE_OWNER && !f->own_text)
        return; /* an owner that never answers */
    uint8_t ev[32] = {0};
    sk_x11_set32(ev + 4, time);
    if (client_id(owner)) {
        ev[0] = SK_X11_SELECTION_REQUEST;
        sk_x11_set32(ev + 8, owner);
        sk_x11_set32(ev + 12, requestor);
        sk_x11_set32(ev + 16, selection);
        sk_x11_set32(ev + 20, target);
        sk_x11_set32(ev + 24, property);
        ev_out(f, out, ev);
        return;
    }
    uint32_t utf8 = intern(f, "UTF8_STRING", 11, false), targets = intern(f, "TARGETS", 7, false);
    bool offered = f->own_target ? target == f->own_target
                                 : target == utf8 || target == SK_X11_STRING;
    if (owner != SK_XFAKE_OWNER || !property) {
        property = 0;
    } else if (target == targets && !f->own_target) {
        uint8_t list[12];
        sk_x11_set32(list, targets);
        sk_x11_set32(list + 4, utf8);
        sk_x11_set32(list + 8, SK_X11_STRING);
        set_prop(f, out, 0, requestor, property, SK_X11_ATOM, 32, list, sizeof list);
    } else if (offered) {
        if (f->own_chunk && f->own_len > f->own_chunk) {
            uint8_t size[4];
            sk_x11_set32(size, (uint32_t)f->own_len);
            f->incr_on = true;
            f->incr_win = requestor;
            f->incr_prop = property;
            f->incr_type = target;
            f->incr_off = 0;
            set_prop(f, out, 0, requestor, property, intern(f, INCR_ATOM_NAME, 4, false), 32, size,
                     4);
        } else {
            set_prop(f, out, 0, requestor, property, target, 8, f->own_text, f->own_len);
        }
    } else {
        property = 0;
    }
    /* From the owner's SendEvent, or from the server when there is none. */
    ev[0] = SK_X11_SELECTION_NOTIFY | (owner ? 0x80 : 0);
    sk_x11_set32(ev + 8, requestor);
    sk_x11_set32(ev + 12, selection);
    sk_x11_set32(ev + 16, target);
    sk_x11_set32(ev + 20, property);
    ev_out(f, out, ev);
}

static void send_event(sk_xfake *f, sk_buf *out, const uint8_t *r)
{
    uint32_t dest = sk_x11_get32(r + 4);
    const uint8_t *ev = r + 12;
    if (dest == SK_XFAKE_REQUESTOR && (ev[0] & 0x7f) == SK_X11_SELECTION_NOTIFY && f->ask_on) {
        uint32_t property = sk_x11_get32(ev + 20);
        prop *p = property ? find_prop(f, SK_XFAKE_REQUESTOR, property) : NULL;
        if (!p) {
            f->ask_refused = f->ask_done = true;
        } else if (p->type == intern(f, INCR_ATOM_NAME, 4, false)) {
            f->ask_incr = true;
            delete_prop(f, out, SK_XFAKE_REQUESTOR, property);
        } else {
            f->ask_type = p->type;
            sk_buf_append(&f->ask_data, p->data.ptr, p->data.len);
            delete_prop(f, out, SK_XFAKE_REQUESTOR, property);
            f->ask_done = true;
        }
        return;
    }
    drawable *d = find_draw(f, dest);
    if (d && client_id(dest)) {
        uint8_t e[32];
        memcpy(e, ev, 32);
        e[0] |= 0x80;
        ev_out(f, out, e);
    }
}

static void keyboard_mapping(sk_xfake *f, sk_buf *out, const uint8_t *r)
{
    int first = r[4], count = r[5];
    int all;
    const uint32_t *syms = sk_xfake_keysyms(&all);
    uint8_t h[32] = {0, SK_XFAKE_PER};
    size_t n = (size_t)count * SK_XFAKE_PER;
    uint8_t *data = sk_xcalloc(n + 1, 4);
    for (size_t i = 0; i < n; i++) {
        size_t at = (size_t)(first - SK_XFAKE_MIN_KEYCODE) * SK_XFAKE_PER + i;
        sk_x11_set32(data + 4 * i, at < (size_t)all * SK_XFAKE_PER ? syms[at] : 0);
    }
    reply_out(f, out, h, data, n * 4);
    free(data);
}

static void query_extension(sk_xfake *f, sk_buf *out, const uint8_t *r, size_t len)
{
    size_t n = sk_x11_get16(r + 4);
    uint8_t h[32] = {0};
    if (8 + n <= len && n == 12 && !memcmp(r + 8, "BIG-REQUESTS", 12) && f->o.big_requests) {
        h[8] = 1;
        h[9] = BIG_OPCODE;
    }
    if (8 + n <= len && n == 7 && !memcmp(r + 8, "MIT-SHM", 7) && f->o.shm) {
        h[8] = 1;
        h[9] = SHM_OPCODE;
        h[10] = SK_XFAKE_SHM_EVENT;
    }
    reply_out(f, out, h, NULL, 0);
}

static segment *find_seg(sk_xfake *f, uint32_t id)
{
    for (size_t i = 0; i < f->segs.len; i++)
        if (f->segs.ptr[i].id == id)
            return &f->segs.ptr[i];
    return NULL;
}

/* MIT-SHM: the client's segments attached here, read on its puts. */
static void shm_request(sk_xfake *f, sk_buf *out, const uint8_t *r)
{
    uint8_t h[32] = {0};
    switch (r[1]) {
    case 0: /* ShmQueryVersion */
        sk_x11_set16(h + 8, 1);
        sk_x11_set16(h + 10, 2);
        reply_out(f, out, h, NULL, 0);
        break;
    case 1: { /* ShmAttach */
        int shmid = (int)sk_x11_get32(r + 8);
        void *addr = f->o.shm_refuse ? (void *)-1 : shmat(shmid, NULL, SHM_RDONLY);
        if (addr == (void *)-1) {
            error_out(f, out, 10 /* BadAccess */, r, sk_x11_get32(r + 8));
            break;
        }
        segment s = {sk_x11_get32(r + 4), addr};
        sk_vec_push(&f->segs, s);
        break;
    }
    case 2: /* ShmDetach */
        for (size_t i = 0; i < f->segs.len; i++) {
            if (f->segs.ptr[i].id == sk_x11_get32(r + 4)) {
                shmdt(f->segs.ptr[i].addr);
                f->segs.ptr[i] = f->segs.ptr[--f->segs.len];
                break;
            }
        }
        break;
    case 3: { /* ShmPutImage */
        uint32_t id = sk_x11_get32(r + 4), seg_id = sk_x11_get32(r + 32);
        drawable *d = find_draw(f, id);
        segment *s = find_seg(f, seg_id);
        int total_w = sk_x11_get16(r + 12), sx = sk_x11_get16(r + 16), sy = sk_x11_get16(r + 18);
        int w = sk_x11_get16(r + 20), hh = sk_x11_get16(r + 22);
        int dx = (int16_t)sk_x11_get16(r + 24), dy = (int16_t)sk_x11_get16(r + 26);
        if (!d || !s || r[28] != 24 || r[29] != 2) {
            error_out(f, out, 8 /* BadMatch */, r, id);
            break;
        }
        const uint8_t *base = s->addr + sk_x11_get32(r + 36);
        for (int j = 0; j < hh; j++)
            for (int i = 0; i < w; i++)
                if (d->px && dx + i >= 0 && dx + i < d->w && dy + j >= 0 && dy + j < d->h)
                    d->px[(size_t)(dy + j) * (size_t)d->w + (size_t)(dx + i)] = sk_x11_get32(
                        base + ((size_t)(sy + j) * (size_t)total_w + (size_t)(sx + i)) * 4);
        if (r[30]) {
            uint8_t ev[32] = {SK_XFAKE_SHM_EVENT};
            sk_x11_set32(ev + 4, id);
            sk_x11_set16(ev + 8, 3);
            ev[10] = SHM_OPCODE;
            sk_x11_set32(ev + 12, seg_id);
            sk_x11_set32(ev + 16, sk_x11_get32(r + 36));
            ev_out(f, out, ev);
        }
        break;
    }
    default:
        break;
    }
}

static void handle(sk_xfake *f, const uint8_t *r, size_t n, sk_buf *out)
{
    uint8_t h[32] = {0};
    switch (r[0]) {
    case 1: { /* CreateWindow */
        bool found;
        uint32_t mask = value_of(r + 32, sk_x11_get32(r + 28), SK_X11_CW_EVENT_MASK, &found);
        drawable *d = add_draw(f, sk_x11_get32(r + 4), sk_x11_get16(r + 16), sk_x11_get16(r + 18),
                               false);
        d->mask = mask;
        d->x = (int16_t)sk_x11_get16(r + 12);
        d->y = (int16_t)sk_x11_get16(r + 14);
        break;
    }
    case 2: { /* ChangeWindowAttributes */
        bool found;
        uint32_t id = sk_x11_get32(r + 4);
        uint32_t mask = value_of(r + 12, sk_x11_get32(r + 8), SK_X11_CW_EVENT_MASK, &found);
        drawable *d = find_draw(f, id);
        if (!d && found && (id == SK_XFAKE_OWNER || id == SK_XFAKE_REQUESTOR))
            d = add_draw(f, id, 0, 0, false);
        if (d && found)
            d->mask = mask;
        break;
    }
    case 4: /* DestroyWindow */
    case 54: /* FreePixmap */
        drop_draw(f, sk_x11_get32(r + 4));
        break;
    case 8: /* MapWindow */
        map_window(f, out, sk_x11_get32(r + 4));
        break;
    case 16: /* InternAtom */
        sk_x11_set32(h + 8, intern(f, (const char *)r + 8, sk_x11_get16(r + 4), r[1]));
        reply_out(f, out, h, NULL, 0);
        break;
    case 18: /* ChangeProperty */
        set_prop(f, out, r[1], sk_x11_get32(r + 4), sk_x11_get32(r + 8), sk_x11_get32(r + 12),
                 r[16], r + 24, (size_t)sk_x11_get32(r + 20) * (r[16] / 8));
        break;
    case 19: /* DeleteProperty */
        delete_prop(f, out, sk_x11_get32(r + 4), sk_x11_get32(r + 8));
        break;
    case 20:
        get_property(f, out, r);
        break;
    case 22: { /* SetSelectionOwner */
        uint32_t a = sk_x11_get32(r + 8), was = owner_of(f, a), owner = sk_x11_get32(r + 4);
        if (was && was != owner && client_id(was)) {
            uint8_t ev[32] = {SK_X11_SELECTION_CLEAR};
            sk_x11_set32(ev + 4, ++f->time);
            sk_x11_set32(ev + 8, was);
            sk_x11_set32(ev + 12, a);
            ev_out(f, out, ev);
        }
        set_owner(f, a, owner);
        break;
    }
    case 23: /* GetSelectionOwner */
        sk_x11_set32(h + 8, owner_of(f, sk_x11_get32(r + 4)));
        reply_out(f, out, h, NULL, 0);
        break;
    case 24:
        convert_selection(f, out, r);
        break;
    case 25:
        send_event(f, out, r);
        break;
    case 40: { /* TranslateCoordinates, to the root: where a window was made */
        const drawable *d = find_draw(f, sk_x11_get32(r + 4));
        h[1] = 1;
        sk_x11_set16(h + 12, (uint16_t)((d ? d->x : 0) + (int16_t)sk_x11_get16(r + 12)));
        sk_x11_set16(h + 14, (uint16_t)((d ? d->y : 0) + (int16_t)sk_x11_get16(r + 14)));
        reply_out(f, out, h, NULL, 0);
        break;
    }
    case 43: /* GetInputFocus */
        h[1] = 1;
        sk_x11_set32(h + 8, SK_XFAKE_ROOT);
        reply_out(f, out, h, NULL, 0);
        break;
    case 53: /* CreatePixmap */
        add_draw(f, sk_x11_get32(r + 4), sk_x11_get16(r + 12), sk_x11_get16(r + 14), true);
        break;
    case 72:
        put_image(f, r, n);
        break;
    case 73:
        get_image(f, out, r);
        break;
    case 98:
        query_extension(f, out, r, n);
        break;
    case 101:
        keyboard_mapping(f, out, r);
        break;
    case 119: /* GetModifierMapping */
        h[1] = SK_XFAKE_PER_MOD;
        reply_out(f, out, h, modifiers, sizeof modifiers);
        break;
    case SHM_OPCODE:
        shm_request(f, out, r);
        break;
    case BIG_OPCODE: /* BigReqEnable */
        f->big = true;
        sk_x11_set32(h + 8, 0x3fffff);
        reply_out(f, out, h, NULL, 0);
        break;
    default:
        break;
    }
}

/* ---- The thread ---- */

static bool read_full(sk_xfake *f, void *buf, size_t n)
{
    size_t got = 0;
    while (got < n) {
        if (atomic_load(&f->stop))
            return false;
        struct pollfd p = {f->fd, POLLIN, 0};
        int r = poll(&p, 1, 20);
        if (r <= 0)
            continue;
        ssize_t k = read(f->fd, (char *)buf + got, n - got);
        if (k < 0 && errno == EINTR)
            continue;
        if (k <= 0)
            return false;
        got += (size_t)k;
    }
    return true;
}

static void setup_reply(sk_xfake *f, sk_buf *out)
{
    static const char vendor[] = "skindeep's fake";
    uint8_t b[160] = {1, 0, 11, 0, 0, 0};
    sk_x11_set16(b + 6, (sizeof b - 8) / 4);
    sk_x11_set32(b + 8, 12101004);
    sk_x11_set32(b + 12, CLIENT_BASE);
    sk_x11_set32(b + 16, CLIENT_MASK);
    sk_x11_set32(b + 20, 256);
    sk_x11_set16(b + 24, sizeof vendor - 1);
    sk_x11_set16(b + 26, f->o.max_request ? f->o.max_request : 0xffff);
    b[28] = 1; /* screens */
    b[29] = 3; /* formats */
    b[30] = 0; /* LSBFirst */
    b[31] = 0;
    b[32] = 32;
    b[33] = 32;
    b[34] = SK_XFAKE_MIN_KEYCODE;
    b[35] = MAX_KEYCODE;
    memcpy(b + 40, vendor, sizeof vendor - 1);
    uint8_t *p = b + 56;
    static const uint8_t formats[3][3] = {{1, 1, 32}, {24, 32, 32}, {32, 32, 32}};
    for (int i = 0; i < 3; i++, p += 8)
        memcpy(p, formats[i], 3);
    sk_x11_set32(p, SK_XFAKE_ROOT);
    sk_x11_set32(p + 4, 0x20); /* colormap */
    sk_x11_set32(p + 8, 0xffffff);
    sk_x11_set32(p + 12, 0);
    sk_x11_set16(p + 20, 1920);
    sk_x11_set16(p + 22, 1080);
    sk_x11_set16(p + 24, 508);
    sk_x11_set16(p + 26, 286);
    sk_x11_set16(p + 28, 1);
    sk_x11_set16(p + 30, 1);
    sk_x11_set32(p + 32, SK_XFAKE_VISUAL);
    p[38] = 24;
    p[39] = 2; /* depths */
    p += 40;
    p[0] = 24;
    sk_x11_set16(p + 2, 1);
    p += 8;
    sk_x11_set32(p, SK_XFAKE_VISUAL);
    p[4] = 4; /* TrueColor */
    p[5] = 8;
    sk_x11_set16(p + 6, 256);
    sk_x11_set32(p + 8, 0xff0000);
    sk_x11_set32(p + 12, 0xff00);
    sk_x11_set32(p + 16, 0xff);
    p += 24;
    p[0] = 1; /* depth 1, no visuals */
    p += 8;
    sk_buf_append(out, b, (size_t)(p - b));
}

static bool setup(sk_xfake *f)
{
    uint8_t h[12];
    if (!read_full(f, h, sizeof h) || h[0] != 'l')
        return false;
    size_t nl = sk_x11_get16(h + 6), dl = sk_x11_get16(h + 8);
    size_t total = sk_x11_pad4(nl) + sk_x11_pad4(dl);
    uint8_t *b = sk_xmalloc(total + 1);
    if (!read_full(f, b, total)) {
        free(b);
        return false;
    }
    sk_buf auth = {0};
    sk_buf_append(&auth, b, nl);
    sk_buf_putc(&auth, ' ');
    for (size_t i = 0; i < dl; i++)
        sk_buf_printf(&auth, "%02x", b[sk_x11_pad4(nl) + i]);
    free(b);
    pthread_mutex_lock(&f->lock);
    f->auth = auth.ptr;
    pthread_mutex_unlock(&f->lock);
    sk_buf out = {0};
    if (f->o.refuse) {
        size_t n = strlen(f->o.refuse);
        uint8_t r[8] = {0, (uint8_t)n, 11, 0, 0, 0};
        sk_x11_set16(r + 6, (uint32_t)(sk_x11_pad4(n) / 4));
        sk_buf_append(&out, r, sizeof r);
        sk_buf_append(&out, f->o.refuse, n);
        sk_buf_append(&out, "\0\0\0", sk_x11_pad4(n) - n);
    } else {
        setup_reply(f, &out);
    }
    flush_out(f, &out);
    return !f->o.refuse;
}

static void *serve(void *arg)
{
    sk_xfake *f = arg;
    while (!atomic_load(&f->stop)) {
        struct pollfd p = {f->listen_fd, POLLIN, 0};
        if (poll(&p, 1, 20) > 0) {
            int fd = accept(f->listen_fd, NULL, NULL);
            if (fd >= 0) {
                pthread_mutex_lock(&f->wlock);
                f->fd = fd;
                pthread_mutex_unlock(&f->wlock);
                atomic_store(&f->client, fd);
                break;
            }
        }
    }
    if (f->fd < 0 || !setup(f))
        return NULL;
    for (;;) {
        uint8_t h[8];
        if (!read_full(f, h, 4))
            break;
        size_t words = sk_x11_get16(h + 2), skip = 0;
        bool big = false;
        if (words == 0 && f->big) {
            if (!read_full(f, h + 4, 4))
                break;
            words = sk_x11_get32(h + 4);
            big = true;
            skip = 4;
        }
        if (words * 4 < 4 + skip)
            break;
        size_t n = words * 4;
        uint8_t *raw = sk_xmalloc(n + 1);
        memcpy(raw, h, 4 + skip);
        if (!read_full(f, raw + 4 + skip, n - 4 - skip)) {
            free(raw);
            break;
        }
        /* The handlers see every request in the plain form. */
        uint8_t *r = sk_xmalloc(n + 1);
        memcpy(r, raw, 4);
        memcpy(r + 4, raw + 4 + skip, n - 4 - skip);
        pthread_mutex_lock(&f->wlock);
        f->seq++;
        pthread_mutex_unlock(&f->wlock);
        sk_buf out = {0};
        pthread_mutex_lock(&f->lock);
        req q = {raw[0], raw[1], big, raw, n};
        sk_vec_push(&f->reqs, q);
        f->big_count += big;
        handle(f, r, n - skip, &out);
        pthread_mutex_unlock(&f->lock);
        flush_out(f, &out);
        free(r);
    }
    return NULL;
}

/* ---- The test's side ---- */

sk_xfake *sk_xfake_start(const sk_xfake_opts *o)
{
    sk_xfake *f = sk_xcalloc(1, sizeof *f);
    if (o)
        f->o = *o;
    f->fd = -1;
    atomic_init(&f->client, -1);
    atomic_init(&f->stop, false);
    pthread_mutex_init(&f->lock, NULL);
    pthread_mutex_init(&f->wlock, NULL);
    sk_buf b = {0};
    sk_buf_printf(&b, "%s/X7", sk_test_tmpdir());
    f->path = b.ptr;
    b = (sk_buf){0};
    sk_buf_printf(&b, "%s:7", f->path);
    f->display = b.ptr;
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    if (strlen(f->path) >= sizeof sa.sun_path)
        sk_die("the fake X server's path is too long: %s", f->path);
    memcpy(sa.sun_path, f->path, strlen(f->path));
    f->listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (f->listen_fd < 0 || bind(f->listen_fd, (struct sockaddr *)&sa, sizeof sa) != 0 ||
        listen(f->listen_fd, 4) != 0)
        sk_die("the fake X server cannot listen at %s", f->path);
    b = (sk_buf){0};
    if (f->o.resources)
        set_prop(f, &b, 0, SK_XFAKE_ROOT, SK_X11_RESOURCE_MANAGER, SK_X11_STRING, 8,
                 f->o.resources, strlen(f->o.resources));
    sk_buf_free(&b);
    if (pthread_create(&f->thread, NULL, serve, f) != 0)
        sk_die("the fake X server cannot start its thread");
    return f;
}

void sk_xfake_stop(sk_xfake *f)
{
    if (!f)
        return;
    atomic_store(&f->stop, true);
    int fd = atomic_load(&f->client);
    if (fd >= 0)
        shutdown(fd, SHUT_RDWR); /* a write blocked on the client returns */
    pthread_join(f->thread, NULL);
    if (f->fd >= 0)
        close(f->fd);
    close(f->listen_fd);
    unlink(f->path);
    for (size_t i = 0; i < f->reqs.len; i++)
        free(f->reqs.ptr[i].bytes);
    sk_vec_free(&f->reqs);
    for (size_t i = 0; i < f->atoms.len; i++)
        free(f->atoms.ptr[i].name);
    sk_vec_free(&f->atoms);
    for (size_t i = 0; i < f->draws.len; i++)
        free(f->draws.ptr[i].px);
    sk_vec_free(&f->draws);
    for (size_t i = 0; i < f->props.len; i++)
        sk_buf_free(&f->props.ptr[i].data);
    sk_vec_free(&f->props);
    sk_vec_free(&f->sels);
    for (size_t i = 0; i < f->segs.len; i++)
        shmdt(f->segs.ptr[i].addr);
    sk_vec_free(&f->segs);
    sk_buf_free(&f->ask_data);
    free(f->own_text);
    free(f->auth);
    free(f->path);
    free(f->display);
    pthread_mutex_destroy(&f->lock);
    pthread_mutex_destroy(&f->wlock);
    free(f);
}

const char *sk_xfake_display(const sk_xfake *f)
{
    return f->display;
}

char *sk_xfake_auth(sk_xfake *f)
{
    pthread_mutex_lock(&f->lock);
    char *a = f->auth ? sk_xstrdup(f->auth) : NULL;
    pthread_mutex_unlock(&f->lock);
    return a;
}

static bool matches(const req *q, int major, int minor)
{
    return q->major == major && (minor < 0 || q->minor == minor);
}

size_t sk_xfake_count(sk_xfake *f, int major, int minor)
{
    size_t n = 0;
    pthread_mutex_lock(&f->lock);
    for (size_t i = 0; i < f->reqs.len; i++)
        n += matches(&f->reqs.ptr[i], major, minor);
    pthread_mutex_unlock(&f->lock);
    return n;
}

uint8_t *sk_xfake_request(sk_xfake *f, int major, int minor, size_t nth, size_t *len)
{
    uint8_t *copy = NULL;
    pthread_mutex_lock(&f->lock);
    for (size_t i = 0; i < f->reqs.len; i++) {
        const req *q = &f->reqs.ptr[i];
        if (matches(q, major, minor) && nth-- == 0) {
            copy = sk_xmalloc(q->len + 1);
            memcpy(copy, q->bytes, q->len);
            if (len)
                *len = q->len;
            break;
        }
    }
    pthread_mutex_unlock(&f->lock);
    return copy;
}

bool sk_xfake_wait(sk_xfake *f, int major, int minor, size_t count, int ms)
{
    for (int waited = 0; sk_xfake_count(f, major, minor) < count; waited += 5) {
        if (waited >= ms)
            return false;
        sk_test_sleep_ms(5);
    }
    return true;
}

size_t sk_xfake_big_count(sk_xfake *f)
{
    pthread_mutex_lock(&f->lock);
    size_t n = f->big_count;
    pthread_mutex_unlock(&f->lock);
    return n;
}

uint32_t sk_xfake_atom(sk_xfake *f, const char *name)
{
    pthread_mutex_lock(&f->lock);
    uint32_t a = intern(f, name, strlen(name), false);
    pthread_mutex_unlock(&f->lock);
    return a;
}

void sk_xfake_event(sk_xfake *f, const uint8_t ev[32])
{
    sk_buf out = {0};
    pthread_mutex_lock(&f->wlock);
    ev_out(f, &out, ev);
    write_all(f, out.ptr, out.len);
    pthread_mutex_unlock(&f->wlock);
    sk_buf_free(&out);
}

void sk_xfake_configure(sk_xfake *f, uint32_t win, int w, int h)
{
    pthread_mutex_lock(&f->lock);
    drawable *d = find_draw(f, win);
    uint32_t mask = 0;
    if (d) {
        free(d->px);
        d->w = w;
        d->h = h;
        d->px = sk_xcalloc((size_t)w * (size_t)h, sizeof *d->px);
        mask = d->mask;
    }
    pthread_mutex_unlock(&f->lock);
    if (!(mask & SK_X11_STRUCTURE_NOTIFY_MASK))
        return;
    uint8_t ev[32] = {SK_X11_CONFIGURE_NOTIFY};
    sk_x11_set32(ev + 4, win);
    sk_x11_set32(ev + 8, win);
    sk_x11_set16(ev + 20, (uint32_t)w);
    sk_x11_set16(ev + 22, (uint32_t)h);
    sk_xfake_event(f, ev);
}

uint32_t *sk_xfake_pixels(sk_xfake *f, uint32_t id, int *w, int *h)
{
    uint32_t *copy = NULL;
    pthread_mutex_lock(&f->lock);
    drawable *d = find_draw(f, id);
    if (d && d->px) {
        size_t n = (size_t)d->w * (size_t)d->h;
        copy = sk_xmalloc(n * sizeof *copy);
        memcpy(copy, d->px, n * sizeof *copy);
        *w = d->w;
        *h = d->h;
    }
    pthread_mutex_unlock(&f->lock);
    return copy;
}

uint8_t *sk_xfake_property(sk_xfake *f, uint32_t win, uint32_t a, size_t *len, uint32_t *type)
{
    uint8_t *copy = NULL;
    pthread_mutex_lock(&f->lock);
    prop *p = find_prop(f, win, a);
    if (p) {
        copy = sk_xmalloc(p->data.len + 1);
        memcpy(copy, p->data.ptr, p->data.len);
        *len = p->data.len;
        if (type)
            *type = p->type;
    }
    pthread_mutex_unlock(&f->lock);
    return copy;
}

void sk_xfake_own(sk_xfake *f, uint32_t selection, const char *text, size_t chunk)
{
    sk_xfake_own_target(f, selection, 0, text, chunk);
}

void sk_xfake_own_target(sk_xfake *f, uint32_t selection, uint32_t target, const char *text,
                         size_t chunk)
{
    sk_buf out = {0};
    pthread_mutex_lock(&f->lock);
    uint32_t was = owner_of(f, selection);
    if (was && client_id(was)) {
        uint8_t ev[32] = {SK_X11_SELECTION_CLEAR};
        sk_x11_set32(ev + 4, ++f->time);
        sk_x11_set32(ev + 8, was);
        sk_x11_set32(ev + 12, selection);
        pthread_mutex_lock(&f->wlock);
        ev_out(f, &out, ev);
        pthread_mutex_unlock(&f->wlock);
    }
    set_owner(f, selection, SK_XFAKE_OWNER);
    free(f->own_text);
    f->own_len = text ? strlen(text) : 0;
    f->own_text = text ? sk_xstrdup(text) : NULL;
    f->own_chunk = chunk;
    f->own_target = target;
    pthread_mutex_unlock(&f->lock);
    flush_out(f, &out);
}

void sk_xfake_set_property(sk_xfake *f, uint32_t win, uint32_t atom, uint32_t type,
                           uint8_t format, const void *data, size_t len)
{
    sk_buf out = {0};
    pthread_mutex_lock(&f->lock);
    pthread_mutex_lock(&f->wlock);
    set_prop(f, &out, 0, win, atom, type, format, data, len);
    pthread_mutex_unlock(&f->wlock);
    pthread_mutex_unlock(&f->lock);
    flush_out(f, &out);
}

void sk_xfake_ask(sk_xfake *f, uint32_t selection, uint32_t target)
{
    uint8_t ev[32] = {SK_X11_SELECTION_REQUEST};
    pthread_mutex_lock(&f->lock);
    f->ask_on = true;
    f->ask_incr = f->ask_done = f->ask_refused = false;
    sk_buf_clear(&f->ask_data);
    f->ask_prop = intern(f, "FAKE_ANSWER", 11, false);
    sk_x11_set32(ev + 4, ++f->time);
    sk_x11_set32(ev + 8, owner_of(f, selection));
    sk_x11_set32(ev + 12, SK_XFAKE_REQUESTOR);
    sk_x11_set32(ev + 16, selection);
    sk_x11_set32(ev + 20, target);
    sk_x11_set32(ev + 24, f->ask_prop);
    if (!find_draw(f, SK_XFAKE_REQUESTOR))
        add_draw(f, SK_XFAKE_REQUESTOR, 0, 0, false);
    pthread_mutex_unlock(&f->lock);
    sk_xfake_event(f, ev);
}

bool sk_xfake_answer(sk_xfake *f, char **data, size_t *len, uint32_t *type)
{
    pthread_mutex_lock(&f->lock);
    bool done = f->ask_done;
    if (done) {
        *data = NULL;
        *len = f->ask_data.len;
        if (!f->ask_refused) {
            *data = sk_xmalloc(f->ask_data.len + 1);
            memcpy(*data, f->ask_data.ptr ? f->ask_data.ptr : "", f->ask_data.len);
            (*data)[f->ask_data.len] = 0;
        }
        *type = f->ask_type;
    }
    pthread_mutex_unlock(&f->lock);
    return done;
}
