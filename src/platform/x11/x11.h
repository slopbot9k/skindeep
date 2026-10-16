/* X11's wire protocol over a Unix socket, without Xlib (decision 0036):
 * the display name, the Xauthority cookie, the connection setup, requests
 * and their sequence numbers, and the replies, events and errors that come
 * back. It is plain C over a socket, so it builds, and its tests run
 * against a fake server, on every system; only Linux opens windows with
 * it (platform/linux/ui.c).
 *
 * The client speaks little-endian ('l'), whatever the machine. Image data
 * goes in the server's image byte order, which sk_x11.image_lsb gives.
 *
 * One thread uses a connection. A request that has a reply is waited for
 * before events are read again: sk_x11_read drops replies nobody waits
 * for. */
#ifndef SK_X11_H
#define SK_X11_H

#include "../../base/str.h"

/* How long a reply may take before the connection counts as lost. */
#define SK_X11_TIMEOUT_MS 10000

/* ---- Bytes on the wire ---- */

static inline uint16_t sk_x11_get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}
static inline uint32_t sk_x11_get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static inline void sk_x11_set16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static inline void sk_x11_set32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
static inline size_t sk_x11_pad4(size_t n)
{
    return (n + 3) & ~(size_t)3;
}

/* ---- The display ---- */

/* Where $DISPLAY leads. ":N[.S]" and "unix:N[.S]" are the local server N:
 * on Linux its abstract socket "@/tmp/.X11-unix/XN" first, then the file
 * /tmp/.X11-unix/XN. "/path/to/socket:N[.S]" is a socket at that path, the
 * part before the colon first, then the whole name, as XQuartz's sockets
 * are named. A host name is X over TCP, which is not spoken. */
typedef struct {
    char *sockets[2]; /* an abstract socket's name begins with '@' */
    size_t n_sockets;
    char number[16]; /* as written, for Xauthority */
    int screen;
} sk_x11_display;

bool sk_x11_display_parse(const char *name, sk_x11_display *d, char **err);
void sk_x11_display_free(sk_x11_display *d);
/* A non-blocking, close-on-exec socket connected to the first of
 * d->sockets that answers, or -1 with *err a malloc'd reason. */
int sk_x11_display_connect(const sk_x11_display *d, char **err);

/* ---- Xauthority ---- */

#define SK_X11_FAMILY_LOCAL 256
#define SK_X11_FAMILY_WILD 65535

typedef struct {
    char name[32];
    uint8_t data[256];
    size_t name_len, data_len;
} sk_x11_cookie;

/* The first MIT-MAGIC-COOKIE-1 in an Xauthority file's bytes for a local
 * display: family FamilyLocal with this host's name, or FamilyWild, and
 * the display number as written, or no number at all. */
bool sk_x11_xauth_find(const uint8_t *buf, size_t n, const char *host, const char *number,
                       sk_x11_cookie *out);
/* The same from $XAUTHORITY, else ~/.Xauthority, for this host. */
bool sk_x11_xauth_load(const char *number, sk_x11_cookie *out);

/* ---- The connection ---- */

typedef struct {
    uint8_t *data; /* the whole reply, header included, malloc'd */
    size_t len;
} sk_x11_reply;

typedef struct {
    uint8_t b[32];
} sk_x11_raw;

typedef void (*sk_x11_error_fn)(void *ud, const uint8_t err[32], uint64_t seq);

typedef struct {
    int fd;
    bool dead; /* the socket failed, closed, or the server stopped answering */

    /* The setup. */
    uint32_t release;
    char vendor[64];
    uint32_t id_base, id_mask, id_next;
    uint32_t max_request; /* bytes */
    bool image_lsb;       /* the server's image byte order is LSBFirst */
    uint8_t min_keycode, max_keycode;
    uint8_t bpp24;        /* bits per pixel of a depth-24 ZPixmap */
    /* The screen of the display name. */
    uint32_t root, root_visual, colormap, white, black;
    uint16_t root_w, root_h, root_mm_w, root_mm_h;
    uint8_t root_depth;
    /* A 24-bit TrueColor visual with masks 0xff0000, 0xff00, 0xff: the
     * root's own when it is one, else one the root allows; 0 for none. */
    uint32_t visual;

    /* Extensions, 0 when absent. */
    uint8_t big_opcode;
    uint8_t shm_opcode, shm_event;

    /* Requests out. */
    sk_buf out;
    size_t req_start; /* where the request being built begins in out */
    uint64_t seq;     /* the last request's sequence number */
    uint64_t seen;    /* the last sequence number the server reported */
    bool syncing;

    /* Bytes, events and errors in. */
    sk_buf in;
    size_t in_off; /* where the bytes not yet looked at begin */
    SK_VEC(sk_x11_raw) events;
    size_t ev_head;
    sk_x11_error_fn on_error;
    void *error_ud;
    uint64_t check_seq;
    uint8_t check_error;
} sk_x11;

/* Connects to the display name and completes the setup. NULL with *err a
 * malloc'd reason. */
sk_x11 *sk_x11_open(const char *display, char **err);
/* The setup over a connected socket, which the connection then owns; the
 * cookie may be NULL. screen: the screen to use. */
sk_x11 *sk_x11_open_fd(int fd, const sk_x11_cookie *cookie, int screen, char **err);
void sk_x11_free(sk_x11 *c);

/* The bytes of the connection setup request. */
void sk_x11_setup_request(sk_buf *out, const sk_x11_cookie *cookie);
/* The server's whole setup reply (status 1) into c, using screen.
 * false with *err a malloc'd reason. */
bool sk_x11_setup_parse(sk_x11 *c, const uint8_t *buf, size_t n, int screen, char **err);

/* A new resource id. */
uint32_t sk_x11_id(sk_x11 *c);

/* Starts a request: its header, and its sequence number in c->seq. The
 * body follows through sk_x11_put*, then sk_x11_end. */
uint64_t sk_x11_begin(sk_x11 *c, uint8_t opcode, uint8_t data);
void sk_x11_put8(sk_x11 *c, uint32_t v);
void sk_x11_put16(sk_x11 *c, uint32_t v);
void sk_x11_put32(sk_x11 *c, uint32_t v);
void sk_x11_put(sk_x11 *c, const void *p, size_t n);
/* Pads the request to four bytes and writes its length, in
 * BIG-REQUESTS's form when it is long. A request past the server's
 * maximum is a broken invariant: callers chunk. */
void sk_x11_end(sk_x11 *c);

/* Sends what is queued, reading while it writes so that neither side
 * blocks. false when the connection is lost. */
bool sk_x11_flush(sk_x11 *c);
/* Waits at most timeout_ms (0: not at all) for bytes, reads what is
 * there, and queues the events in it. -1 when the connection is lost, 1
 * when bytes came, else 0. */
int sk_x11_read(sk_x11 *c, int timeout_ms);
/* Queues the events of whatever was read but not yet looked at; true
 * when any event is queued. */
bool sk_x11_pending(sk_x11 *c);
/* Takes the next queued event. */
bool sk_x11_next_event(sk_x11 *c, uint8_t ev[32]);
/* The queued event i places ahead, without taking it, or NULL. */
const uint8_t *sk_x11_peek_event(const sk_x11 *c, size_t i);
/* Waits up to timeout_ms for an event that match accepts and takes it,
 * leaving the others queued in their order. */
bool sk_x11_wait_event(sk_x11 *c, bool (*match)(const uint8_t ev[32], void *ud), void *ud,
                       uint8_t ev[32], int timeout_ms);

/* The reply to request seq, events that come first queued. false on an
 * error, its code in *error (0 when the connection was lost instead). */
bool sk_x11_reply_wait(sk_x11 *c, uint64_t seq, sk_x11_reply *r, uint8_t *error);
void sk_x11_reply_free(sk_x11_reply *r);
/* A round trip: every request before has been handled. */
bool sk_x11_sync(sk_x11 *c);
/* A round trip, then the error request seq drew: 0 for none, -1 when the
 * connection is lost. That error does not reach on_error. */
int sk_x11_check(sk_x11 *c, uint64_t seq);

/* Interns names in one round trip. */
bool sk_x11_intern(sk_x11 *c, const char *const *names, size_t n, uint32_t *atoms);
/* BIG-REQUESTS, then MIT-SHM's presence and version 1.1 or later. */
void sk_x11_extensions(sk_x11 *c);

/* ---- Requests ---- */

/* Atoms every server predefines. */
enum {
    SK_X11_PRIMARY = 1,
    SK_X11_ATOM = 4,
    SK_X11_CARDINAL = 6,
    SK_X11_INTEGER = 19,
    SK_X11_RESOURCE_MANAGER = 23,
    SK_X11_STRING = 31,
    SK_X11_WINDOW = 33,
    SK_X11_WM_HINTS = 35,
    SK_X11_WM_CLIENT_MACHINE = 36,
    SK_X11_WM_NAME = 39,
    SK_X11_WM_NORMAL_HINTS = 40,
    SK_X11_WM_SIZE_HINTS = 41,
    SK_X11_WM_CLASS = 67,
};

/* Event codes. */
enum {
    SK_X11_KEY_PRESS = 2,
    SK_X11_KEY_RELEASE = 3,
    SK_X11_BUTTON_PRESS = 4,
    SK_X11_BUTTON_RELEASE = 5,
    SK_X11_MOTION_NOTIFY = 6,
    SK_X11_ENTER_NOTIFY = 7,
    SK_X11_LEAVE_NOTIFY = 8,
    SK_X11_FOCUS_IN = 9,
    SK_X11_FOCUS_OUT = 10,
    SK_X11_KEYMAP_NOTIFY = 11,
    SK_X11_EXPOSE = 12,
    SK_X11_DESTROY_NOTIFY = 17,
    SK_X11_UNMAP_NOTIFY = 18,
    SK_X11_MAP_NOTIFY = 19,
    SK_X11_CONFIGURE_NOTIFY = 22,
    SK_X11_PROPERTY_NOTIFY = 28,
    SK_X11_SELECTION_CLEAR = 29,
    SK_X11_SELECTION_REQUEST = 30,
    SK_X11_SELECTION_NOTIFY = 31,
    SK_X11_CLIENT_MESSAGE = 33,
    SK_X11_MAPPING_NOTIFY = 34,
    SK_X11_GENERIC_EVENT = 35,
};

/* Event masks. */
enum {
    SK_X11_KEY_PRESS_MASK = 1 << 0,
    SK_X11_KEY_RELEASE_MASK = 1 << 1,
    SK_X11_BUTTON_PRESS_MASK = 1 << 2,
    SK_X11_BUTTON_RELEASE_MASK = 1 << 3,
    SK_X11_ENTER_WINDOW_MASK = 1 << 4,
    SK_X11_LEAVE_WINDOW_MASK = 1 << 5,
    SK_X11_POINTER_MOTION_MASK = 1 << 6,
    SK_X11_EXPOSURE_MASK = 1 << 15,
    SK_X11_STRUCTURE_NOTIFY_MASK = 1 << 17,
    SK_X11_SUBSTRUCTURE_NOTIFY_MASK = 1 << 19,
    SK_X11_SUBSTRUCTURE_REDIRECT_MASK = 1 << 20,
    SK_X11_FOCUS_CHANGE_MASK = 1 << 21,
    SK_X11_PROPERTY_CHANGE_MASK = 1 << 22,
};

/* CreateWindow's and ChangeWindowAttributes's value bits. */
enum {
    SK_X11_CW_BACK_PIXMAP = 1 << 0,
    SK_X11_CW_BORDER_PIXEL = 1 << 3,
    SK_X11_CW_BIT_GRAVITY = 1 << 4,
    SK_X11_CW_OVERRIDE_REDIRECT = 1 << 9,
    SK_X11_CW_EVENT_MASK = 1 << 11,
    SK_X11_CW_COLORMAP = 1 << 13,
    SK_X11_CW_CURSOR = 1 << 14,
};

/* values: one per set bit of mask, in the order of the bits. */
uint64_t sk_x11_create_window(sk_x11 *c, uint8_t depth, uint32_t wid, uint32_t parent, int x, int y,
                              int w, int h, uint16_t cls, uint32_t visual, uint32_t mask,
                              const uint32_t *values);
uint64_t sk_x11_change_attributes(sk_x11 *c, uint32_t win, uint32_t mask, const uint32_t *values);
uint64_t sk_x11_destroy_window(sk_x11 *c, uint32_t win);
uint64_t sk_x11_map_window(sk_x11 *c, uint32_t win);
uint64_t sk_x11_unmap_window(sk_x11 *c, uint32_t win);
uint64_t sk_x11_configure_window(sk_x11 *c, uint32_t win, uint16_t mask, const uint32_t *values);
uint64_t sk_x11_intern_atom(sk_x11 *c, const char *name, bool only_if_exists);
/* mode: 0 replace, 1 prepend, 2 append; n in units of format bits. */
uint64_t sk_x11_change_property(sk_x11 *c, uint8_t mode, uint32_t win, uint32_t prop, uint32_t type,
                                uint8_t format, const void *data, uint32_t n);
uint64_t sk_x11_delete_property(sk_x11 *c, uint32_t win, uint32_t prop);
/* type 0 for any; offset and length in 32-bit units. */
uint64_t sk_x11_get_property(sk_x11 *c, bool del, uint32_t win, uint32_t prop, uint32_t type,
                             uint32_t offset, uint32_t length);
uint64_t sk_x11_set_selection_owner(sk_x11 *c, uint32_t owner, uint32_t selection, uint32_t time);
uint64_t sk_x11_convert_selection(sk_x11 *c, uint32_t requestor, uint32_t selection,
                                  uint32_t target, uint32_t property, uint32_t time);
uint64_t sk_x11_send_event(sk_x11 *c, bool propagate, uint32_t dest, uint32_t mask,
                           const uint8_t ev[32]);
uint64_t sk_x11_set_input_focus(sk_x11 *c, uint8_t revert_to, uint32_t focus, uint32_t time);
uint64_t sk_x11_get_input_focus(sk_x11 *c);
uint64_t sk_x11_open_font(sk_x11 *c, uint32_t fid, const char *name);
uint64_t sk_x11_close_font(sk_x11 *c, uint32_t fid);
uint64_t sk_x11_create_pixmap(sk_x11 *c, uint8_t depth, uint32_t pid, uint32_t drawable, int w,
                              int h);
uint64_t sk_x11_free_pixmap(sk_x11 *c, uint32_t pid);
uint64_t sk_x11_create_gc(sk_x11 *c, uint32_t gc, uint32_t drawable, uint32_t mask,
                          const uint32_t *values);
uint64_t sk_x11_free_gc(sk_x11 *c, uint32_t gc);
uint64_t sk_x11_fill_rect(sk_x11 *c, uint32_t drawable, uint32_t gc, int x, int y, int w, int h);
/* A ZPixmap of w by h 32-bit pixels at (x, y), from rows stride pixels
 * apart; swap reverses each pixel's bytes, for a server whose image byte
 * order is not this machine's. */
uint64_t sk_x11_put_image(sk_x11 *c, uint32_t drawable, uint32_t gc, int x, int y, int w, int h,
                          uint8_t depth, const uint32_t *px, size_t stride, bool swap);
uint64_t sk_x11_get_image(sk_x11 *c, uint32_t drawable, int x, int y, int w, int h);
uint64_t sk_x11_create_colormap(sk_x11 *c, uint32_t mid, uint32_t win, uint32_t visual);
uint64_t sk_x11_free_colormap(sk_x11 *c, uint32_t mid);
/* A black-on-white cursor from glyph ch of font, its mask glyph ch + 1. */
uint64_t sk_x11_create_glyph_cursor(sk_x11 *c, uint32_t cid, uint32_t font, uint16_t ch);
uint64_t sk_x11_create_cursor(sk_x11 *c, uint32_t cid, uint32_t source, uint32_t mask);
uint64_t sk_x11_free_cursor(sk_x11 *c, uint32_t cid);
uint64_t sk_x11_query_extension(sk_x11 *c, const char *name);
uint64_t sk_x11_get_keyboard_mapping(sk_x11 *c, uint8_t first, uint8_t count);
uint64_t sk_x11_get_modifier_mapping(sk_x11 *c);

/* BIG-REQUESTS and MIT-SHM. */
uint64_t sk_x11_big_enable(sk_x11 *c);
uint64_t sk_x11_shm_query_version(sk_x11 *c);
uint64_t sk_x11_shm_attach(sk_x11 *c, uint32_t seg, uint32_t shmid, bool read_only);
uint64_t sk_x11_shm_detach(sk_x11 *c, uint32_t seg);
uint64_t sk_x11_shm_put_image(sk_x11 *c, uint32_t drawable, uint32_t gc, int total_w, int total_h,
                              int src_x, int src_y, int w, int h, int dst_x, int dst_y,
                              uint8_t depth, bool send_event, uint32_t seg, uint32_t offset);

/* ---- Replies and events ---- */

/* GetProperty's reply. */
typedef struct {
    uint32_t type, bytes_after, n;
    uint8_t format;
    const uint8_t *value;
    size_t len; /* bytes */
} sk_x11_prop;

bool sk_x11_prop_parse(const sk_x11_reply *r, sk_x11_prop *p);
/* GetProperty in one round trip, at most max bytes; the reply in *r. */
bool sk_x11_prop_get(sk_x11 *c, bool del, uint32_t win, uint32_t prop, uint32_t type, size_t max,
                     sk_x11_reply *r, sk_x11_prop *p);

/* An event, its fields by name. Which are set depends on the type. */
typedef struct {
    uint8_t type; /* without SendEvent's bit */
    bool sent;
    uint8_t detail; /* keycode, button, focus detail, client message format */
    uint16_t seq;
    uint32_t time;
    uint32_t window; /* the window it is about */
    int x, y;        /* the pointer in the window; Expose's and Configure's x, y */
    int width, height, count;
    uint16_t state;
    uint8_t mode; /* Enter, Leave and Focus's mode; PropertyNotify's state */
    uint32_t atom; /* PropertyNotify's atom, ClientMessage's type */
    uint32_t owner, requestor, selection, target, property;
    uint32_t data[5]; /* a 32-bit ClientMessage's data */
    uint8_t request, first_keycode; /* MappingNotify */
    uint32_t shmseg; /* MIT-SHM's completion */
} sk_x11_event;

/* shm_event: MIT-SHM's first event, 0 when absent. false for an event
 * this does not know, with type and sent still set. */
bool sk_x11_event_decode(const uint8_t raw[32], uint8_t shm_event, sk_x11_event *ev);

/* The Xft.dpi of a RESOURCE_MANAGER string, 0 when it has none. */
double sk_x11_xft_dpi(const char *s, size_t n);

/* ---- Selections (ICCCM) ---- */

/* The most a selection's owner writes into a property at once: more goes
 * by INCR, in chunks of this size. */
size_t sk_x11_incr_threshold(const sk_x11 *c);

/* An owner's INCR transfer: a chunk each time the requestor deletes the
 * property, then an empty one. */
typedef struct {
    uint32_t requestor, property, type;
    char *data;
    size_t len, off;
    bool done; /* the empty chunk went out */
} sk_x11_incr;

/* The next chunk, at most max bytes; the empty one sets done. */
sk_str sk_x11_incr_next(sk_x11_incr *t, size_t max);

/* STRING is Latin-1: a character it cannot hold goes as '?'. */
void sk_x11_latin1_from_utf8(sk_buf *out, const char *s, size_t n);
void sk_x11_utf8_from_latin1(sk_buf *out, const uint8_t *s, size_t n);

#endif
