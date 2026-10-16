/* The terminal tests' tools: a terminal that keeps its replies, the
 * screen as text, and golden screens. A golden screen is a compact dump
 * of the viewport, its attributes, links and scrollback, compared with
 * tests/golden/term/NAME.txt; SK_GOLDEN_UPDATE=1 writes it instead, once,
 * after reading what the test made. */
#ifndef SK_TEST_TERM_SCREEN_H
#define SK_TEST_TERM_SCREEN_H

#include "../../src/term/term.h"
#include "../test.h"

typedef struct {
    sk_term *t;
    sk_buf replies;
    sk_buf title;
    int bells;
} tt;

void tt_open(tt *x, int cols, int rows);
void tt_close(tt *x);
void tt_feed(tt *x, const char *s);
void tt_feedn(tt *x, const char *s, size_t n);
/* The viewport's rows, trailing blanks trimmed, joined by \n; malloc'd. */
char *tt_rows(sk_term *t);
/* The dump a golden screen holds; malloc'd. */
char *tt_dump(sk_term *t);
/* Takes the replies so far. */
char *tt_take(tt *x);

#define CHECK_ROWS(t, want)                                                  \
    do {                                                                     \
        char *r_ = tt_rows(t);                                               \
        CHECK_STR(r_, want);                                                 \
        free(r_);                                                            \
    } while (0)

#define CHECK_REPLY(x, want)                                                 \
    do {                                                                     \
        char *r_ = tt_take(x);                                               \
        CHECK_STR(r_, want);                                                 \
        free(r_);                                                            \
    } while (0)

#define CHECK_CURSOR(t, cx, cy)                                              \
    do {                                                                     \
        sk_term_cursor c_;                                                   \
        sk_term_cursor_of(t, &c_);                                           \
        CHECK_INT(c_.x, cx);                                                 \
        CHECK_INT(c_.y, cy);                                                 \
    } while (0)

#define CHECK_GOLDEN_SCREEN(name, t) tt_golden(__FILE__, __LINE__, (name), (t))
void tt_golden(const char *file, int line, const char *name, sk_term *t);

/* The cell at viewport x, y. */
sk_term_cell tt_cell(sk_term *t, int x, int y);

#endif
