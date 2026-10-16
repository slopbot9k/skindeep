/* The connection: its setup, the bytes each way, sequence numbers, and
 * replies, events and errors sorted as they come. */
#include "x11.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#ifdef MSG_NOSIGNAL
#define SEND_FLAGS MSG_NOSIGNAL
#else
#define SEND_FLAGS 0
#endif

/* A reply past this is not one a client of ours asked for. */
#define MAX_PACKET ((size_t)1 << 30)

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static int lose(sk_x11 *c)
{
    c->dead = true;
    return -1;
}

/* Reads whatever the socket holds. 1 when bytes came, 0 when none were
 * there, -1 when the connection is lost. */
static int read_some(sk_x11 *c)
{
    if (c->dead)
        return -1;
    /* What was looked at goes; a packet cut short moves to the front. */
    if (c->in_off) {
        memmove(c->in.ptr, c->in.ptr + c->in_off, c->in.len - c->in_off);
        c->in.len -= c->in_off;
        c->in_off = 0;
    }
    int got = 0;
    for (;;) {
        sk_buf_reserve(&c->in, 65536);
        ssize_t n = read(c->fd, c->in.ptr + c->in.len, c->in.cap - c->in.len - 1);
        if (n > 0) {
            c->in.len += (size_t)n;
            c->in.ptr[c->in.len] = 0;
            got = 1;
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return got;
        return lose(c);
    }
}

/* Waits at most timeout_ms for bytes and reads them. */
static int fill(sk_x11 *c, int timeout_ms)
{
    if (c->dead)
        return -1;
    struct pollfd p = {c->fd, POLLIN, 0};
    int r;
    while ((r = poll(&p, 1, timeout_ms)) < 0 && errno == EINTR)
        ;
    if (r < 0)
        return lose(c);
    return r == 0 ? 0 : read_some(c);
}

bool sk_x11_flush(sk_x11 *c)
{
    size_t off = 0;
    while (!c->dead && off < c->out.len) {
        struct pollfd p = {c->fd, POLLIN | POLLOUT, 0};
        int r;
        while ((r = poll(&p, 1, SK_X11_TIMEOUT_MS)) < 0 && errno == EINTR)
            ;
        if (r <= 0) {
            lose(c);
            break;
        }
        /* The server may be waiting for us to read before it reads more. */
        if ((p.revents & (POLLIN | POLLHUP)) && read_some(c) < 0)
            break;
        if (p.revents & POLLERR) {
            lose(c);
            break;
        }
        if (p.revents & POLLOUT) {
            ssize_t n = send(c->fd, c->out.ptr + off, c->out.len - off, SEND_FLAGS);
            if (n > 0)
                off += (size_t)n;
            else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                lose(c);
        }
    }
    sk_buf_clear(&c->out);
    c->req_start = 0;
    return !c->dead;
}

/* ---- The setup ---- */

void sk_x11_setup_request(sk_buf *out, const sk_x11_cookie *k)
{
    static const uint8_t zero[4];
    uint8_t h[12] = {'l', 0, 11, 0, 0, 0};
    size_t nl = k ? k->name_len : 0, dl = k ? k->data_len : 0;
    sk_x11_set16(h + 6, (uint32_t)nl);
    sk_x11_set16(h + 8, (uint32_t)dl);
    sk_buf_append(out, h, sizeof h);
    if (nl) {
        sk_buf_append(out, k->name, nl);
        sk_buf_append(out, zero, sk_x11_pad4(nl) - nl);
    }
    if (dl) {
        sk_buf_append(out, k->data, dl);
        sk_buf_append(out, zero, sk_x11_pad4(dl) - dl);
    }
}

static bool setup_fail(char **err, const char *what)
{
    *err = sk_xstrdup(what);
    return false;
}

bool sk_x11_setup_parse(sk_x11 *c, const uint8_t *b, size_t n, int screen, char **err)
{
    if (n < 40 || b[0] != 1)
        return setup_fail(err, "the X server's setup is cut short");
    c->release = sk_x11_get32(b + 8);
    c->id_base = sk_x11_get32(b + 12);
    c->id_mask = sk_x11_get32(b + 16);
    size_t vlen = sk_x11_get16(b + 24);
    c->max_request = (uint32_t)sk_x11_get16(b + 26) * 4;
    unsigned n_screens = b[28], n_formats = b[29];
    c->image_lsb = b[30] == 0;
    c->min_keycode = b[34];
    c->max_keycode = b[35];
    size_t off = 40;
    if (n - off < sk_x11_pad4(vlen))
        return setup_fail(err, "the X server's setup is cut short");
    size_t keep = SK_MIN(vlen, sizeof c->vendor - 1);
    memcpy(c->vendor, b + off, keep);
    c->vendor[keep] = 0;
    off += sk_x11_pad4(vlen);
    if (n - off < 8 * (size_t)n_formats)
        return setup_fail(err, "the X server's setup is cut short");
    for (unsigned i = 0; i < n_formats; i++, off += 8)
        if (b[off] == 24)
            c->bpp24 = b[off + 1];
    if (screen < 0 || (unsigned)screen >= n_screens)
        return setup_fail(err, "the display has no such screen");
    for (unsigned s = 0; s <= (unsigned)screen; s++) {
        if (n - off < 40)
            return setup_fail(err, "the X server's setup is cut short");
        const uint8_t *p = b + off;
        bool mine = s == (unsigned)screen;
        if (mine) {
            c->root = sk_x11_get32(p);
            c->colormap = sk_x11_get32(p + 4);
            c->white = sk_x11_get32(p + 8);
            c->black = sk_x11_get32(p + 12);
            c->root_w = sk_x11_get16(p + 20);
            c->root_h = sk_x11_get16(p + 22);
            c->root_mm_w = sk_x11_get16(p + 24);
            c->root_mm_h = sk_x11_get16(p + 26);
            c->root_visual = sk_x11_get32(p + 32);
            c->root_depth = p[38];
        }
        unsigned n_depths = p[39];
        off += 40;
        for (unsigned d = 0; d < n_depths; d++) {
            if (n - off < 8)
                return setup_fail(err, "the X server's setup is cut short");
            unsigned depth = b[off], n_visuals = sk_x11_get16(b + off + 2);
            off += 8;
            if (n - off < 24 * (size_t)n_visuals)
                return setup_fail(err, "the X server's setup is cut short");
            for (unsigned v = 0; v < n_visuals; v++, off += 24) {
                const uint8_t *q = b + off;
                uint32_t id = sk_x11_get32(q);
                bool fits = depth == 24 && q[4] == 4 /* TrueColor */ &&
                            sk_x11_get32(q + 8) == 0xff0000 && sk_x11_get32(q + 12) == 0xff00 &&
                            sk_x11_get32(q + 16) == 0xff;
                if (mine && fits && (!c->visual || id == c->root_visual))
                    c->visual = id;
            }
        }
    }
    if (!c->id_mask)
        return setup_fail(err, "the X server gave no resource ids");
    if (!c->visual)
        return setup_fail(err, "the display has no 24-bit TrueColor visual");
    if (c->bpp24 != 32)
        return setup_fail(err, "the display does not keep 24-bit pixels in 32 bits");
    return true;
}

static void set_nonblocking(int fd)
{
    int fl = fcntl(fd, F_GETFL);
    if (fl >= 0)
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

sk_x11 *sk_x11_open_fd(int fd, const sk_x11_cookie *cookie, int screen, char **err)
{
    sk_x11 *c = sk_xcalloc(1, sizeof *c);
    c->fd = fd;
    set_nonblocking(fd);
    sk_x11_setup_request(&c->out, cookie);
    uint64_t deadline = now_ms() + SK_X11_TIMEOUT_MS;
    size_t total = 8;
    bool ok = sk_x11_flush(c);
    while (ok && c->in.len < total) {
        uint64_t now = now_ms();
        ok = now < deadline && fill(c, (int)(deadline - now)) >= 0;
        if (c->in.len >= 8)
            total = 8 + 4 * (size_t)sk_x11_get16((const uint8_t *)c->in.ptr + 6);
    }
    if (!ok) {
        *err = sk_xstrdup("the X server closed the connection during the setup");
        sk_x11_free(c);
        return NULL;
    }
    const uint8_t *b = (const uint8_t *)c->in.ptr;
    if (b[0] != 1) {
        size_t len = b[0] == 0 ? b[1] : total - 8;
        sk_buf m = {0};
        sk_buf_printf(&m, "the X server refused the connection: %.*s", (int)SK_MIN(len, total - 8),
                      (const char *)b + 8);
        while (m.len && (m.ptr[m.len - 1] == '\n' || m.ptr[m.len - 1] == 0))
            m.ptr[--m.len] = 0;
        *err = m.ptr;
        sk_x11_free(c);
        return NULL;
    }
    if (!sk_x11_setup_parse(c, b, total, screen, err)) {
        sk_x11_free(c);
        return NULL;
    }
    c->in_off = total;
    return c;
}

sk_x11 *sk_x11_open(const char *display, char **err)
{
    sk_x11_display d;
    if (!sk_x11_display_parse(display, &d, err))
        return NULL;
    int fd = sk_x11_display_connect(&d, err);
    if (fd < 0) {
        sk_x11_display_free(&d);
        return NULL;
    }
    sk_x11_cookie k;
    bool have = sk_x11_xauth_load(d.number, &k);
    sk_x11 *c = sk_x11_open_fd(fd, have ? &k : NULL, d.screen, err);
    sk_x11_display_free(&d);
    return c;
}

void sk_x11_free(sk_x11 *c)
{
    if (!c)
        return;
    close(c->fd);
    sk_buf_free(&c->out);
    sk_buf_free(&c->in);
    sk_vec_free(&c->events);
    free(c);
}

uint32_t sk_x11_id(sk_x11 *c)
{
    uint32_t inc = c->id_mask & (~c->id_mask + 1);
    c->id_next += inc;
    if (c->id_next & ~c->id_mask)
        sk_die("the X server's resource ids ran out");
    return c->id_base | c->id_next;
}

/* ---- Requests ---- */

uint64_t sk_x11_begin(sk_x11 *c, uint8_t opcode, uint8_t data)
{
    /* Sequence numbers come back in 16 bits: keep the server within reach. */
    if (c->seq - c->seen >= 0xf000 && !c->syncing && !c->dead) {
        c->syncing = true;
        sk_x11_sync(c);
        c->syncing = false;
    }
    c->req_start = c->out.len;
    uint8_t h[4] = {opcode, data, 0, 0};
    sk_buf_append(&c->out, h, sizeof h);
    return ++c->seq;
}

void sk_x11_put8(sk_x11 *c, uint32_t v)
{
    uint8_t b = (uint8_t)v;
    sk_buf_append(&c->out, &b, 1);
}

void sk_x11_put16(sk_x11 *c, uint32_t v)
{
    uint8_t b[2];
    sk_x11_set16(b, v);
    sk_buf_append(&c->out, b, 2);
}

void sk_x11_put32(sk_x11 *c, uint32_t v)
{
    uint8_t b[4];
    sk_x11_set32(b, v);
    sk_buf_append(&c->out, b, 4);
}

void sk_x11_put(sk_x11 *c, const void *p, size_t n)
{
    sk_buf_append(&c->out, p, n);
}

void sk_x11_end(sk_x11 *c)
{
    static const uint8_t zero[4];
    size_t n = c->out.len - c->req_start;
    sk_buf_append(&c->out, zero, sk_x11_pad4(n) - n);
    n = sk_x11_pad4(n);
    size_t words = n / 4;
    if (words <= 0xffff && n <= c->max_request) {
        sk_x11_set16((uint8_t *)c->out.ptr + c->req_start + 2, (uint32_t)words);
        return;
    }
    if (!c->big_opcode || n + 4 > c->max_request)
        sk_die("an X request of %zu bytes is past the server's maximum of %u", n,
               (unsigned)c->max_request);
    /* BIG-REQUESTS: a length of 0, then the length in 32 bits after the
     * header, counting itself. */
    sk_buf_reserve(&c->out, 4);
    uint8_t *h = (uint8_t *)c->out.ptr + c->req_start;
    memmove(h + 8, h + 4, n - 4);
    sk_x11_set16(h + 2, 0);
    sk_x11_set32(h + 4, (uint32_t)words + 1);
    c->out.len += 4;
    c->out.ptr[c->out.len] = 0;
}

/* ---- What comes back ---- */

static uint64_t full_seq(const sk_x11 *c, uint16_t s16)
{
    uint64_t s = (c->seq & ~(uint64_t)0xffff) | s16;
    if (s > c->seq && s >= 0x10000)
        s -= 0x10000;
    return s;
}

/* Takes the packet at the head of the unread bytes, when it is all
 * there: an event to the queue, an error to whoever waits for it, a reply
 * to *r when it answers want. *found when it was want's reply or error. */
static bool take(sk_x11 *c, uint64_t want, sk_x11_reply *r, uint8_t *error, bool *found)
{
    size_t avail = c->in.len - c->in_off;
    if (avail < 32)
        return false;
    const uint8_t *p = (const uint8_t *)c->in.ptr + c->in_off;
    uint8_t type = p[0];
    size_t n = 32;
    if (type == 1 || (type & 0x7f) == SK_X11_GENERIC_EVENT) {
        size_t extra = 4 * (size_t)sk_x11_get32(p + 4);
        if (extra > MAX_PACKET) {
            lose(c);
            return false;
        }
        n += extra;
    }
    if (avail < n)
        return false;
    uint64_t seq = c->seen;
    if ((type & 0x7f) != SK_X11_KEYMAP_NOTIFY)
        c->seen = seq = full_seq(c, sk_x11_get16(p + 2));
    if (type == 0) {
        if (want && seq == want) {
            *error = p[1];
            *found = true;
        } else if (c->check_seq && seq == c->check_seq) {
            c->check_error = p[1];
        } else if (c->on_error) {
            c->on_error(c->error_ud, p, seq);
        }
    } else if (type == 1) {
        if (want && seq == want) {
            r->data = sk_xmalloc(n);
            memcpy(r->data, p, n);
            r->len = n;
            *found = true;
        }
    } else if ((type & 0x7f) != SK_X11_GENERIC_EVENT) {
        sk_x11_raw ev;
        memcpy(ev.b, p, 32);
        sk_vec_push(&c->events, ev);
    }
    c->in_off += n;
    return true;
}

static void take_all(sk_x11 *c)
{
    bool found = false;
    while (take(c, 0, NULL, NULL, &found))
        ;
}

int sk_x11_read(sk_x11 *c, int timeout_ms)
{
    int r = fill(c, timeout_ms);
    take_all(c);
    return c->dead ? -1 : r;
}

bool sk_x11_pending(sk_x11 *c)
{
    take_all(c);
    return c->ev_head < c->events.len;
}

bool sk_x11_next_event(sk_x11 *c, uint8_t ev[32])
{
    if (c->ev_head >= c->events.len)
        return false;
    memcpy(ev, c->events.ptr[c->ev_head++].b, 32);
    if (c->ev_head == c->events.len)
        c->ev_head = c->events.len = 0;
    return true;
}

const uint8_t *sk_x11_peek_event(const sk_x11 *c, size_t i)
{
    return c->ev_head + i < c->events.len ? c->events.ptr[c->ev_head + i].b : NULL;
}

bool sk_x11_wait_event(sk_x11 *c, bool (*match)(const uint8_t ev[32], void *ud), void *ud,
                       uint8_t ev[32], int timeout_ms)
{
    if (!sk_x11_flush(c))
        return false;
    uint64_t deadline = now_ms() + (uint64_t)(timeout_ms < 0 ? 0 : timeout_ms);
    size_t looked = c->ev_head;
    for (;;) {
        take_all(c);
        for (size_t i = looked; i < c->events.len; i++) {
            if (!match(c->events.ptr[i].b, ud))
                continue;
            memcpy(ev, c->events.ptr[i].b, 32);
            memmove(c->events.ptr + i, c->events.ptr + i + 1,
                    (c->events.len - i - 1) * sizeof *c->events.ptr);
            c->events.len--;
            if (c->ev_head == c->events.len)
                c->ev_head = c->events.len = 0;
            return true;
        }
        looked = c->events.len;
        uint64_t now = now_ms();
        if (c->dead || now >= deadline || fill(c, (int)(deadline - now)) < 0)
            return false;
    }
}

bool sk_x11_reply_wait(sk_x11 *c, uint64_t seq, sk_x11_reply *r, uint8_t *error)
{
    memset(r, 0, sizeof *r);
    *error = 0;
    if (seq > c->seq || !sk_x11_flush(c))
        return false;
    uint64_t deadline = now_ms() + SK_X11_TIMEOUT_MS;
    for (;;) {
        bool found = false;
        while (!found && take(c, seq, r, error, &found))
            ;
        if (found)
            return r->data != NULL;
        /* The server is past it: its answer went by unclaimed. */
        if (c->dead || c->seen > seq)
            return false;
        uint64_t now = now_ms();
        if (now >= deadline) {
            lose(c);
            return false;
        }
        if (fill(c, (int)(deadline - now)) < 0)
            return false;
    }
}

void sk_x11_reply_free(sk_x11_reply *r)
{
    free(r->data);
    r->data = NULL;
    r->len = 0;
}

bool sk_x11_sync(sk_x11 *c)
{
    sk_x11_reply r;
    uint8_t e;
    bool ok = sk_x11_reply_wait(c, sk_x11_get_input_focus(c), &r, &e);
    sk_x11_reply_free(&r);
    return ok;
}

int sk_x11_check(sk_x11 *c, uint64_t seq)
{
    c->check_seq = seq;
    c->check_error = 0;
    bool ok = sk_x11_sync(c);
    c->check_seq = 0;
    return ok ? c->check_error : -1;
}

bool sk_x11_intern(sk_x11 *c, const char *const *names, size_t n, uint32_t *atoms)
{
    uint64_t *seqs = sk_xcalloc(n ? n : 1, sizeof *seqs);
    for (size_t i = 0; i < n; i++)
        seqs[i] = sk_x11_intern_atom(c, names[i], false);
    bool ok = true;
    for (size_t i = 0; i < n; i++) {
        sk_x11_reply r;
        uint8_t e;
        bool got = sk_x11_reply_wait(c, seqs[i], &r, &e) && r.len >= 12;
        atoms[i] = got ? sk_x11_get32(r.data + 8) : 0;
        ok = ok && got;
        sk_x11_reply_free(&r);
    }
    free(seqs);
    return ok && !c->dead;
}

void sk_x11_extensions(sk_x11 *c)
{
    uint64_t big = sk_x11_query_extension(c, "BIG-REQUESTS");
    uint64_t shm = sk_x11_query_extension(c, "MIT-SHM");
    sk_x11_reply r;
    uint8_t e;
    uint8_t big_op = 0, shm_op = 0, shm_ev = 0;
    if (sk_x11_reply_wait(c, big, &r, &e) && r.len >= 12 && r.data[8])
        big_op = r.data[9];
    sk_x11_reply_free(&r);
    if (sk_x11_reply_wait(c, shm, &r, &e) && r.len >= 12 && r.data[8]) {
        shm_op = r.data[9];
        shm_ev = r.data[10];
    }
    sk_x11_reply_free(&r);
    if (big_op) {
        c->big_opcode = big_op;
        if (sk_x11_reply_wait(c, sk_x11_big_enable(c), &r, &e) && r.len >= 12) {
            uint64_t max = (uint64_t)sk_x11_get32(r.data + 8) * 4;
            if (max > c->max_request)
                c->max_request = (uint32_t)SK_MIN(max, (uint64_t)1 << 30);
        } else {
            c->big_opcode = 0;
        }
        sk_x11_reply_free(&r);
    }
    if (shm_op) {
        c->shm_opcode = shm_op;
        if (sk_x11_reply_wait(c, sk_x11_shm_query_version(c), &r, &e) && r.len >= 12 &&
            (sk_x11_get16(r.data + 8) > 1 || sk_x11_get16(r.data + 10) >= 1))
            c->shm_event = shm_ev;
        else
            c->shm_opcode = 0;
        sk_x11_reply_free(&r);
    }
}

bool sk_x11_prop_parse(const sk_x11_reply *r, sk_x11_prop *p)
{
    memset(p, 0, sizeof *p);
    if (!r->data || r->len < 32)
        return false;
    p->format = r->data[1];
    p->type = sk_x11_get32(r->data + 8);
    p->bytes_after = sk_x11_get32(r->data + 12);
    p->n = sk_x11_get32(r->data + 16);
    if (p->format != 0 && p->format != 8 && p->format != 16 && p->format != 32)
        return false;
    size_t len = (size_t)p->n * (p->format / 8);
    if (len > r->len - 32)
        return false;
    p->value = r->data + 32;
    p->len = len;
    return true;
}

bool sk_x11_prop_get(sk_x11 *c, bool del, uint32_t win, uint32_t prop, uint32_t type, size_t max,
                     sk_x11_reply *r, sk_x11_prop *p)
{
    uint8_t e;
    uint32_t words = (uint32_t)SK_MIN((max + 3) / 4, (size_t)0x3fffffff);
    if (!sk_x11_reply_wait(c, sk_x11_get_property(c, del, win, prop, type, 0, words), r, &e)) {
        memset(p, 0, sizeof *p);
        return false;
    }
    return sk_x11_prop_parse(r, p);
}

/* ---- Events ---- */

bool sk_x11_event_decode(const uint8_t *b, uint8_t shm_event, sk_x11_event *ev)
{
    memset(ev, 0, sizeof *ev);
    ev->type = b[0] & 0x7f;
    ev->sent = (b[0] & 0x80) != 0;
    ev->detail = b[1];
    ev->seq = sk_x11_get16(b + 2);
    switch (ev->type) {
    case SK_X11_KEY_PRESS:
    case SK_X11_KEY_RELEASE:
    case SK_X11_BUTTON_PRESS:
    case SK_X11_BUTTON_RELEASE:
    case SK_X11_MOTION_NOTIFY:
    case SK_X11_ENTER_NOTIFY:
    case SK_X11_LEAVE_NOTIFY:
        ev->time = sk_x11_get32(b + 4);
        ev->window = sk_x11_get32(b + 12);
        ev->x = (int16_t)sk_x11_get16(b + 24);
        ev->y = (int16_t)sk_x11_get16(b + 26);
        ev->state = sk_x11_get16(b + 28);
        ev->mode = b[30];
        return true;
    case SK_X11_FOCUS_IN:
    case SK_X11_FOCUS_OUT:
        ev->window = sk_x11_get32(b + 4);
        ev->mode = b[8];
        return true;
    case SK_X11_EXPOSE:
        ev->window = sk_x11_get32(b + 4);
        ev->x = sk_x11_get16(b + 8);
        ev->y = sk_x11_get16(b + 10);
        ev->width = sk_x11_get16(b + 12);
        ev->height = sk_x11_get16(b + 14);
        ev->count = sk_x11_get16(b + 16);
        return true;
    case SK_X11_DESTROY_NOTIFY:
    case SK_X11_UNMAP_NOTIFY:
    case SK_X11_MAP_NOTIFY:
        ev->window = sk_x11_get32(b + 8);
        return true;
    case SK_X11_CONFIGURE_NOTIFY:
        ev->window = sk_x11_get32(b + 8);
        ev->x = (int16_t)sk_x11_get16(b + 16);
        ev->y = (int16_t)sk_x11_get16(b + 18);
        ev->width = sk_x11_get16(b + 20);
        ev->height = sk_x11_get16(b + 22);
        return true;
    case SK_X11_PROPERTY_NOTIFY:
        ev->window = sk_x11_get32(b + 4);
        ev->atom = sk_x11_get32(b + 8);
        ev->time = sk_x11_get32(b + 12);
        ev->mode = b[16];
        return true;
    case SK_X11_SELECTION_CLEAR:
        ev->time = sk_x11_get32(b + 4);
        ev->owner = ev->window = sk_x11_get32(b + 8);
        ev->selection = sk_x11_get32(b + 12);
        return true;
    case SK_X11_SELECTION_REQUEST:
        ev->time = sk_x11_get32(b + 4);
        ev->owner = ev->window = sk_x11_get32(b + 8);
        ev->requestor = sk_x11_get32(b + 12);
        ev->selection = sk_x11_get32(b + 16);
        ev->target = sk_x11_get32(b + 20);
        ev->property = sk_x11_get32(b + 24);
        return true;
    case SK_X11_SELECTION_NOTIFY:
        ev->time = sk_x11_get32(b + 4);
        ev->requestor = ev->window = sk_x11_get32(b + 8);
        ev->selection = sk_x11_get32(b + 12);
        ev->target = sk_x11_get32(b + 16);
        ev->property = sk_x11_get32(b + 20);
        return true;
    case SK_X11_CLIENT_MESSAGE:
        ev->window = sk_x11_get32(b + 4);
        ev->atom = sk_x11_get32(b + 8);
        for (int i = 0; i < 5; i++)
            ev->data[i] = sk_x11_get32(b + 12 + 4 * i);
        return true;
    case SK_X11_MAPPING_NOTIFY:
        ev->request = b[4];
        ev->first_keycode = b[5];
        ev->count = b[6];
        return true;
    default:
        if (shm_event && ev->type == shm_event) {
            /* MIT-SHM's completion: the drawable, the minor and major
             * opcode of the request, the segment and the offset. */
            ev->window = sk_x11_get32(b + 4);
            ev->shmseg = sk_x11_get32(b + 12);
            return true;
        }
        return false;
    }
}

/* ---- Resources ---- */

static sk_str trim(const char *s, const char *e)
{
    return sk_str_trim(sk_str_n(s, (size_t)(e - s)));
}

/* A plain decimal with an optional fraction, read without the locale. */
static double decimal(sk_str s)
{
    double v = 0, scale = 1;
    bool point = false, any = false;
    for (size_t i = 0; i < s.len; i++) {
        char ch = s.ptr[i];
        if (ch == '.' && !point) {
            point = true;
        } else if (ch >= '0' && ch <= '9') {
            any = true;
            if (point)
                v += (ch - '0') * (scale /= 10);
            else
                v = v * 10 + (ch - '0');
        } else {
            return 0;
        }
    }
    return any ? v : 0;
}

double sk_x11_xft_dpi(const char *s, size_t n)
{
    const char *end = s + n;
    double dpi = 0;
    while (s < end) {
        const char *nl = memchr(s, '\n', (size_t)(end - s));
        const char *le = nl ? nl : end;
        const char *colon = memchr(s, ':', (size_t)(le - s));
        if (colon && sk_str_eq_c(trim(s, colon), "Xft.dpi"))
            dpi = decimal(trim(colon + 1, le));
        s = nl ? nl + 1 : end;
    }
    return dpi;
}
