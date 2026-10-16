/* Web links in the terminal's text (phase 4 of decision 0036), as
 * @xterm/addon-web-links 0.12.0 finds them for the Electron app: its URL
 * regex, matched by hand; its isUrl, which keeps a match only when the
 * URL parser gives back the scheme, user, password and host the text
 * begins with; and its LinkComputer, which joins wrapped rows and maps a
 * match back to cells. Ported from WebLinksAddon.ts and
 * WebLinkProvider.ts, (c) 2019 The xterm.js authors, MIT licence.
 *
 * Text is code points. Where the addon counts UTF-16 units, a match's
 * bounds and cells come out the same: no character the regex stops at
 * lies outside the BMP. */
#ifndef SK_TERM_LINKS_H
#define SK_TERM_LINKS_H

#include "../base/base.h"

/* The next link in s[*from..n): the next match of
 *   /(https?|HTTPS?):[/]{2}[^\s"'!*(){}|\\\^<>`]*[^\s"':,.!?{}|\\\^~\[\]`()<>]/
 * that the URL check keeps. true with [*start, *end) its bounds. *from
 * moves past every match, kept or not, as the regex's lastIndex does. */
bool sk_term_url_next(const uint32_t *s, size_t n, size_t *from, size_t *start, size_t *end);

/* The buffer, read through the caller's functions. Rows are numbered
 * from 0. */
typedef struct {
    void *ud;
    /* Row y: false when there is none, else its width in cells and
     * whether it continues the row above (isWrapped). */
    bool (*row)(void *ud, int y, int *cols, bool *wrapped);
    /* Cell x of row y: *width 0 for the right half of a wide
     * character, else 1 or 2. Returns how many code points the cell
     * holds, 0 when it is empty, and copies at most max of them to cp. */
    int (*cell)(void *ud, int y, int x, uint32_t *cp, int max, int *width);
} sk_term_link_src;

typedef struct {
    /* From cell (x0, y0) to the cell before (x1, y1) in reading order;
     * x1 is 0 on the next row when the link ends a row. xterm.js's
     * 1-based range is (x0 + 1, y0 + 1) to (x1, y1 + 1). */
    int x0, y0, x1, y1;
    char *uri; /* UTF-8 */
} sk_term_link;

typedef SK_VEC(sk_term_link) sk_term_links;

/* The links the addon gives for row y, appended to out: every link of
 * the wrapped rows around it, as far as a space or 2048 characters. The
 * caller keeps those under the pointer. */
void sk_term_links_find(const sk_term_link_src *src, int y, sk_term_links *out);
void sk_term_links_free(sk_term_links *l);

#endif
