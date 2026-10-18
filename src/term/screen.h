/* The terminal's insides, shared by term.c, which ports xterm.js 6.0.0's
 * InputHandler and the services around it, and select.c, which ports its
 * SelectionService (MIT, third_party/xterm.js). */
#ifndef SK_TERM_SCREEN_H
#define SK_TERM_SCREEN_H

#include "../base/map.h"
#include "buffer.h"
#include "parser.h"
#include "term.h"

#include <pthread.h>

/* An OSC 8 link while some line holds it, as OscLinkService keeps it:
 * each line it was printed on is a marker that moves with its buffer's
 * lines and goes when they do. */
typedef struct {
    int line;
    int buffer; /* the sk_lines id of the buffer it marks */
} sk_marker;

typedef struct {
    uint32_t id;
    char *key; /* "id;;uri" for a link with an id=, else NULL */
    char *id_param;
    char *uri;
    SK_VEC(sk_marker) lines;
} sk_link;

enum { SK_SEL_NORMAL, SK_SEL_WORD, SK_SEL_LINE, SK_SEL_COLUMN };

/* xterm.js's SelectionModel and the service's mode. */
typedef struct {
    bool all;
    int start_len;
    bool has_start, has_end;
    int sx, sy, ex, ey;
    int mode;
} sk_selection;

typedef struct {
    uintptr_t line;
    uint64_t gen;
} sk_seen;

struct sk_term {
    pthread_mutex_t mu;
    sk_term_callbacks cb;
    int cols, rows;
    sk_buffer normal, alt;
    sk_buffer *active;
    int buffer_ids;
    sk_lines_listener listen;
    bool user_scrolling;
    sk_vt_parser vt;

    /* xterm.js's _curAttrData; erase is _eraseAttrDataInternal. */
    sk_cell_data cur, erase;

    /* CoreService's modes. */
    bool insert_mode;
    bool app_cursor, app_keypad, bracketed_paste, origin, reverse_wrap, send_focus, sync_output, wraparound;
    int cursor_style; /* -1: the option's, block */
    int cursor_blink; /* -1: the option's */
    bool cursor_hidden, cursor_initialized;
    /* Options the sequences change, which no reset restores. */
    bool convert_eol, opt_cursor_blink;
    sk_mouse_protocol mouse;
    sk_mouse_encoding mouse_encoding;

    /* CharsetService: a charset maps ASCII, NULL for none. */
    const uint32_t *charset;
    const uint32_t *charsets[4];
    int glevel;

    char *title, *icon;
    sk_term_palette pal, pal_restore;
    bool focused;

    SK_VEC(sk_link) links; /* by id, ascending */
    sk_map link_keys;      /* a link's key to its id */
    uint32_t next_link;

    sk_selection sel;
    bool select_enabled;

    sk_seen *seen;
    int seen_rows;
    bool all_dirty;
    /* The cursor as last drawn: x, viewport row, hidden. */
    int seen_cursor[3];
};

static inline sk_line *sk_screen_line(const sk_buffer *b, int i)
{
    return sk_lines_get(&b->lines, i);
}

/* lines.get(i) as JavaScript reads it past the end: the ring wraps when
 * full; otherwise there is no line. */
sk_line *sk_screen_line_at(sk_buffer *b, int i);

/* The selection's part of a buffer event; select.c. */
void sk_select_trim(sk_term *t, int amount);
void sk_select_reset(sk_term *t);

#endif
