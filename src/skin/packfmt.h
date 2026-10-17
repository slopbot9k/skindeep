/* What skinc's pack writer and the pack loader share of decision 0037's
 * file: the sizes of the header and the records, little-endian numbers,
 * and the payloads of a style's properties and of keyframes, written
 * and read by one codec (packstyle.c) so the two sides cannot disagree.
 * Internal to src/skin/ and skinc/.
 *
 * The loader reads a pack in two passes over the same code. The first
 * checks every byte and counts the memory the second needs; mem is
 * NULL then, and a list decodes into scratch, which is never read back.
 * The second allocates that much once and fills it.
 *
 * Where decision 0037 leaves a detail open, format 1.0 is this:
 *   - The header's flags are 0, and a pack has at most
 *     SK_PACK_MAX_SECTIONS sections. skinc writes them in the
 *     decision's order, each after the last, zeros between.
 *   - STRS's count is its number of strings; it ends in a NUL and is
 *     valid UTF-8 throughout. BLOB's count is 0.
 *   - STYL's and KEYF's first offset is just past the offsets, their
 *     last the section's size.
 *   - A fixed section with no records has no bytes.
 *   - clip-path's list counts points, each two lengths.
 *   - WIDG's flags, from bit 0: has on, pulse, vertical, column, run,
 *     needle meters, across.
 *   - ELEM's group and meter are those of the meter an element is or
 *     is in. An element with no layers has first layer 0.
 *   - A pack holds the styles some state table names, no others.
 *   - LAYR's records are in their elements' order, each element's
 *     layers its range of ELEM, every record in one. A layer's style
 *     is none or one its element's table gives: none for STATIC, one
 *     for STATE, either for SPRITE and NINE. skinc writes an
 *     element's by style (none first), kind, scale.
 *   - A layer's x and y are from its element's border box, or for an
 *     SVG shape or group from its svg's: such a layer is painted with
 *     nothing between it and its svg but the viewBox, and its pixels
 *     go back through the viewBox to be turned with it.
 *   - SPRITE: ox, oy are what it turns about, from x, y, as the layout
 *     gives the turn. NINE: ox, oy are the width and height of the box
 *     it was painted for, in CSS px; its slices leave a middle row and
 *     column, which repeat; it is drawn over the box's background in
 *     place of its box shadows and border.
 *   - A layer's pictures may share their bytes in BLOB. */
#ifndef SK_SKIN_PACKFMT_H
#define SK_SKIN_PACKFMT_H

#include "../base/str.h"
#include "pack.h"

#include <string.h>

#define SK_PACK_HEADER_SIZE 64u
#define SK_PACK_ENTRY_SIZE 16u /* a section table's entry */

/* The records of format 1.0, in bytes. A newer minor's may be larger. */
#define SK_REC_META 216u
#define SK_REC_SPLT 16u
#define SK_REC_SPAN 4u
#define SK_REC_RECT 32u
#define SK_REC_WIDG 128u
#define SK_REC_MARK 12u
#define SK_REC_ELEM 52u
#define SK_REC_SVGG 28u
#define SK_REC_INPT 8u
#define SK_REC_STAB 4u
#define SK_REC_IMGS 16u
#define SK_REC_VARI 16u
#define SK_REC_LAYR 48u
#define SK_REC_FONT 16u
#define SK_REC_SNDS 36u

/* The faces a style's font indexes before the pack's own: the embedded
 * DejaVu of text/text.h. */
#define SK_PACK_EMBEDDED_FACES 6u

/* ---- Numbers ---- */

static inline uint16_t sk_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static inline uint32_t sk_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static inline uint64_t sk_le64(const uint8_t *p)
{
    return (uint64_t)sk_le32(p) | (uint64_t)sk_le32(p + 4) << 32;
}

static inline float sk_lef32(const uint8_t *p)
{
    uint32_t u = sk_le32(p);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static inline void sk_put8(sk_buf *b, unsigned v)
{
    sk_buf_putc(b, (char)(uint8_t)v);
}

static inline void sk_put16(sk_buf *b, unsigned v)
{
    sk_put8(b, v & 0xff);
    sk_put8(b, (v >> 8) & 0xff);
}

static inline void sk_put32(sk_buf *b, uint32_t v)
{
    sk_put16(b, v & 0xffff);
    sk_put16(b, v >> 16);
}

static inline void sk_put64(sk_buf *b, uint64_t v)
{
    sk_put32(b, (uint32_t)v);
    sk_put32(b, (uint32_t)(v >> 32));
}

static inline void sk_putf32(sk_buf *b, float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    sk_put32(b, u);
}

/* ---- Writing payloads ---- */

/* A string's offset in the pack's STRS, which the writer keeps;
 * SK_PACK_NONE for NULL. */
typedef uint32_t (*sk_pack_intern)(void *data, const char *s);

/* Appends property id's payload as s holds it. False, appending
 * nothing, for a property a pack never holds (counter-reset,
 * counter-increment, contain, flex-wrap, user-select) or an id that is
 * none. */
bool sk_pack_put_prop(sk_buf *out, uint16_t id, const sk_css_style *s, sk_pack_intern intern, void *data);
/* Appends a keyframe: its offset, what it sets, its timing and its
 * transform list. */
void sk_pack_put_key(sk_buf *out, const sk_pack_key *k);

/* ---- Reading ---- */

/* The most bytes one list decodes into. */
#define SK_PACK_SCRATCH (SK_PACK_MAX_LIST * 64u)

typedef struct {
    const uint8_t *data; /* the pack */
    size_t n;
    const char *strs; /* STRS, which ends in a NUL and is valid UTF-8 */
    uint32_t nstrs;
    uint32_t nimages, nkeyframes, nfaces;
    /* NULL in the first pass, which counts in used what the second
     * takes from mem. */
    uint8_t *mem;
    uint64_t used, cap;
    uint8_t *scratch; /* SK_PACK_SCRATCH bytes */
    /* The record being read, which a message names. */
    const char *sect;
    uint32_t rec;
    bool has_rec;
    char *err;
    size_t err_len;
    bool failed;
} sk_packfmt;

/* Says why the pack fails, "SECT rec: what", once; false. */
bool sk_packfmt_fail(sk_packfmt *c, const char *fmt, ...) SK_PRINTF(2, 3);
/* The string at off, or NULL for SK_PACK_NONE when none_ok. */
bool sk_packfmt_str(sk_packfmt *c, uint32_t off, bool none_ok, const char *what, const char **out);
/* count items of size bytes: a fixed array, NULL in the first pass, or
 * a list of at most SK_PACK_MAX_LIST, scratch in the first pass. NULL
 * for none. */
void *sk_packfmt_fixed(sk_packfmt *c, size_t count, size_t size);
void *sk_packfmt_list(sk_packfmt *c, size_t count, size_t size);

/* One style of STYL, n bytes at p: its property count and properties. */
bool sk_packfmt_style(sk_packfmt *c, const uint8_t *p, size_t n, sk_css_style *out);
/* One track of KEYF, n bytes at p. */
bool sk_packfmt_track(sk_packfmt *c, const uint8_t *p, size_t n, sk_pack_keyframes *out);

/* For the writer: whether a payload it made is one the loader takes,
 * with the strings it has pooled so far (strs, n bytes, each string
 * ending in a NUL) and the counts the pack will have. False with why in
 * err. */
bool sk_pack_check_prop(uint16_t id, const uint8_t *payload, size_t n, const char *strs, size_t nstrs,
                        uint32_t nimages, uint32_t nkeyframes, uint32_t nfaces, char *err, size_t err_len);

#endif
