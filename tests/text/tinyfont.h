/* A small TrueType font made in code, for the font checker's tests and
 * its fuzz target's seed: every table and form the checker follows, in
 * about 1.3 KB, with a layout the tests patch to make it wrong.
 *
 * Glyphs, 1000 units to the em:
 *   0 .notdef  a box, 4 points on the curve, (50, 0) to (450, 700)
 *   1 space    empty
 *   2 A        two contours: one of curves through points off the
 *              curve, one repeated flag; one starting off the curve;
 *              2 bytes of instructions; box (100, 0) to (400, 600)
 *   3 B        compound: A moved 10 right, .notdef at (500, 0) scaled
 *              by a half; box (110, 0) to (725, 600)
 *   4 C        compound: B turned a quarter by a 2 x 2 matrix; box
 *              (-600, 110) to (0, 725)
 * cmap: format 4 for platform 0 encoding 3 and platform 3 encoding 1
 * (one subtable), 12 for 0/4 and 3/10, 6 for 0/1, 0 for 0/2; space and
 * A to C. kern: A then B, -50. GPOS: a format 1 pair (A, B: -40) and a
 * format 2 class pair (B or C, then A: -30). name: family "Tiny",
 * style "Regular". OS/2 version 4; post version 2 with one name, "Cee".
 * Short loca. */
#ifndef SK_TESTS_TEXT_TINYFONT_H
#define SK_TESTS_TEXT_TINYFONT_H

#include "../../src/base/base.h"

/* The font, malloc'd; its size in *n. */
uint8_t *sk_test_tiny_font(size_t *n);
/* Where a table of a font starts, its length in *len (may be NULL);
 * 0 when there is none. */
uint32_t sk_test_font_table(const uint8_t *data, const char *tag, uint32_t *len);
/* Where table tag's record is in the directory, or 0. */
uint32_t sk_test_font_record(const uint8_t *data, const char *tag);
/* Big-endian stores, for patching. */
void sk_test_put16(uint8_t *p, unsigned v);
void sk_test_put32(uint8_t *p, uint32_t v);

#endif
