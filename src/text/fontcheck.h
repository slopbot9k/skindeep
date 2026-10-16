/* The font checker of decisions 0036 and 0037. stb_truetype trusts the
 * fonts it reads, so a skin's font reaches it only after this check,
 * which skinc runs on the skin's files and the pack loader runs again
 * on the bytes of each pack's FONT records.
 *
 * It takes a TrueType font with TrueType outlines (a glyf table) and
 * nothing else: no CFF, no collection, no bitmap glyphs. It walks the
 * table directory and every table stb_truetype or text/font.c reads
 * (head, hhea, maxp, hmtx, cmap's Unicode subtables, loca, glyf, kern,
 * GPOS's pair adjustments, name, OS/2 and post), following each
 * offset, count and search the readers do, so that no read of theirs
 * leaves the table it belongs to, and no 32-bit number it reads, as an
 * int shifted 24 places, overflows (cmap's groups, kern's pairs, which
 * also limit such a font to 32768 glyphs). Each glyph is walked as
 * stb_truetype turns it into vertices: every byte its points take
 * lies inside the glyph, each contour as stb_truetype walks it ends
 * inside the point list, a compound holds glyphs that exist, places
 * them by offsets (stb_truetype asserts on matched points), nests no
 * deeper than SK_FONTCHECK_MAX_DEPTH, never holds itself, and moves no
 * value past a short's range. The outline stb_truetype draws, its
 * curves' extremes and its components where it places them, stays
 * inside the glyph's box, but for the unit font tools round by (two
 * for a compound), so it rasterizes inside the bitmap the box sizes;
 * and a glyph whose box tops out at or below the baseline draws
 * nothing above it, where stb_truetype's rasterizer would assert.
 * Fonts made by tools that box only the points on the curve, as
 * DejaVu's are, are refused. A font that passes opens with
 * sk_font_open. */
#ifndef SK_TEXT_FONTCHECK_H
#define SK_TEXT_FONTCHECK_H

#include "../base/base.h"

/* The largest font the check takes. */
#define SK_FONTCHECK_MAX_BYTES ((size_t)64 << 20)
/* A compound of compounds of ... at most this deep. */
#define SK_FONTCHECK_MAX_DEPTH 8
/* The vertices stb_truetype may make of one glyph, compounds expanded:
 * a simple glyph's most, 65536 points and two for each contour. */
#define SK_FONTCHECK_MAX_VERTICES 131072
/* How far from the origin a glyph's box may reach, in ems, so a glyph
 * cannot ask the glyph cache for a bitmap past all reason. */
#define SK_FONTCHECK_MAX_EMS 4

/* Whether stb_truetype may read the n bytes of data. False with why in
 * err (err_len bytes, which may be 0), as the end of a sentence about
 * the file: "has CFF outlines: a skin's fonts must have TrueType
 * outlines (glyf)". */
bool sk_font_check(const uint8_t *data, size_t n, char *err, size_t err_len);

#endif
