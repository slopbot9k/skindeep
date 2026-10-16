#include "tinyfont.h"

#include "../../src/base/str.h"

#include <string.h>

void sk_test_put16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

void sk_test_put32(uint8_t *p, uint32_t v)
{
    sk_test_put16(p, v >> 16);
    sk_test_put16(p + 2, v & 0xffff);
}

static void be16(sk_buf *b, int v)
{
    uint8_t x[2];
    sk_test_put16(x, (unsigned)v & 0xffff);
    sk_buf_append(b, x, 2);
}

static void be32(sk_buf *b, uint32_t v)
{
    uint8_t x[4];
    sk_test_put32(x, v);
    sk_buf_append(b, x, 4);
}

static void bytes(sk_buf *b, const uint8_t *p, size_t n)
{
    sk_buf_append(b, p, n);
}

static void zeros(sk_buf *b, size_t n)
{
    while (n--)
        sk_buf_putc(b, 0);
}

/* Each glyph padded to an even length, as short loca needs. */
static void glyphs(sk_buf *glyf, uint16_t loca[6])
{
    /* 0: (50, 0) (50, 700) (450, 700) (450, 0), all on the curve. */
    loca[0] = 0;
    be16(glyf, 1);
    be16(glyf, 50), be16(glyf, 0), be16(glyf, 450), be16(glyf, 700);
    be16(glyf, 3); /* end of the contour */
    be16(glyf, 0); /* no instructions */
    static const uint8_t g0[] = {
        0x33, 0x11, 0x21, 0x11,        /* flags */
        50,   0x01, 0x90,              /* x: +50 short, +0, +400 long, +0 */
        0x02, 0xBC, 0xFD, 0x44,        /* y: +0, +700, +0, -700 */
    };
    bytes(glyf, g0, sizeof g0);
    sk_buf_putc(glyf, 0);
    /* 1: empty. */
    loca[1] = loca[2] = (uint16_t)(glyf->len / 2);
    /* 2: (100, 0) on, (100, 300) off, (250, 600) off, (400, 300) off,
     * (400, 0) on; then (200, 100) off, (300, 100) on, (250, 200) on. */
    be16(glyf, 2);
    be16(glyf, 100), be16(glyf, 0), be16(glyf, 400), be16(glyf, 600);
    be16(glyf, 4), be16(glyf, 7);
    be16(glyf, 2);
    static const uint8_t g2[] = {
        0xB0, 0x01,                                     /* instructions */
        0x33, 0x10, 0x1A, 1, 0x11, 0x26, 0x33, 0x27,    /* flags, one repeated */
        100,  150,  150,  200, 100, 50,                 /* x */
        0x01, 0x2C, 0x01, 0x2C, 0xFE, 0xD4, 0xFE, 0xD4, /* y, long */
        100,  100,                                      /* y, short */
    };
    bytes(glyf, g2, sizeof g2);
    /* 3: glyph 2 at (10, 0) by bytes; glyph 0 at (500, 0) by words,
     * scaled by 0x2000, a half. */
    loca[3] = (uint16_t)(glyf->len / 2);
    be16(glyf, -1);
    be16(glyf, 110), be16(glyf, 0), be16(glyf, 725), be16(glyf, 600);
    be16(glyf, 0x22), be16(glyf, 2);
    sk_buf_putc(glyf, 10), sk_buf_putc(glyf, 0);
    be16(glyf, 0x0B), be16(glyf, 0), be16(glyf, 500), be16(glyf, 0), be16(glyf, 0x2000);
    /* 4: glyph 3 turned: x' = -y, y' = x. */
    loca[4] = (uint16_t)(glyf->len / 2);
    be16(glyf, -1);
    be16(glyf, -600), be16(glyf, 110), be16(glyf, 0), be16(glyf, 725);
    be16(glyf, 0x83), be16(glyf, 3), be16(glyf, 0), be16(glyf, 0);
    be16(glyf, 0), be16(glyf, 0x4000), be16(glyf, 0xC000), be16(glyf, 0);
    loca[5] = (uint16_t)(glyf->len / 2);
}

static void cmap(sk_buf *b)
{
    /* Records, then format 4, 12, 6 and 0. */
    const uint32_t f4 = 4 + 8 * 6, f12 = f4 + 46, f6 = f12 + 40, f0 = f6 + 16;
    be16(b, 0), be16(b, 6);
    const struct {
        int plat, enc;
        uint32_t off;
    } recs[] = {{0, 1, f6}, {0, 2, f0}, {0, 3, f4}, {0, 4, f12}, {3, 1, f4}, {3, 10, f12}};
    for (size_t i = 0; i < SK_LEN(recs); i++)
        be16(b, recs[i].plat), be16(b, recs[i].enc), be32(b, recs[i].off);
    /* Format 4: space by delta, A to C through the array, 0xFFFF. */
    be16(b, 4), be16(b, 46), be16(b, 0);
    be16(b, 6), be16(b, 4), be16(b, 1), be16(b, 2);
    be16(b, 0x20), be16(b, 0x43), be16(b, 0xFFFF);
    be16(b, 0);
    be16(b, 0x20), be16(b, 0x41), be16(b, 0xFFFF);
    be16(b, -31), be16(b, 0), be16(b, 1);
    be16(b, 0), be16(b, 4), be16(b, 0);
    be16(b, 2), be16(b, 3), be16(b, 4);
    /* Format 12. */
    be16(b, 12), be16(b, 0), be32(b, 40), be32(b, 0), be32(b, 2);
    be32(b, 0x20), be32(b, 0x20), be32(b, 1);
    be32(b, 0x41), be32(b, 0x43), be32(b, 2);
    /* Format 6. */
    be16(b, 6), be16(b, 16), be16(b, 0), be16(b, 0x41), be16(b, 3);
    be16(b, 2), be16(b, 3), be16(b, 4);
    /* Format 0. */
    be16(b, 0), be16(b, 262), be16(b, 0);
    for (int i = 0; i < 256; i++)
        sk_buf_putc(b, (char)(i == 0x20 ? 1 : i >= 0x41 && i <= 0x43 ? i - 0x41 + 2 : 0));
}

static void gpos(sk_buf *b)
{
    be32(b, 0x00010000), be16(b, 0), be16(b, 0), be16(b, 10);
    /* The lookup list, then lookup 0 at 6 and lookup 1 at 38 from it. */
    be16(b, 2), be16(b, 6), be16(b, 38);
    be16(b, 2), be16(b, 0), be16(b, 1), be16(b, 8);
    /* Pair format 1: coverage at 12, the pair set at 18. */
    be16(b, 1), be16(b, 12), be16(b, 4), be16(b, 0), be16(b, 1), be16(b, 18);
    be16(b, 1), be16(b, 1), be16(b, 2);
    be16(b, 1), be16(b, 3), be16(b, -40);
    be16(b, 2), be16(b, 0), be16(b, 1), be16(b, 8);
    /* Pair format 2: coverage at 24, class definitions at 34 and 44. */
    be16(b, 2), be16(b, 24), be16(b, 4), be16(b, 0), be16(b, 34), be16(b, 44), be16(b, 2), be16(b, 2);
    be16(b, 0), be16(b, 0), be16(b, 0), be16(b, -30);
    be16(b, 2), be16(b, 1), be16(b, 3), be16(b, 4), be16(b, 0);
    be16(b, 1), be16(b, 3), be16(b, 2), be16(b, 1), be16(b, 1);
    be16(b, 2), be16(b, 1), be16(b, 2), be16(b, 2), be16(b, 1);
}

static void name(sk_buf *b)
{
    static const char family[] = "Tiny", style[] = "Regular";
    be16(b, 0), be16(b, 2), be16(b, 6 + 24);
    be16(b, 3), be16(b, 1), be16(b, 0x409), be16(b, 1), be16(b, 8), be16(b, 0);
    be16(b, 3), be16(b, 1), be16(b, 0x409), be16(b, 2), be16(b, 14), be16(b, 8);
    for (const char *s = family; *s; s++)
        be16(b, *s);
    for (const char *s = style; *s; s++)
        be16(b, *s);
}

static void os2(sk_buf *b)
{
    size_t at = b->len;
    zeros(b, 96);
    uint8_t *p = (uint8_t *)b->ptr + at;
    sk_test_put16(p, 4);
    sk_test_put16(p + 4, 400);
    sk_test_put16(p + 6, 5);
    sk_test_put16(p + 62, 0x40);
    sk_test_put16(p + 68, 800);
    sk_test_put16(p + 70, (unsigned)-200 & 0xffff);
    sk_test_put16(p + 74, 800);
    sk_test_put16(p + 76, 200);
}

static void post(sk_buf *b)
{
    be32(b, 0x00020000), be32(b, 0), be16(b, -100), be16(b, 50);
    zeros(b, 20);
    be16(b, 5), be16(b, 0), be16(b, 3), be16(b, 36), be16(b, 37), be16(b, 258);
    sk_buf_append_c(b, "\x03"
                       "Cee");
}

typedef struct {
    const char *tag;
    sk_buf b;
} table;

uint8_t *sk_test_tiny_font(size_t *n)
{
    table t[12] = {{.tag = "GPOS"}, {.tag = "OS/2"}, {.tag = "cmap"}, {.tag = "glyf"},
                   {.tag = "head"}, {.tag = "hhea"}, {.tag = "hmtx"}, {.tag = "kern"},
                   {.tag = "loca"}, {.tag = "maxp"}, {.tag = "name"}, {.tag = "post"}};
    gpos(&t[0].b);
    os2(&t[1].b);
    cmap(&t[2].b);
    uint16_t loca[6];
    glyphs(&t[3].b, loca);
    sk_buf *head = &t[4].b;
    be32(head, 0x00010000), be32(head, 0x00010000), be32(head, 0), be32(head, 0x5F0F3CF5);
    be16(head, 0x0B), be16(head, 1000);
    zeros(head, 16);
    be16(head, -600), be16(head, 0), be16(head, 725), be16(head, 725);
    be16(head, 0), be16(head, 8), be16(head, 2), be16(head, 0), be16(head, 0);
    sk_buf *hhea = &t[5].b;
    be32(hhea, 0x00010000), be16(hhea, 800), be16(hhea, -200), be16(hhea, 0), be16(hhea, 600);
    be16(hhea, -600), be16(hhea, 0), be16(hhea, 725), be16(hhea, 1), be16(hhea, 0), be16(hhea, 0);
    zeros(hhea, 10);
    be16(hhea, 3);
    sk_buf *hmtx = &t[6].b;
    be16(hmtx, 500), be16(hmtx, 50), be16(hmtx, 250), be16(hmtx, 0), be16(hmtx, 500), be16(hmtx, 100);
    be16(hmtx, 110), be16(hmtx, -600);
    sk_buf *kern = &t[7].b;
    be16(kern, 0), be16(kern, 1);
    be16(kern, 0), be16(kern, 20), be16(kern, 1), be16(kern, 1), be16(kern, 6), be16(kern, 0), be16(kern, 0);
    be16(kern, 2), be16(kern, 3), be16(kern, -50);
    for (int i = 0; i < 6; i++)
        be16(&t[8].b, loca[i]);
    sk_buf *maxp = &t[9].b;
    be32(maxp, 0x00010000), be16(maxp, 5), be16(maxp, 8), be16(maxp, 2), be16(maxp, 16), be16(maxp, 3);
    be16(maxp, 2);
    zeros(maxp, 10);
    be16(maxp, 2), be16(maxp, 2), be16(maxp, 2);
    name(&t[10].b);
    post(&t[11].b);

    sk_buf out = {0};
    be32(&out, 0x00010000), be16(&out, 12), be16(&out, 128), be16(&out, 3), be16(&out, 64);
    uint32_t at = 12 + 16 * 12;
    for (int i = 0; i < 12; i++) {
        bytes(&out, (const uint8_t *)t[i].tag, 4);
        be32(&out, 0);
        be32(&out, at);
        be32(&out, (uint32_t)t[i].b.len);
        at += ((uint32_t)t[i].b.len + 3) & ~3u;
    }
    for (int i = 0; i < 12; i++) {
        bytes(&out, (const uint8_t *)t[i].b.ptr, t[i].b.len);
        zeros(&out, ((t[i].b.len + 3) & ~(size_t)3) - t[i].b.len);
        sk_buf_free(&t[i].b);
    }
    *n = out.len;
    return (uint8_t *)out.ptr;
}

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

uint32_t sk_test_font_record(const uint8_t *data, const char *tag)
{
    unsigned count = (unsigned)(data[4] << 8 | data[5]);
    for (unsigned i = 0; i < count; i++)
        if (memcmp(data + 12 + 16 * i, tag, 4) == 0)
            return 12 + 16 * i;
    return 0;
}

uint32_t sk_test_font_table(const uint8_t *data, const char *tag, uint32_t *len)
{
    uint32_t r = sk_test_font_record(data, tag);
    if (len)
        *len = r ? u32(data + r + 12) : 0;
    return r ? u32(data + r + 8) : 0;
}
