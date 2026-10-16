#include "internal.h"

#include "../base/utf8.h"

#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)(p[0] << 8 | p[1]);
}

static int16_t s16(const uint8_t *p)
{
    return (int16_t)u16(p);
}

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

int sk_font_faces(const uint8_t *data, size_t n)
{
    if (!data || n < 12)
        return 0;
    int k = stbtt_GetNumberOfFonts(data);
    if (k <= 0)
        return 0;
    /* A collection's offsets must lie within it. */
    if (memcmp(data, "ttcf", 4) == 0 && (size_t)k > (n - 12) / 4)
        return 0;
    return k;
}

/* A table of the face whose directory is at start; len 0 when the face
 * has none. The directory has been checked to lie within the data. */
typedef struct {
    const uint8_t *p;
    uint32_t len;
} table;

static table find_table(const uint8_t *data, uint32_t start, const char *tag)
{
    table t = {NULL, 0};
    uint32_t count = u16(data + start + 4);
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *r = data + start + 12 + 16 * i;
        if (memcmp(r, tag, 4) == 0) {
            t.p = data + u32(r + 8);
            t.len = u32(r + 12);
            break;
        }
    }
    return t;
}

/* Whether the directory and every table it lists lie within the data. */
static bool tables_fit(const uint8_t *data, size_t n, uint32_t start)
{
    if ((uint64_t)start + 12 > n)
        return false;
    uint64_t count = u16(data + start + 4);
    if (start + 12 + 16 * count > n)
        return false;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *r = data + start + 12 + 16 * i;
        if ((uint64_t)u32(r + 8) + u32(r + 12) > n)
            return false;
    }
    return true;
}

/* The Unicode subtable to read, as an offset into the data: format 12
 * (every plane) before 4 (the BMP), and 0 or 6 in small fonts. Format
 * 13 maps whole ranges to one glyph, as the LastResort font does, and
 * 14 holds variation sequences: neither says which glyph draws a
 * character, and stb_truetype stops on 14. Microsoft's symbol encoding
 * maps to private-use codes, so it does not count. */
static uint32_t pick_cmap(const uint8_t *data, table cmap)
{
    if (cmap.len < 4)
        return 0;
    uint32_t count = u16(cmap.p + 2), best = 0;
    int best_rank = 0;
    for (uint32_t i = 0; i < count && 4 + 8 * (uint64_t)(i + 1) <= cmap.len; i++) {
        const uint8_t *r = cmap.p + 4 + 8 * i;
        unsigned plat = u16(r), enc = u16(r + 2);
        uint32_t off = u32(r + 4);
        if (!(plat == 0 || (plat == 3 && (enc == 1 || enc == 10))) || (uint64_t)off + 16 > cmap.len)
            continue;
        unsigned format = u16(cmap.p + off);
        int rank = format == 12 ? 3 : format == 4 ? 2 : format == 0 || format == 6 ? 1 : 0;
        if (rank > best_rank) {
            best_rank = rank;
            best = (uint32_t)(cmap.p - data) + off;
        }
    }
    return best;
}

/* Mac OS Roman's upper half. */
static const uint16_t mac_roman[128] = {
    0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1, 0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5,
    0x00E7, 0x00E9, 0x00E8, 0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3, 0x00F2, 0x00F4,
    0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC, 0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6,
    0x00DF, 0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8, 0x221E, 0x00B1, 0x2264, 0x2265,
    0x00A5, 0x00B5, 0x2202, 0x2211, 0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8, 0x00BF,
    0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB, 0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5,
    0x0152, 0x0153, 0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA, 0x00FF, 0x0178, 0x2044,
    0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02, 0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1,
    0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4, 0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9,
    0x0131, 0x02C6, 0x02DC, 0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7,
};

/* Appends cp to the 64-byte out when the whole of it fits. */
static bool put(char *out, size_t *len, uint32_t cp)
{
    if (cp < 0x20 || cp == 0x7f)
        return true;
    char b[4];
    size_t k = sk_utf8_encode(cp, b);
    if (*len + k > 63)
        return false;
    memcpy(out + *len, b, k);
    *len += k;
    out[*len] = 0;
    return true;
}

/* A name as UTF-8, cut at a character to fit 64 bytes, without the
 * spaces some fonts pad it with. */
static void decode_name(const uint8_t *s, uint32_t n, bool utf16, char out[64])
{
    size_t len = 0;
    out[0] = 0;
    if (!utf16) {
        for (uint32_t i = 0; i < n; i++)
            if (!put(out, &len, s[i] < 128 ? s[i] : mac_roman[s[i] - 128]))
                break;
    } else {
        for (uint32_t i = 0; i + 1 < n; i += 2) {
            uint32_t cp = u16(s + i);
            if (cp >= 0xD800 && cp < 0xDC00 && i + 3 < n) {
                uint32_t lo = u16(s + i + 2);
                if (lo >= 0xDC00 && lo < 0xE000) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    i += 2;
                }
            }
            if (!put(out, &len, cp))
                break;
        }
    }
    sk_str t = sk_str_trim(sk_str_n(out, len));
    memmove(out, t.ptr, t.len);
    out[t.len] = 0;
}

/* How well a name record suits: Windows in English, the Mac's Roman,
 * Unicode's, then Windows in another language. */
static int name_rank(unsigned plat, unsigned enc, unsigned lang)
{
    if (plat == 3 && (enc == 0 || enc == 1 || enc == 10)) {
        if (lang == 0x409)
            return 5;
        return (lang & 0x3ff) == 0x09 ? 4 : 1;
    }
    if (plat == 1 && enc == 0 && lang == 0)
        return 3;
    return plat == 0 ? 2 : 0;
}

static bool read_name(table name, unsigned id, char out[64])
{
    out[0] = 0;
    if (name.len < 6)
        return false;
    uint32_t count = u16(name.p + 2), strings = u16(name.p + 4), best_len = 0;
    const uint8_t *best = NULL;
    int best_rank = 0;
    bool utf16 = true;
    for (uint32_t i = 0; i < count && 6 + 12 * (uint64_t)(i + 1) <= name.len; i++) {
        const uint8_t *r = name.p + 6 + 12 * i;
        if (u16(r + 6) != id)
            continue;
        int rank = name_rank(u16(r), u16(r + 2), u16(r + 4));
        uint64_t off = (uint64_t)strings + u16(r + 10), len = u16(r + 8);
        if (rank > best_rank && len > 0 && off + len <= name.len) {
            best_rank = rank;
            best = name.p + off;
            best_len = (uint32_t)len;
            utf16 = u16(r) != 1;
        }
    }
    if (best)
        decode_name(best, best_len, utf16, out);
    return out[0] != 0;
}

/* Names, weight, slant and pitch. Typographic names (IDs 16 and 17)
 * group a large family's faces as CSS does; the legacy family (ID 1)
 * still matches. */
static void read_meta(sk_font *f, const uint8_t *data, uint32_t start, table head)
{
    table name = find_table(data, start, "name"), os2 = find_table(data, start, "OS/2"),
          post = find_table(data, start, "post");
    sk_font_info *m = &f->meta;
    if (read_name(name, 16, m->family)) {
        if (!read_name(name, 17, m->style))
            read_name(name, 2, m->style);
        read_name(name, 1, f->legacy_family);
    } else {
        read_name(name, 1, m->family);
        read_name(name, 2, m->style);
    }
    unsigned mac_style = u16(head.p + 44);
    m->weight = mac_style & 1 ? 700 : 400;
    m->italic = mac_style & 2;
    f->width = 5;
    if (os2.len >= 64) {
        unsigned w = u16(os2.p + 4), width = u16(os2.p + 6);
        if (w >= 1 && w <= 9) /* the scale of some old fonts */
            w *= 100;
        if (w >= 1 && w <= 1000)
            m->weight = (int)w;
        if (width >= 1 && width <= 9)
            f->width = (int)width;
        if (u16(os2.p + 62) & 0x201) /* ITALIC or OBLIQUE */
            m->italic = true;
    }
    m->mono = post.len >= 16 && u32(post.p + 12) != 0;
}

/* Browsers' line metrics: OS/2's typographic ones when its
 * USE_TYPO_METRICS bit says so, else hhea's; then whichever is set. */
static void read_vmetrics(sk_font *f, table hhea, table os2)
{
    int asc = s16(hhea.p + 4), desc = -s16(hhea.p + 6), gap = s16(hhea.p + 8);
    bool typo = os2.len >= 74 && (u16(os2.p + 62) & 0x80);
    if (os2.len >= 74 && (typo || (asc == 0 && desc == 0))) {
        asc = s16(os2.p + 68);
        desc = -s16(os2.p + 70);
        gap = s16(os2.p + 72);
    }
    if (os2.len >= 78 && asc == 0 && desc == 0) {
        asc = u16(os2.p + 74);
        desc = u16(os2.p + 76);
        gap = 0;
    }
    f->ascent = asc;
    f->descent = desc < 0 ? -desc : desc;
    f->line_gap = gap > 0 ? gap : 0;
}

sk_font *sk_font_open(const uint8_t *data, size_t n, int index)
{
    if (index < 0 || index >= sk_font_faces(data, n) || n > INT_MAX)
        return NULL;
    int start = stbtt_GetFontOffsetForIndex(data, index);
    if (start < 0 || !tables_fit(data, n, (uint32_t)start))
        return NULL;
    uint32_t at = (uint32_t)start;
    /* Colour bitmap faces (sbix, CBDT) have no outlines to draw, whatever
     * their glyf table holds. */
    if (find_table(data, at, "sbix").p || find_table(data, at, "CBDT").p)
        return NULL;
    table head = find_table(data, at, "head"), hhea = find_table(data, at, "hhea"),
          cmap = find_table(data, at, "cmap");
    if (head.len < 54 || hhea.len < 36)
        return NULL;
    int upem = u16(head.p + 18);
    uint32_t index_map = pick_cmap(data, cmap);
    if (upem < 16 || upem > 16384 || !index_map)
        return NULL;
    sk_font *f = sk_xcalloc(1, sizeof *f);
    /* stb_truetype only reads the data; it takes it without const. */
    if (!stbtt_InitFont(&f->info, (unsigned char *)(uintptr_t)data, start) || f->info.numGlyphs <= 0) {
        free(f);
        return NULL;
    }
    f->info.index_map = (int)index_map;
    f->kern = f->info;
    f->kern.gpos = 0;
    f->upem = upem;
    read_meta(f, data, at, head);
    read_vmetrics(f, hhea, find_table(data, at, "OS/2"));
    return f;
}

sk_font *sk_font_open_file(const char *path, int index)
{
    /* O_NONBLOCK, so a FIFO cannot hold the open up. */
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0)
        return NULL;
    struct stat st;
    void *map = MAP_FAILED;
    size_t n = 0;
    if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size >= 12 && st.st_size <= INT_MAX) {
        n = (size_t)st.st_size;
        map = mmap(NULL, n, PROT_READ, MAP_PRIVATE, fd, 0);
    }
    close(fd);
    if (map == MAP_FAILED)
        return NULL;
    sk_font *f = sk_font_open(map, n, index);
    if (!f) {
        munmap(map, n);
        return NULL;
    }
    f->map = map;
    f->map_size = n;
    return f;
}

void sk_font_close(sk_font *f)
{
    if (!f)
        return;
    if (f->map)
        munmap(f->map, f->map_size);
    free(f);
}

void sk_font_info_of(const sk_font *f, sk_font_info *out)
{
    *out = f->meta;
}

uint32_t sk_font_glyph(const sk_font *f, uint32_t cp)
{
    if (cp > 0x10FFFF)
        return 0;
    int g = stbtt_FindGlyphIndex(&f->info, (int)cp);
    return g > 0 && g < f->info.numGlyphs ? (uint32_t)g : 0;
}

void sk_font_vmetrics_at(const sk_font *f, float px, sk_font_vmetrics *out)
{
    float s = sk_text_scale(f, px);
    out->ascent = (float)f->ascent * s;
    out->descent = (float)f->descent * s;
    out->line_gap = (float)f->line_gap * s;
}

float sk_font_advance(const sk_font *f, uint32_t glyph, float px)
{
    int advance = 0;
    stbtt_GetGlyphHMetrics(&f->info, glyph < (uint32_t)f->info.numGlyphs ? (int)glyph : 0, &advance, NULL);
    return (float)advance * sk_text_scale(f, px);
}

float sk_font_kern(const sk_font *f, uint32_t left, uint32_t right, float px)
{
    uint32_t n = (uint32_t)f->info.numGlyphs;
    if ((!f->info.gpos && !f->info.kern) || left >= n || right >= n)
        return 0;
    int k = stbtt_GetGlyphKernAdvance(&f->info, (int)left, (int)right);
    if (k == 0 && f->info.gpos && f->info.kern)
        k = stbtt_GetGlyphKernAdvance(&f->kern, (int)left, (int)right);
    return (float)k * sk_text_scale(f, px);
}

int sk_text_synth(const sk_font *f, int style)
{
    int s = 0;
    if ((style & SK_STYLE_BOLD) && f->meta.weight < 600)
        s |= SK_STYLE_BOLD;
    if ((style & SK_STYLE_ITALIC) && !f->meta.italic)
        s |= SK_STYLE_ITALIC;
    return s;
}
