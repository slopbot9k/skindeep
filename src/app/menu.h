/* The app's drop-down (decision 0022), as app/renderer/menu.js builds it
 * and app.css draws #menu: a bar of the four menus at its top, the shown
 * menu's rows below, a flyout beside a row that has one, the agent
 * sessions in a box of their own that keeps its height while it
 * refreshes, and one message, an error or a note, at its foot. It opens
 * below the skin's menu button, inside the window, above the button
 * when there is no room.
 *
 * The rows are the caller's to build, and what a row does is the
 * caller's to run: a press gives back the row's action. The menu only
 * knows what a press does to itself: a row whose action waits for an
 * answer keeps the menu open until sk_menu_answer, which shows an error
 * or a note, or closes it.
 *
 * Everything is in CSS points, from the window's top-left, and drawn at
 * the window's scale. UI thread only. */
#ifndef SK_APP_MENU_H
#define SK_APP_MENU_H

#include "../base/json.h"
#include "../gfx/image.h"
#include "../platform/ui.h"
#include "../text/text.h"

/* menu.js's MENUS, in the bar's order. */
enum { SK_MENU_FILE, SK_MENU_AGENTS, SK_MENU_SKINS, SK_MENU_VIZ, SK_MENU_COUNT };
extern const char *const sk_menu_titles[SK_MENU_COUNT];

typedef enum {
    SK_ROW_ITEM,     /* text, its shortcut at the right */
    SK_ROW_HEADING,  /* small capitals, no press */
    SK_ROW_RULE,     /* an hr */
    SK_ROW_FLYOUT,   /* a row whose list opens beside the drop-down */
    SK_ROW_SESSIONS, /* the agent sessions' box, filled by sk_menu_sessions */
} sk_row_kind;

/* What a press on an item does to the menu: menu.js's then() closes it
 * at once; act() waits for the answer; a problem's row says its detail
 * as an error, with no action. */
typedef enum { SK_ROW_CLOSES, SK_ROW_WAITS, SK_ROW_SAYS } sk_row_then;

typedef struct sk_menu_row sk_menu_row;
struct sk_menu_row {
    sk_row_kind kind;
    char *text;
    char *keys;     /* an item's shortcut, or NULL */
    char *location; /* a skin's folder at the item's right, its end shown */
    bool disabled, current, problem;
    sk_row_then then;
    int action;     /* the caller's; 0 for a row that does nothing */
    char *arg;      /* the caller's, or for SK_ROW_SAYS the text said */
    sk_menu_row *sub; /* a flyout's items */
    size_t nsub;
    struct sk_menu_session *sessions; /* the sessions' box's first rows */
    size_t nsessions;
};

/* A row of the agent sessions (decision 0027), as the menu shows it:
 * the folder with ~, the state (working, waiting, idle, unknown) and
 * the age already as text. */
typedef struct sk_menu_session {
    char *key, *label, *folder, *state, *age, *where;
    bool in_tab;
} sk_menu_session;

typedef struct {
    sk_fontset *sans, *mono;
    sk_glyph_cache *cache;
    double scale;
} sk_menu_style;

typedef struct sk_menu sk_menu;

sk_menu *sk_menu_new(void);
void sk_menu_free(sk_menu *m);

bool sk_menu_open(const sk_menu *m);
/* The menu shown, or the one asked for while the menu is closed. */
int sk_menu_shown(const sk_menu *m);

/* An ask to show menu: its number. Only the newest ask's sk_menu_show
 * shows anything, and a close cancels every ask before it, as menu.js's
 * asked counter has it. */
unsigned sk_menu_ask(sk_menu *m, int menu);

/* The answer to ask: rows for menu, which the menu takes and frees,
 * placed below the button's rect (or above it), inside a window of
 * win_w by win_h whose stage starts at stage_top. False when a newer ask
 * or a close came first: the rows are freed and nothing changes. */
bool sk_menu_show(sk_menu *m, unsigned ask, int menu, sk_menu_row *rows, size_t n, const sk_menu_style *st,
                  double bx, double by, double bw, double bh, double win_w, double win_h, double stage_top);

/* The sessions' box filled again, as fillSessions has it: a session
 * still listed redrawn in its place, a new one added last, one gone
 * marked ended until the menu closes. The menu takes the rows. */
void sk_menu_sessions(sk_menu *m, sk_menu_session *rows, size_t n);
/* Whether the menu shows the sessions' box, which the caller refreshes
 * every 2 s while it does. */
bool sk_menu_has_sessions(const sk_menu *m);

void sk_menu_close(sk_menu *m);

/* The answer to a waiting row's action: an error or a note stays in the
 * drop-down, nothing closes it. Ignored once the menu closed or showed
 * another menu since the press. */
void sk_menu_answer(sk_menu *m, unsigned press, const char *error, const char *note);

/* What a press did. */
typedef enum {
    SK_MENU_PRESS_OUTSIDE, /* outside the menu: it closed, and the press goes on to what is under it */
    SK_MENU_PRESS_NOTHING, /* inside, on nothing that acts */
    SK_MENU_PRESS_SHOW,    /* a title: the caller asks for menu */
    SK_MENU_PRESS_ACTION,  /* a row: the caller runs action with arg */
    SK_MENU_PRESS_SESSION, /* an agent session: the caller opens arg, the key */
} sk_menu_press_kind;

typedef struct {
    sk_menu_press_kind kind;
    int menu;          /* SHOW */
    int action;        /* ACTION */
    const char *arg;   /* ACTION, SESSION: valid until the menu next changes */
    bool waits;        /* ACTION, SESSION: answer with sk_menu_answer(press) */
    bool closes;       /* ACTION: the menu closed */
    unsigned press;    /* the number sk_menu_answer takes */
} sk_menu_press;

/* A button 1 press at x, y. */
sk_menu_press sk_menu_mouse_down(sk_menu *m, double x, double y);
/* The pointer at x, y (or left the window: x, y negative); true when the
 * drawing changed. */
bool sk_menu_mouse_move(sk_menu *m, double x, double y);
/* The wheel over the sessions' box scrolls it, dy in lines (positive
 * down); true when it took the wheel. */
bool sk_menu_scroll(sk_menu *m, double x, double y, double dy);
/* While the menu is open it takes every key: Left and Right give the
 * menu to ask for (*show), wrapping; Escape closes it. Returns true for
 * a key it took, which is every key while open. */
bool sk_menu_key(sk_menu *m, const sk_key_event *k, int *show);

/* Draws the menu over dst. Returns the rect it covers in pixels, its
 * shadows included, or an empty one when closed. */
sk_rect sk_menu_draw(sk_menu *m, sk_image *dst, const sk_menu_style *st);
/* The rect the menu covers, shadows included, in pixels at scale. */
sk_rect sk_menu_bounds(const sk_menu *m, double scale);

/* For the live checks: { open, shown, x, y, w, h, rows: [{ kind, text,
 * keys, disabled, current, y, h }], sessions: [{ key, label, ended }],
 * message, error, flyout: [...] | null }. */
void sk_menu_state(const sk_menu *m, sk_jw *w);

/* The rows a caller builds, freed. */
void sk_menu_rows_free(sk_menu_row *rows, size_t n);
void sk_menu_sessions_free(sk_menu_session *rows, size_t n);

/* ---- The drawing the app's other drop-downs share ---- */

/* A box shadow of rect x, y, w, h (pixels) blurred by blur pixels (CSS's
 * blur radius, a Gaussian of half that deviation), in c, source-over,
 * exactly: each pixel's coverage is the Gaussian's integral over the
 * rect. */
void sk_shadow_rect(sk_image *dst, double x, double y, double w, double h, double blur, sk_color c,
                    const sk_rect *clip);
/* text in at most maxw pixels, cut with an ellipsis at its end, or with
 * at_start at its start (a path showing its end). Returns the width
 * drawn. */
float sk_draw_fit(sk_image *dst, sk_glyph_cache *cache, sk_fontset *set, int style, float px, float x, float base,
                  float maxw, const char *s, sk_color c, bool at_start, const sk_rect *clip);
/* The baseline of a line of set's text at px in a line box lh high from
 * top, as CSS centres the font's ascent and descent in it. */
float sk_baseline_in(sk_fontset *set, int style, float px, float top, float lh);

#endif
