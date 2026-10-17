/* The window layer over X11. A window's frame lives in this process, in
 * a System V shared memory segment the server attached when MIT-SHM
 * allows, else in plain memory sent by PutImage in chunks. A present
 * waits until the server has read the segment, so the app never draws
 * into memory the server is reading. A hidden window is never mapped: its
 * presents go to a pixmap of its size, which readback reads.
 *
 * The clipboard follows the ICCCM: an unmapped window owns the
 * selections and receives conversions, and text past one request goes by
 * INCR both ways. Getting the clipboard always asks the server, even when
 * this process owns it. */
#include "xui.h"

#include "../../base/log.h"
#include "../../base/utf8.h"
#include "../../core/loop.h"
#include "keys.h"
#include "x11.h"

#include <math.h>
#include <poll.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>

#define FRAME_MS 16
#define CLICK_MS 400u
#define CLICK_PX 4
#define GET_TIMEOUT_MS 2000
/* INCR transfers out at once; a requestor that went away is forgotten. */
#define MAX_SENDS 8
#define MAX_TITLE 4096
/* The most a conversion's property is read at once. */
#define MAX_GET ((size_t)256 << 20)
/* PutImage's chunks stay under this, whatever BIG-REQUESTS allows. */
#define MAX_CHUNK ((size_t)1 << 20)

enum {
    A_WM_PROTOCOLS,
    A_WM_DELETE_WINDOW,
    A_NET_WM_PING,
    A_NET_WM_NAME,
    A_NET_WM_PID,
    A_NET_ACTIVE_WINDOW,
    A_UTF8_STRING,
    A_CLIPBOARD,
    A_TARGETS,
    A_INCR,
    A_TEXT,
    A_SELECTION,
    N_ATOMS
};

static const char *const ATOM_NAMES[N_ATOMS] = {
    "WM_PROTOCOLS", "WM_DELETE_WINDOW", "_NET_WM_PING", "_NET_WM_NAME", "_NET_WM_PID",
    "_NET_ACTIVE_WINDOW", "UTF8_STRING", "CLIPBOARD", "TARGETS", "INCR", "TEXT",
    "SKINNED_SELECTION",
};

/* The cursor font's glyphs (X11's cursorfont.h). */
static const uint16_t GLYPHS[] = {
    [SK_CURSOR_ARROW] = 68,      /* left_ptr */
    [SK_CURSOR_IBEAM] = 152,     /* xterm */
    [SK_CURSOR_POINTER] = 60,    /* hand2 */
    [SK_CURSOR_RESIZE_EW] = 108, /* sb_h_double_arrow */
    [SK_CURSOR_RESIZE_NS] = 116, /* sb_v_double_arrow */
};

enum {
    EVENTS = SK_X11_KEY_PRESS_MASK | SK_X11_KEY_RELEASE_MASK | SK_X11_BUTTON_PRESS_MASK |
             SK_X11_BUTTON_RELEASE_MASK | SK_X11_ENTER_WINDOW_MASK | SK_X11_LEAVE_WINDOW_MASK |
             SK_X11_POINTER_MOTION_MASK | SK_X11_EXPOSURE_MASK | SK_X11_STRUCTURE_NOTIFY_MASK |
             SK_X11_FOCUS_CHANGE_MASK | SK_X11_PROPERTY_CHANGE_MASK,
};

struct sk_window {
    sk_window *next;
    uint32_t id;
    uint32_t pixmap; /* a hidden window's picture on the server */
    void *ud;
    int pw, ph; /* pixels */
    bool hidden, mapped, focused;
    bool no_focus; /* WM_HINTS's input False: the window manager never gives it the keys */
    sk_image frame;
    uint32_t seg; /* the frame's MIT-SHM segment, 0 for none */
    void *shm;
    int frame_timer;
    uint64_t last_frame;
    sk_rect exposed;
    int click_button, clicks, click_x, click_y;
    uint32_t click_time;
    uint8_t repeat_key; /* the key whose release an autorepeat dropped */
};

typedef struct {
    int id;
    sk_ui_task task;
    void *ud;
    bool repeat;
} ui_timer;

typedef struct {
    bool primary;
    void (*done)(void *ud, const char *utf8);
    void *ud;
} clip_get;

static struct {
    sk_x11 *c;
    sk_loop *loop;
    sk_ui_handler handler;
    void *ud;
    char *app_id;
    double scale;
    sk_x11_keymap keymap;
    uint32_t atoms[N_ATOMS];
    uint32_t helper; /* the unmapped window that holds the selections */
    uint32_t colormap;
    uint32_t gc, font;
    bool font_failed;
    uint32_t cursors[SK_CURSOR_HIDDEN + 1];
    bool shm, swap;
    sk_window *windows;
    int dispatch_timer;
    bool lost;
    SK_VEC(ui_timer *) timers;
    /* The selections owned: CLIPBOARD's text, then PRIMARY's. */
    char *owned[2];
    SK_VEC(sk_x11_incr) sends;
    /* Clipboard reads, the first in progress when get_active. */
    SK_VEC(clip_get) gets;
    bool get_active, get_incr;
    uint32_t get_target, get_type;
    sk_buf get_data;
    int get_timer;
} ui;

static void dispatch(void);

/* ---- The connection ---- */

static void lose(void)
{
    if (ui.lost)
        return;
    ui.lost = true;
    sk_log("the X server's connection is lost");
    sk_loop_stop(ui.loop);
}

static void on_dispatch_timer(sk_loop *loop, void *ud)
{
    (void)loop, (void)ud;
    ui.dispatch_timer = 0;
    dispatch();
}

/* After requests: sends them, and has events that came meanwhile handled
 * on the next round. */
static void settle(void)
{
    if (!ui.c || ui.lost)
        return;
    if (!sk_x11_flush(ui.c)) {
        lose();
        return;
    }
    if (sk_x11_pending(ui.c) && !ui.dispatch_timer)
        ui.dispatch_timer = sk_loop_timer(ui.loop, 0, 0, on_dispatch_timer, NULL);
}

static void on_io(sk_loop *loop, int fd, short revents, void *ud)
{
    (void)loop, (void)fd, (void)revents, (void)ud;
    if (sk_x11_read(ui.c, 0) < 0) {
        lose();
        return;
    }
    dispatch();
}

static void on_error(void *ud, const uint8_t *e, uint64_t seq)
{
    (void)ud;
    sk_log("X error %u on request %u.%u (sequence %llu), resource 0x%x", e[1], e[10],
           sk_x11_get16(e + 8), (unsigned long long)seq, (unsigned)sk_x11_get32(e + 4));
}

static bool host_lsb(void)
{
    uint32_t one = 1;
    uint8_t b;
    memcpy(&b, &one, 1);
    return b == 1;
}

static int px(int points)
{
    long v = lround(points * ui.scale);
    return (int)SK_MIN(SK_MAX(v, 1), 32767);
}

static int points(int pixels)
{
    return (int)lround(pixels / ui.scale);
}

static void emit(const sk_event *e)
{
    if (ui.handler)
        ui.handler(ui.ud, e);
}

static sk_window *find(uint32_t id)
{
    for (sk_window *w = ui.windows; w; w = w->next)
        if (w->id == id)
            return w;
    return NULL;
}

/* A property of 32-bit values, sent in the client's byte order. */
static void put_card32(uint32_t win, uint32_t prop, uint32_t type, const uint32_t *v, size_t n)
{
    uint8_t buf[18 * 4];
    if (n > SK_LEN(buf) / 4)
        return;
    for (size_t i = 0; i < n; i++)
        sk_x11_set32(buf + 4 * i, v[i]);
    sk_x11_change_property(ui.c, 0, win, prop, type, 32, buf, (uint32_t)n);
}

static void put_bytes(uint32_t win, uint32_t prop, uint32_t type, const void *p, size_t n)
{
    sk_x11_change_property(ui.c, 0, win, prop, type, 8, p, (uint32_t)n);
}

/* ---- Open, run, close ---- */

static double scale_from_resources(void)
{
    sk_x11_reply r;
    sk_x11_prop p;
    double dpi = 0;
    if (sk_x11_prop_get(ui.c, false, ui.c->root, SK_X11_RESOURCE_MANAGER, SK_X11_STRING, 1 << 20,
                        &r, &p) &&
        p.format == 8)
        dpi = sk_x11_xft_dpi((const char *)p.value, p.len);
    sk_x11_reply_free(&r);
    if (dpi <= 0)
        return 1;
    return SK_MIN(SK_MAX(dpi / 96, 0.5), 8.0);
}

static void free_all(void)
{
    for (size_t i = 0; i < ui.timers.len; i++)
        free(ui.timers.ptr[i]);
    sk_vec_free(&ui.timers);
    for (size_t i = 0; i < ui.sends.len; i++)
        free(ui.sends.ptr[i].data);
    sk_vec_free(&ui.sends);
    sk_vec_free(&ui.gets);
    sk_buf_free(&ui.get_data);
    free(ui.owned[0]);
    free(ui.owned[1]);
    sk_x11_keymap_free(&ui.keymap);
    free(ui.app_id);
    sk_loop_free(ui.loop);
    sk_x11_free(ui.c);
    memset(&ui, 0, sizeof ui);
}

bool sk_xui_open(const char *display, const char *app_id, sk_ui_handler handler, void *ud,
                 char **err)
{
    if (ui.c) {
        *err = sk_xstrdup("the display is open already");
        return false;
    }
    sk_x11 *c = sk_x11_open(display, err);
    if (!c)
        return false;
    memset(&ui, 0, sizeof ui);
    ui.c = c;
    ui.loop = sk_loop_new();
    c->on_error = on_error;
    ui.handler = handler;
    ui.ud = ud;
    ui.app_id = sk_xstrdup(app_id && *app_id ? app_id : "skinned");
    sk_x11_extensions(c);
    ui.swap = c->image_lsb != host_lsb();
    ui.shm = c->shm_opcode && !ui.swap;
    if (!sk_x11_intern(c, ATOM_NAMES, N_ATOMS, ui.atoms) || !sk_x11_keymap_load(c, &ui.keymap)) {
        *err = sk_xstrdup("the X server stopped answering");
        free_all();
        return false;
    }
    ui.scale = scale_from_resources();
    if (c->visual != c->root_visual) {
        ui.colormap = sk_x11_id(c);
        sk_x11_create_colormap(c, ui.colormap, c->root, c->visual);
    }
    ui.helper = sk_x11_id(c);
    uint32_t mask = SK_X11_PROPERTY_CHANGE_MASK;
    sk_x11_create_window(c, 0, ui.helper, c->root, 0, 0, 1, 1, 2 /* InputOnly */, 0,
                         SK_X11_CW_EVENT_MASK, &mask);
    sk_loop_watch(ui.loop, c->fd, POLLIN, on_io, NULL);
    settle();
    if (ui.lost) {
        *err = sk_xstrdup("the X server closed the connection");
        free_all();
        return false;
    }
    return true;
}

bool sk_xui_run(void)
{
    if (!ui.c || ui.lost)
        return false;
    dispatch();
    if (ui.lost)
        return false;
    sk_loop_run(ui.loop);
    return !ui.lost;
}

void sk_xui_once(int timeout_ms)
{
    if (ui.c && !ui.lost)
        sk_loop_once(ui.loop, timeout_ms);
}

static void quit_task(void *ud)
{
    (void)ud;
    if (ui.loop)
        sk_loop_stop(ui.loop);
}

void sk_xui_quit(void)
{
    if (ui.loop)
        sk_loop_post(ui.loop, quit_task, NULL);
}

void sk_xui_close(void)
{
    if (!ui.c)
        return;
    while (ui.windows)
        sk_xui_window_free(ui.windows);
    sk_x11 *c = ui.c;
    for (size_t i = 0; i < SK_LEN(ui.cursors); i++)
        if (ui.cursors[i])
            sk_x11_free_cursor(c, ui.cursors[i]);
    if (ui.font)
        sk_x11_close_font(c, ui.font);
    if (ui.gc)
        sk_x11_free_gc(c, ui.gc);
    if (ui.colormap)
        sk_x11_free_colormap(c, ui.colormap);
    sk_x11_destroy_window(c, ui.helper);
    if (!ui.lost)
        sk_x11_flush(c);
    free_all();
}

bool sk_xui_sync(void)
{
    if (!ui.c || ui.lost)
        return false;
    bool ok = sk_x11_sync(ui.c);
    if (!ok)
        lose();
    settle();
    return ok;
}

bool sk_xui_shm(void)
{
    return ui.shm;
}

/* ---- Tasks and timers ---- */

void sk_xui_post(sk_ui_task task, void *ud)
{
    sk_xui_post_owned(task, ud, NULL);
}

/* With no connection, or once it closed, nothing runs: what the task
 * owned is dropped at once, or when the loop is freed. */
void sk_xui_post_owned(sk_ui_task task, void *ud, sk_ui_task drop)
{
    if (ui.loop)
        sk_loop_post_owned(ui.loop, task, ud, drop);
    else if (drop)
        drop(ud);
}

static void drop_timer(int id)
{
    for (size_t i = 0; i < ui.timers.len; i++) {
        if (ui.timers.ptr[i]->id == id) {
            free(ui.timers.ptr[i]);
            ui.timers.ptr[i] = ui.timers.ptr[--ui.timers.len];
            return;
        }
    }
}

static void on_timer(sk_loop *loop, void *ud)
{
    (void)loop;
    ui_timer *t = ud;
    sk_ui_task task = t->task;
    void *task_ud = t->ud;
    if (!t->repeat)
        drop_timer(t->id);
    /* The task may cancel this timer, or close the display: nothing of t
     * is touched after. */
    task(task_ud);
    settle();
}

int sk_xui_timer(uint64_t delay_ms, uint64_t repeat_ms, sk_ui_task task, void *ud)
{
    if (!ui.loop)
        return 0;
    ui_timer *t = sk_xcalloc(1, sizeof *t);
    t->task = task;
    t->ud = ud;
    t->repeat = repeat_ms != 0;
    t->id = sk_loop_timer(ui.loop, delay_ms, repeat_ms, on_timer, t);
    sk_vec_push(&ui.timers, t);
    return t->id;
}

void sk_xui_cancel(int id)
{
    if (!ui.loop)
        return;
    sk_loop_cancel(ui.loop, id);
    drop_timer(id);
}

/* ---- Frames ---- */

static uint32_t drawable_of(const sk_window *w)
{
    return w->pixmap ? w->pixmap : w->id;
}

static uint32_t gc(uint32_t drawable)
{
    if (!ui.gc) {
        ui.gc = sk_x11_id(ui.c);
        uint32_t no_exposures = 0;
        sk_x11_create_gc(ui.c, ui.gc, drawable, 1u << 16 /* graphics-exposures */, &no_exposures);
    }
    return ui.gc;
}

/* The frame in a segment the server attaches; false, and MIT-SHM given
 * up, when the server cannot attach it, as a server elsewhere or in
 * another IPC namespace cannot. */
static bool shm_frame(sk_window *w)
{
    size_t size = (size_t)w->pw * (size_t)w->ph * 4;
    int id = shmget(IPC_PRIVATE, size, IPC_CREAT | 0600);
    if (id < 0) {
        ui.shm = false;
        return false;
    }
    void *p = shmat(id, NULL, 0);
    if (p == (void *)-1) {
        shmctl(id, IPC_RMID, NULL);
        ui.shm = false;
        return false;
    }
    uint32_t seg = sk_x11_id(ui.c);
    int e = sk_x11_check(ui.c, sk_x11_shm_attach(ui.c, seg, (uint32_t)id, true));
    /* Both sides hold it now: it goes when both let go, even if we die. */
    shmctl(id, IPC_RMID, NULL);
    if (e != 0) {
        shmdt(p);
        ui.shm = false;
        if (e < 0)
            lose();
        return false;
    }
    w->seg = seg;
    w->shm = p;
    w->frame = (sk_image){p, w->pw, w->ph, w->pw, false};
    return true;
}

static void frame_make(sk_window *w)
{
    if (w->hidden) {
        w->pixmap = sk_x11_id(ui.c);
        sk_x11_create_pixmap(ui.c, 24, w->pixmap, w->id, w->pw, w->ph);
    }
    if (!(ui.shm && shm_frame(w)))
        sk_image_init(&w->frame, w->pw, w->ph);
}

static void frame_free(sk_window *w)
{
    if (w->seg) {
        sk_x11_shm_detach(ui.c, w->seg);
        shmdt(w->shm);
        w->seg = 0;
        w->shm = NULL;
        memset(&w->frame, 0, sizeof w->frame);
    } else {
        sk_image_free(&w->frame);
    }
    if (w->pixmap) {
        sk_x11_free_pixmap(ui.c, w->pixmap);
        w->pixmap = 0;
    }
}

typedef struct {
    uint8_t type;
    uint32_t drawable, seg;
} shm_wait;

static bool shm_done(const uint8_t ev[32], void *ud)
{
    const shm_wait *s = ud;
    return (ev[0] & 0x7f) == s->type && sk_x11_get32(ev + 4) == s->drawable &&
           sk_x11_get32(ev + 12) == s->seg;
}

static void put_chunks(sk_window *w, sk_rect r)
{
    sk_x11 *c = ui.c;
    size_t room = SK_MIN((size_t)c->max_request, MAX_CHUNK) - 32;
    int cols = (int)SK_MIN((size_t)r.w, room / 4);
    for (int x = r.x; x < r.x + r.w; x += cols) {
        int cw = SK_MIN(cols, r.x + r.w - x);
        int rows = (int)SK_MAX(room / ((size_t)cw * 4), (size_t)1);
        for (int y = r.y; y < r.y + r.h; y += rows) {
            int ch = SK_MIN(rows, r.y + r.h - y);
            sk_x11_put_image(c, drawable_of(w), gc(drawable_of(w)), x, y, cw, ch, 24,
                             sk_image_row(&w->frame, y) + x, (size_t)w->frame.stride, ui.swap);
            if (c->out.len >= MAX_CHUNK && !sk_x11_flush(c))
                return;
        }
    }
}

static void present(sk_window *w, const sk_rect *rects, size_t n)
{
    if (!ui.c || ui.lost || !w->frame.px)
        return;
    sk_rect all = sk_image_bounds(&w->frame);
    size_t last = SIZE_MAX;
    for (size_t i = 0; i < n; i++)
        if (!sk_rect_empty(sk_rect_intersect(rects[i], all)))
            last = i;
    uint64_t put = 0;
    for (size_t i = 0; last != SIZE_MAX && i <= last; i++) {
        sk_rect r = sk_rect_intersect(rects[i], all);
        if (sk_rect_empty(r))
            continue;
        if (w->seg)
            put = sk_x11_shm_put_image(ui.c, drawable_of(w), gc(drawable_of(w)), w->frame.w,
                                       w->frame.h, r.x, r.y, r.w, r.h, r.x, r.y, 24, i == last,
                                       w->seg, 0);
        else
            put_chunks(w, r);
    }
    if (put) {
        /* The last put's completion says the server is done with the
         * segment; a put the server refused completes nothing. */
        int e = sk_x11_check(ui.c, put);
        shm_wait s = {ui.c->shm_event, drawable_of(w), w->seg};
        uint8_t ev[32];
        if (e < 0 || (e == 0 && !sk_x11_wait_event(ui.c, shm_done, &s, ev, SK_X11_TIMEOUT_MS)))
            lose();
        else if (e > 0)
            sk_log("X error %d on MIT-SHM's PutImage", e);
    }
    settle();
}

/* ---- Windows ---- */

static void set_title(sk_window *w, const char *title)
{
    size_t n = strlen(title);
    if (n > MAX_TITLE)
        n = sk_utf8_whole(title, MAX_TITLE);
    put_bytes(w->id, ui.atoms[A_NET_WM_NAME], ui.atoms[A_UTF8_STRING], title, n);
    sk_buf latin = {0};
    sk_x11_latin1_from_utf8(&latin, title, n);
    put_bytes(w->id, SK_X11_WM_NAME, SK_X11_STRING, latin.ptr, latin.len);
    sk_buf_free(&latin);
}

static void set_properties(sk_window *w, const sk_window_opts *o)
{
    uint32_t protocols[] = {ui.atoms[A_WM_DELETE_WINDOW], ui.atoms[A_NET_WM_PING]};
    put_card32(w->id, ui.atoms[A_WM_PROTOCOLS], SK_X11_ATOM, protocols, SK_LEN(protocols));
    /* WM_CLASS: the instance, then the class, each ending in a NUL. */
    sk_buf cls = {0};
    sk_buf_append(&cls, ui.app_id, strlen(ui.app_id) + 1);
    sk_buf_append(&cls, ui.app_id, strlen(ui.app_id) + 1);
    put_bytes(w->id, SK_X11_WM_CLASS, SK_X11_STRING, cls.ptr, cls.len);
    sk_buf_free(&cls);
    char host[256];
    if (gethostname(host, sizeof host) == 0) {
        host[sizeof host - 1] = 0;
        put_bytes(w->id, SK_X11_WM_CLIENT_MACHINE, SK_X11_STRING, host, strlen(host));
    }
    uint32_t pid = (uint32_t)getpid();
    put_card32(w->id, ui.atoms[A_NET_WM_PID], SK_X11_CARDINAL, &pid, 1);
    /* WM_HINTS: it takes input, unless it is never to take the focus
     * (ICCCM's No Input model, with no WM_TAKE_FOCUS among the
     * protocols), and starts in the normal state. */
    uint32_t hints[9] = {1 | 2, o->no_focus ? 0 : 1, 1};
    put_card32(w->id, SK_X11_WM_HINTS, SK_X11_WM_HINTS, hints, SK_LEN(hints));
    uint32_t size[18] = {0};
    if (o->min_w > 0 || o->min_h > 0) {
        size[0] = 1 << 4; /* PMinSize */
        size[5] = o->min_w > 0 ? (uint32_t)px(o->min_w) : 1;
        size[6] = o->min_h > 0 ? (uint32_t)px(o->min_h) : 1;
    }
    put_card32(w->id, SK_X11_WM_NORMAL_HINTS, SK_X11_WM_SIZE_HINTS, size, SK_LEN(size));
    set_title(w, o->title ? o->title : "");
}

sk_window *sk_xui_window_new(const sk_window_opts *o, char **err)
{
    if (!ui.c || ui.lost) {
        *err = sk_xstrdup("the display is not open");
        return NULL;
    }
    sk_x11 *c = ui.c;
    sk_window *w = sk_xcalloc(1, sizeof *w);
    w->ud = o->ud;
    w->hidden = o->hidden;
    w->no_focus = o->no_focus;
    w->pw = px(o->w > 0 ? o->w : 800);
    w->ph = px(o->h > 0 ? o->h : 600);
    w->id = sk_x11_id(c);
    uint32_t mask = SK_X11_CW_BORDER_PIXEL | SK_X11_CW_BIT_GRAVITY | SK_X11_CW_EVENT_MASK;
    uint32_t values[4] = {0, 1 /* NorthWest */, EVENTS, ui.colormap};
    if (ui.colormap)
        mask |= SK_X11_CW_COLORMAP;
    uint64_t made = sk_x11_create_window(c, 24, w->id, c->root, 0, 0, w->pw, w->ph,
                                         1 /* InputOutput */, c->visual, mask, values);
    set_properties(w, o);
    int e = sk_x11_check(c, made);
    if (e != 0) {
        sk_buf m = {0};
        if (e < 0)
            sk_buf_printf(&m, "the X server's connection is lost");
        else
            sk_buf_printf(&m, "the X server refused the window (error %d)", e);
        *err = m.ptr;
        if (e < 0)
            lose();
        free(w);
        return NULL;
    }
    frame_make(w);
    w->next = ui.windows;
    ui.windows = w;
    if (!w->hidden)
        sk_x11_map_window(c, w->id);
    settle();
    return w;
}

void sk_xui_window_free(sk_window *w)
{
    if (!w)
        return;
    for (sk_window **p = &ui.windows; *p; p = &(*p)->next) {
        if (*p == w) {
            *p = w->next;
            break;
        }
    }
    if (w->frame_timer)
        sk_loop_cancel(ui.loop, w->frame_timer);
    frame_free(w);
    sk_x11_destroy_window(ui.c, w->id);
    settle();
    free(w);
}

void *sk_xui_window_ud(const sk_window *w)
{
    return w->ud;
}

uint32_t sk_xui_window_id(const sk_window *w)
{
    return w->id;
}

/* WM_NORMAL_HINTS's least size, and the window grown to it when it is
 * smaller (main.js's setMinimumSize and growTo). */
void sk_xui_window_set_min_size(sk_window *w, int min_w, int min_h)
{
    if (!ui.c)
        return;
    uint32_t size[18] = {0};
    size[0] = 1 << 4; /* PMinSize */
    size[5] = (uint32_t)px(SK_MAX(min_w, 1));
    size[6] = (uint32_t)px(SK_MAX(min_h, 1));
    put_card32(w->id, SK_X11_WM_NORMAL_HINTS, SK_X11_WM_SIZE_HINTS, size, SK_LEN(size));
    if (w->pw < (int)size[5] || w->ph < (int)size[6]) {
        uint32_t wh[2] = {(uint32_t)SK_MAX(w->pw, (int)size[5]), (uint32_t)SK_MAX(w->ph, (int)size[6])};
        sk_x11_configure_window(ui.c, w->id, 1 << 2 | 1 << 3 /* width, height */, wh);
    }
}

void sk_xui_window_set_title(sk_window *w, const char *title)
{
    set_title(w, title ? title : "");
    settle();
}

void sk_xui_window_size(const sk_window *w, int *pw, int *ph)
{
    *pw = points(w->pw);
    *ph = points(w->ph);
}

double sk_xui_window_scale(const sk_window *w)
{
    (void)w;
    return ui.scale;
}

static uint32_t cursor(sk_cursor k)
{
    if ((unsigned)k > SK_CURSOR_HIDDEN)
        k = SK_CURSOR_ARROW;
    if (ui.cursors[k])
        return ui.cursors[k];
    sk_x11 *c = ui.c;
    uint32_t id = sk_x11_id(c);
    if (k == SK_CURSOR_HIDDEN) {
        /* A cursor whose mask is a cleared bitmap shows nothing. */
        uint32_t pix = sk_x11_id(c), g = sk_x11_id(c), black = 0;
        sk_x11_create_pixmap(c, 1, pix, c->root, 1, 1);
        sk_x11_create_gc(c, g, pix, 1u << 2 /* foreground */, &black);
        sk_x11_fill_rect(c, pix, g, 0, 0, 1, 1);
        sk_x11_create_cursor(c, id, pix, pix);
        sk_x11_free_gc(c, g);
        sk_x11_free_pixmap(c, pix);
    } else {
        if (!ui.font && !ui.font_failed) {
            ui.font = sk_x11_id(c);
            if (sk_x11_check(c, sk_x11_open_font(c, ui.font, "cursor")) != 0) {
                ui.font = 0;
                ui.font_failed = true;
            }
        }
        if (!ui.font)
            return 0; /* the parent's cursor */
        sk_x11_create_glyph_cursor(c, id, ui.font, GLYPHS[k]);
    }
    ui.cursors[k] = id;
    return id;
}

void sk_xui_window_set_cursor(sk_window *w, sk_cursor k)
{
    uint32_t id = cursor(k);
    sk_x11_change_attributes(ui.c, w->id, SK_X11_CW_CURSOR, &id);
    settle();
}

void sk_xui_window_focus(sk_window *w)
{
    sk_x11 *c = ui.c;
    uint32_t above = 0;
    sk_x11_configure_window(c, w->id, 1 << 6 /* stack mode */, &above);
    if (w->no_focus) {
        settle();
        return;
    }
    if (w->mapped)
        sk_x11_set_input_focus(c, 2 /* to the parent */, w->id, 0);
    /* A window manager raises and focuses on _NET_ACTIVE_WINDOW. */
    uint8_t ev[32] = {SK_X11_CLIENT_MESSAGE, 32};
    sk_x11_set32(ev + 4, w->id);
    sk_x11_set32(ev + 8, ui.atoms[A_NET_ACTIVE_WINDOW]);
    sk_x11_set32(ev + 12, 1); /* from an application */
    sk_x11_send_event(c, false, c->root,
                      SK_X11_SUBSTRUCTURE_NOTIFY_MASK | SK_X11_SUBSTRUCTURE_REDIRECT_MASK, ev);
    settle();
}

sk_image *sk_xui_window_frame(sk_window *w)
{
    return &w->frame;
}

void sk_xui_window_present(sk_window *w, const sk_rect *dirty, size_t n)
{
    sk_rect all = sk_image_bounds(&w->frame);
    if (n == 0)
        present(w, &all, 1);
    else
        present(w, dirty, n);
}

static void on_frame(sk_loop *loop, void *ud)
{
    (void)loop;
    sk_window *w = ud;
    w->frame_timer = 0;
    w->last_frame = sk_now_ms();
    sk_event e = {.type = SK_EV_FRAME, .win = w};
    emit(&e);
    settle();
}

void sk_xui_window_request_frame(sk_window *w)
{
    if (w->frame_timer)
        return;
    uint64_t now = sk_now_ms(), due = w->last_frame + FRAME_MS;
    w->frame_timer = sk_loop_timer(ui.loop, due > now ? due - now : 0, 0, on_frame, w);
}

bool sk_xui_window_readback(sk_window *w, sk_image *out)
{
    sk_image_init(out, 0, 0);
    if (!ui.c || ui.lost)
        return false;
    sk_x11_reply r;
    uint8_t e;
    size_t want = (size_t)w->pw * (size_t)w->ph * 4;
    uint64_t s = sk_x11_get_image(ui.c, drawable_of(w), 0, 0, w->pw, w->ph);
    bool ok = sk_x11_reply_wait(ui.c, s, &r, &e) && r.data[1] == 24 && r.len >= 32 + want;
    if (ok) {
        sk_image_init(out, w->pw, w->ph);
        memcpy(out->px, r.data + 32, want);
        for (size_t i = 0; i < (size_t)w->pw * (size_t)w->ph; i++) {
            uint32_t p = out->px[i];
            if (ui.swap)
                p = p >> 24 | (p >> 8 & 0xff00) | (p << 8 & 0xff0000) | p << 24;
            out->px[i] = p | 0xff000000; /* depth 24 has no alpha */
        }
    }
    sk_x11_reply_free(&r);
    if (ui.c->dead)
        lose();
    settle();
    return ok;
}

/* ---- Input ---- */

static void mouse(sk_window *w, sk_event_type type, const sk_x11_event *ev, int button, int clicks)
{
    sk_event e = {.type = type, .win = w};
    e.mouse.x = ev->x / ui.scale;
    e.mouse.y = ev->y / ui.scale;
    e.mouse.button = button;
    e.mouse.clicks = clicks;
    e.mouse.mods = sk_x11_mods(&ui.keymap, ev->state);
    emit(&e);
}

static void button_press(sk_window *w, const sk_x11_event *ev)
{
    int b = ev->detail;
    if (b >= 4 && b <= 7) {
        /* The wheel: a line a click, up and left positive. */
        sk_event e = {.type = SK_EV_SCROLL, .win = w};
        e.scroll.x = ev->x / ui.scale;
        e.scroll.y = ev->y / ui.scale;
        e.scroll.dy = b == 4 ? 1 : b == 5 ? -1 : 0;
        e.scroll.dx = b == 6 ? 1 : b == 7 ? -1 : 0;
        e.scroll.mods = sk_x11_mods(&ui.keymap, ev->state);
        emit(&e);
        return;
    }
    if (b < 1 || b > 3)
        return;
    bool again = b == w->click_button && (uint32_t)(ev->time - w->click_time) <= CLICK_MS &&
                 abs(ev->x - w->click_x) <= CLICK_PX && abs(ev->y - w->click_y) <= CLICK_PX;
    w->clicks = again ? w->clicks + 1 : 1;
    w->click_button = b;
    w->click_time = ev->time;
    w->click_x = ev->x;
    w->click_y = ev->y;
    mouse(w, SK_EV_MOUSE_DOWN, ev, b, w->clicks);
}

/* A release that a press of the same key at the same time follows is the
 * server's autorepeat: the pair becomes one repeated press. */
static bool autorepeat(const sk_x11_event *ev)
{
    const uint8_t *next = sk_x11_peek_event(ui.c, 0);
    if (!next && sk_x11_read(ui.c, 0) >= 0)
        next = sk_x11_peek_event(ui.c, 0);
    return next && (next[0] & 0x7f) == SK_X11_KEY_PRESS && next[1] == ev->detail &&
           sk_x11_get32(next + 4) == ev->time && sk_x11_get32(next + 12) == ev->window;
}

static void key(sk_window *w, const sk_x11_event *ev)
{
    sk_event e = {.type = ev->type == SK_X11_KEY_PRESS ? SK_EV_KEY_DOWN : SK_EV_KEY_UP, .win = w};
    if (e.type == SK_EV_KEY_UP && autorepeat(ev)) {
        w->repeat_key = ev->detail;
        return;
    }
    sk_x11_key_event(&ui.keymap, ev->detail, ev->state, &e.key);
    if (e.type == SK_EV_KEY_DOWN) {
        e.key.repeat = w->repeat_key == ev->detail;
        w->repeat_key = 0;
    }
    emit(&e);
}

/* Whether a later event of type for the same window waits in the queue. */
static bool superseded(uint8_t type, uint32_t window, size_t window_at)
{
    for (size_t i = 0;; i++) {
        const uint8_t *next = sk_x11_peek_event(ui.c, i);
        if (!next)
            return false;
        if ((next[0] & 0x7f) == type && sk_x11_get32(next + window_at) == window)
            return true;
    }
}

static void configure(sk_window *w, const sk_x11_event *ev)
{
    if (superseded(SK_X11_CONFIGURE_NOTIFY, w->id, 8) ||
        (ev->width == w->pw && ev->height == w->ph) || ev->width <= 0 || ev->height <= 0)
        return;
    frame_free(w);
    w->pw = ev->width;
    w->ph = ev->height;
    frame_make(w);
    sk_event e = {.type = SK_EV_RESIZE, .win = w};
    e.size.w = points(w->pw);
    e.size.h = points(w->ph);
    e.size.scale = ui.scale;
    emit(&e);
}

static void client_message(sk_window *w, const sk_x11_event *ev, const uint8_t *raw)
{
    if (ev->atom != ui.atoms[A_WM_PROTOCOLS])
        return;
    if (ev->data[0] == ui.atoms[A_WM_DELETE_WINDOW]) {
        sk_event e = {.type = SK_EV_CLOSE, .win = w};
        emit(&e);
    } else if (ev->data[0] == ui.atoms[A_NET_WM_PING]) {
        uint8_t pong[32];
        memcpy(pong, raw, 32);
        pong[0] = SK_X11_CLIENT_MESSAGE;
        sk_x11_set32(pong + 4, ui.c->root);
        uint32_t mask = SK_X11_SUBSTRUCTURE_NOTIFY_MASK | SK_X11_SUBSTRUCTURE_REDIRECT_MASK;
        sk_x11_send_event(ui.c, false, ui.c->root, mask, pong);
    }
}

static void window_event(sk_window *w, const sk_x11_event *ev, const uint8_t *raw)
{
    switch (ev->type) {
    case SK_X11_KEY_PRESS:
    case SK_X11_KEY_RELEASE:
        key(w, ev);
        break;
    case SK_X11_BUTTON_PRESS:
        button_press(w, ev);
        break;
    case SK_X11_BUTTON_RELEASE:
        if (ev->detail >= 1 && ev->detail <= 3)
            mouse(w, SK_EV_MOUSE_UP, ev, ev->detail, w->clicks);
        break;
    case SK_X11_MOTION_NOTIFY:
        if (!superseded(SK_X11_MOTION_NOTIFY, w->id, 12))
            mouse(w, SK_EV_MOUSE_MOVE, ev, 0, 0);
        break;
    case SK_X11_ENTER_NOTIFY:
        mouse(w, SK_EV_MOUSE_MOVE, ev, 0, 0);
        break;
    case SK_X11_LEAVE_NOTIFY:
        if (ev->mode != 1 /* the start of a grab */) {
            sk_event e = {.type = SK_EV_MOUSE_LEAVE, .win = w};
            emit(&e);
        }
        break;
    case SK_X11_FOCUS_IN:
    case SK_X11_FOCUS_OUT: {
        /* A grab's focus changes and the pointer's are not the window's. */
        bool in = ev->type == SK_X11_FOCUS_IN;
        if (ev->mode == 1 || ev->mode == 2 || ev->detail == 5 || in == w->focused)
            break;
        w->focused = in;
        sk_event e = {.type = SK_EV_FOCUS, .win = w};
        e.focused = in;
        emit(&e);
        break;
    }
    case SK_X11_EXPOSE:
        w->exposed = sk_rect_union(w->exposed, sk_rect_make(ev->x, ev->y, ev->width, ev->height));
        if (ev->count == 0) {
            sk_rect r = w->exposed;
            w->exposed = sk_rect_make(0, 0, 0, 0);
            present(w, &r, 1);
        }
        break;
    case SK_X11_CONFIGURE_NOTIFY:
        configure(w, ev);
        break;
    case SK_X11_MAP_NOTIFY:
        w->mapped = true;
        break;
    case SK_X11_UNMAP_NOTIFY:
        w->mapped = false;
        break;
    case SK_X11_CLIENT_MESSAGE:
        client_message(w, ev, raw);
        break;
    default:
        break;
    }
}

/* ---- The clipboard: owning ---- */

static uint32_t sel_atom(bool primary)
{
    return primary ? SK_X11_PRIMARY : ui.atoms[A_CLIPBOARD];
}

static int sel_index(uint32_t atom)
{
    return atom == ui.atoms[A_CLIPBOARD] ? 0 : atom == SK_X11_PRIMARY ? 1 : -1;
}

static void own(bool primary, const char *utf8)
{
    if (!ui.c)
        return;
    free(ui.owned[primary]);
    ui.owned[primary] = sk_xstrdup(utf8 ? utf8 : "");
    /* CurrentTime: the server's own clock, which always wins. */
    sk_x11_set_selection_owner(ui.c, ui.helper, sel_atom(primary), 0);
    settle();
}

void sk_xui_clipboard_set(const char *utf8)
{
    own(false, utf8);
}

void sk_xui_primary_set(const char *utf8)
{
    own(true, utf8);
}

static bool ours(uint32_t win)
{
    return win == ui.helper || find(win);
}

static void drop_send(size_t i)
{
    sk_x11_incr *t = &ui.sends.ptr[i];
    uint32_t requestor = t->requestor;
    free(t->data);
    ui.sends.ptr[i] = ui.sends.ptr[--ui.sends.len];
    for (size_t k = 0; k < ui.sends.len; k++)
        if (ui.sends.ptr[k].requestor == requestor)
            return;
    if (!ours(requestor)) {
        uint32_t none = 0;
        sk_x11_change_attributes(ui.c, requestor, SK_X11_CW_EVENT_MASK, &none);
    }
}

/* Writes text into the requestor's property as target asks; false for a
 * target this does not give. */
static bool answer(uint32_t requestor, uint32_t prop, uint32_t target, const char *text)
{
    if (target == ui.atoms[A_TARGETS]) {
        uint32_t targets[] = {ui.atoms[A_TARGETS], ui.atoms[A_UTF8_STRING], SK_X11_STRING,
                              ui.atoms[A_TEXT]};
        put_card32(requestor, prop, SK_X11_ATOM, targets, SK_LEN(targets));
        return true;
    }
    sk_buf data = {0};
    uint32_t type;
    if (target == ui.atoms[A_UTF8_STRING] || target == ui.atoms[A_TEXT]) {
        type = ui.atoms[A_UTF8_STRING];
        sk_buf_append_c(&data, text);
    } else if (target == SK_X11_STRING) {
        type = SK_X11_STRING;
        sk_x11_latin1_from_utf8(&data, text, strlen(text));
    } else {
        return false;
    }
    if (data.len <= sk_x11_incr_threshold(ui.c)) {
        put_bytes(requestor, prop, type, data.ptr, data.len);
        sk_buf_free(&data);
        return true;
    }
    /* INCR: the size, then a chunk each time the requestor deletes the
     * property, which it is watched for. */
    for (size_t i = 0; i < ui.sends.len; i++)
        if (ui.sends.ptr[i].requestor == requestor && ui.sends.ptr[i].property == prop)
            drop_send(i--);
    if (ui.sends.len >= MAX_SENDS)
        drop_send(0);
    if (!ours(requestor)) {
        uint32_t mask = SK_X11_PROPERTY_CHANGE_MASK;
        sk_x11_change_attributes(ui.c, requestor, SK_X11_CW_EVENT_MASK, &mask);
    }
    uint32_t size = (uint32_t)SK_MIN(data.len, (size_t)UINT32_MAX);
    put_card32(requestor, prop, ui.atoms[A_INCR], &size, 1);
    sk_x11_incr t = {requestor, prop, type, data.ptr, data.len, 0, false};
    sk_vec_push(&ui.sends, t);
    return true;
}

static void on_selection_request(const sk_x11_event *ev)
{
    int i = sel_index(ev->selection);
    /* A requestor that names no property is from before the ICCCM. */
    uint32_t prop = ev->property ? ev->property : ev->target;
    const char *text = i >= 0 && ev->owner == ui.helper ? ui.owned[i] : NULL;
    bool ok = text && answer(ev->requestor, prop, ev->target, text);
    uint8_t note[32] = {SK_X11_SELECTION_NOTIFY};
    sk_x11_set32(note + 4, ev->time);
    sk_x11_set32(note + 8, ev->requestor);
    sk_x11_set32(note + 12, ev->selection);
    sk_x11_set32(note + 16, ev->target);
    sk_x11_set32(note + 20, ok ? prop : 0);
    sk_x11_send_event(ui.c, false, ev->requestor, 0, note);
}

static void on_selection_clear(const sk_x11_event *ev)
{
    int i = sel_index(ev->selection);
    if (i >= 0 && ev->owner == ui.helper) {
        free(ui.owned[i]);
        ui.owned[i] = NULL;
    }
}

/* The requestor deleted the property: the next chunk. */
static void send_next(const sk_x11_event *ev)
{
    for (size_t i = 0; i < ui.sends.len; i++) {
        sk_x11_incr *t = &ui.sends.ptr[i];
        if (t->requestor != ev->window || t->property != ev->atom)
            continue;
        sk_str chunk = sk_x11_incr_next(t, sk_x11_incr_threshold(ui.c));
        put_bytes(t->requestor, t->property, t->type, chunk.ptr, chunk.len);
        if (t->done)
            drop_send(i);
        return;
    }
}

/* ---- The clipboard: asking ---- */

static void get_start(void);

static void get_finish(const char *text)
{
    if (ui.get_timer) {
        sk_loop_cancel(ui.loop, ui.get_timer);
        ui.get_timer = 0;
    }
    clip_get g = ui.gets.ptr[0];
    memmove(ui.gets.ptr, ui.gets.ptr + 1, (ui.gets.len - 1) * sizeof *ui.gets.ptr);
    ui.gets.len--;
    ui.get_active = ui.get_incr = false;
    sk_buf_clear(&ui.get_data);
    g.done(g.ud, text);
    if (ui.c && !ui.get_active)
        get_start();
}

static void on_get_timeout(sk_loop *loop, void *ud)
{
    (void)loop, (void)ud;
    ui.get_timer = 0;
    get_finish(NULL);
    settle();
}

static void arm_get_timer(void)
{
    if (ui.get_timer)
        sk_loop_cancel(ui.loop, ui.get_timer);
    ui.get_timer = sk_loop_timer(ui.loop, GET_TIMEOUT_MS, 0, on_get_timeout, NULL);
}

static void convert(uint32_t target)
{
    ui.get_target = target;
    sk_x11_convert_selection(ui.c, ui.helper, sel_atom(ui.gets.ptr[0].primary), target,
                             ui.atoms[A_SELECTION], 0);
    arm_get_timer();
}

static void get_start(void)
{
    if (!ui.gets.len)
        return;
    ui.get_active = true;
    ui.get_incr = false;
    sk_buf_clear(&ui.get_data);
    convert(ui.atoms[A_UTF8_STRING]);
}

/* The text of a property's bytes, malloc'd; NULL for none. */
static char *decode(uint32_t type, uint8_t format, const uint8_t *p, size_t n)
{
    if (format != 8)
        return NULL;
    sk_buf b = {0};
    if (type == SK_X11_STRING)
        sk_x11_utf8_from_latin1(&b, p, n);
    else
        sk_utf8_sanitize(&b, (const char *)p, n);
    sk_buf_reserve(&b, 0);
    return b.ptr;
}

static void deliver(char *text)
{
    get_finish(text);
    free(text);
}

static void on_selection_notify(const sk_x11_event *ev)
{
    if (!ui.get_active || ui.get_incr || ev->requestor != ui.helper ||
        ev->selection != sel_atom(ui.gets.ptr[0].primary) || ev->target != ui.get_target)
        return;
    if (!ev->property) {
        if (ui.get_target == ui.atoms[A_UTF8_STRING])
            convert(SK_X11_STRING);
        else
            get_finish(NULL);
        return;
    }
    sk_x11_reply r;
    sk_x11_prop p;
    if (!sk_x11_prop_get(ui.c, true, ui.helper, ev->property, 0, MAX_GET, &r, &p)) {
        sk_x11_reply_free(&r);
        get_finish(NULL);
        return;
    }
    if (p.type == ui.atoms[A_INCR]) {
        ui.get_incr = true;
        arm_get_timer();
    } else {
        deliver(decode(p.type, p.format, p.value, p.len));
    }
    sk_x11_reply_free(&r);
}

/* A chunk of an INCR transfer came; the empty one ends it. */
static void get_chunk(void)
{
    sk_x11_reply r;
    sk_x11_prop p;
    if (!sk_x11_prop_get(ui.c, true, ui.helper, ui.atoms[A_SELECTION], 0, MAX_GET, &r, &p)) {
        sk_x11_reply_free(&r);
        get_finish(NULL);
        return;
    }
    if (p.len == 0) {
        deliver(decode(ui.get_type, 8, (const uint8_t *)ui.get_data.ptr, ui.get_data.len));
    } else if (p.format == 8) {
        ui.get_type = p.type;
        sk_buf_append(&ui.get_data, p.value, p.len);
        arm_get_timer();
    }
    sk_x11_reply_free(&r);
}

static void on_property(const sk_x11_event *ev)
{
    if (ev->mode == 1 /* deleted */)
        send_next(ev);
    else if (ui.get_active && ui.get_incr && ev->window == ui.helper &&
             ev->atom == ui.atoms[A_SELECTION])
        get_chunk();
}

void sk_xui_clipboard_get(bool primary, void (*done)(void *ud, const char *utf8), void *ud)
{
    if (!ui.c || ui.lost) {
        done(ud, NULL);
        return;
    }
    clip_get g = {primary, done, ud};
    sk_vec_push(&ui.gets, g);
    if (!ui.get_active)
        get_start();
    settle();
}

/* ---- Events ---- */

static void handle(const uint8_t *raw)
{
    sk_x11_event ev;
    if (!sk_x11_event_decode(raw, ui.c->shm_event, &ev))
        return;
    switch (ev.type) {
    case SK_X11_MAPPING_NOTIFY:
        if (ev.request != 2 /* the pointer's */)
            sk_x11_keymap_load(ui.c, &ui.keymap);
        return;
    case SK_X11_SELECTION_REQUEST:
        on_selection_request(&ev);
        return;
    case SK_X11_SELECTION_CLEAR:
        on_selection_clear(&ev);
        return;
    case SK_X11_SELECTION_NOTIFY:
        on_selection_notify(&ev);
        return;
    case SK_X11_PROPERTY_NOTIFY:
        on_property(&ev);
        return;
    default:
        break;
    }
    sk_window *w = find(ev.window);
    if (w && ev.type != ui.c->shm_event)
        window_event(w, &ev, raw);
}

static void dispatch(void)
{
    uint8_t raw[32];
    while (ui.c && !ui.lost && sk_x11_next_event(ui.c, raw))
        handle(raw);
    settle();
}
