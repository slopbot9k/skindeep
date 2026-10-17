/* The sessions dashboard's pane (decisions 0028, 0029 and 0033), as
 * app/renderer/dashboard.js and the .dashboard rules of app.css drew it
 * in place of a terminal: a head line, the sums across sessions as three
 * charts, then a header and one row per session with a sparkline and
 * the last value of each meter and each dial, and the issue board of
 * its project. It draws the view the core sends (sk_dashboard's, the
 * JSON of sk_history_view) and keeps the last, as the page kept its
 * data.
 *
 * The pane scrolls down when its rows pass its height, as overflow:
 * auto scrolls it, and never across: what passes its right edge is cut
 * off there. A click on a live row opens its session.
 *
 * A view belongs to the UI thread and calls nothing of the core's. */
#ifndef SK_APP_DASHVIEW_H
#define SK_APP_DASHVIEW_H

#include "../base/json.h"
#include "../text/text.h"

typedef struct sk_dashview sk_dashview;

/* What a view draws with. The font set and cache outlive the view. */
typedef struct {
    sk_fontset *fonts; /* DejaVu Sans Mono's stack, mono */
    sk_glyph_cache *cache;
    double scale;      /* device pixels per point: 1, 2 or between */
    /* The terminal theme's: the text's colour, as render() set the
     * pane's, and what fills the pane, #term's background. */
    sk_color fg, bg;
} sk_dashview_style;

sk_dashview *sk_dashview_new(void);
void sk_dashview_free(sk_dashview *v);

/* A view to draw, len bytes of JSON: kept, the last one dropped. False,
 * keeping the last, for anything that is not a JSON object. */
bool sk_dashview_update(sk_dashview *v, const char *json, size_t len);

/* The pane drawn into area of dst, in device pixels: area filled with
 * st->bg over what dst has there, then the view at its scroll. Before
 * the first view only the head line's title. Nothing outside area is
 * touched. The size and style laid out here are what scroll, hover and
 * hit measure with until the next draw. */
void sk_dashview_draw(sk_dashview *v, sk_image *dst, sk_rect area, const sk_dashview_style *st);

/* Scrolls dy points, down for dy > 0, within the rows; true when the
 * pane moved, which a draw shows. */
bool sk_dashview_scroll(sk_dashview *v, double dy);

/* The pointer at (x, y) points from the area's top-left, or out of the
 * pane for any point outside it. True when the drawing changed: a live
 * row under the pointer is lit, as :hover lit it. */
bool sk_dashview_hover(sk_dashview *v, double x, double y);

/* The key of the live row at (x, y) points from the area's top-left,
 * which a click opens (its tab made active, else a monitor tab), or
 * NULL. Valid until the next update. */
const char *sk_dashview_hit(sk_dashview *v, double x, double y);

/* The hover text at (x, y) points from the area's top-left, as the
 * title attributes of dashboard.js gave it, into out: a dial's name over
 * its header, the board's over its header, a row's folder whole over
 * its folder, and its issues spelled out over its board cell. False,
 * out empty, where there is none. */
bool sk_dashview_title_at(sk_dashview *v, double x, double y, sk_buf *out);

/* For the live checks, what state() of dashboard.js gave: null before
 * the first view, else { ticks, dials, rows: [{ key, label, state,
 * ended, points, last, board, boardText }], shownRows: [{ key, ended,
 * opacity }] }. */
void sk_dashview_state(const sk_dashview *v, sk_jw *w);

/* fmt of dashboard.js, appended to out: a meter's value as a cell shows
 * it, "--" for no data (NAN). key: "cost", "context" or any other. */
void sk_dashview_format(sk_buf *out, const char *key, double value);

/* The pane as the last draw laid it out, or an update since, in points
 * from the area's top-left with the pane scrolled to its top. False
 * before the first draw. */
typedef struct {
    double height;      /* all of it, the padding below the rows too */
    double scroll, max_scroll;
    size_t head_lines;  /* the head line wraps where the pane is narrow */
    double header_y;    /* the header row's top */
    double rows_y, row_h; /* row i's top is rows_y + i * row_h */
    /* The items of a row, left to right: label, folder, state, the five
     * meters, a dial each, the board. */
    size_t columns;
} sk_dashview_layout;
bool sk_dashview_layout_of(const sk_dashview *v, sk_dashview_layout *out);
/* Column i's left edge and width, in points; false past the last. */
bool sk_dashview_column(const sk_dashview *v, size_t i, double *x, double *w);

#endif
