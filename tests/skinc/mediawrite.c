#include "mediawrite.h"

#include "../../src/base/str.h"

#include <string.h>

static void be32(sk_buf *b, uint32_t v)
{
    uint8_t x[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    sk_buf_append(b, x, 4);
}

static void le16(sk_buf *b, unsigned v)
{
    uint8_t x[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    sk_buf_append(b, x, 2);
}

static void le32(sk_buf *b, uint32_t v)
{
    le16(b, v & 0xffff);
    le16(b, v >> 16);
}

static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++)
            c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
    }
    return c ^ 0xffffffffu;
}

static void chunk(sk_buf *b, const char *type, const uint8_t *data, size_t n)
{
    be32(b, (uint32_t)n);
    size_t at = b->len;
    sk_buf_append(b, type, 4);
    if (n)
        sk_buf_append(b, data, n);
    be32(b, crc32((const uint8_t *)b->ptr + at, n + 4));
}

static uint8_t paeth(int a, int b, int c)
{
    int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    return (uint8_t)(pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
}

/* Rows of len bytes, each filtered by its index % 5, onto out. */
static void filter(sk_buf *out, const uint8_t *rows, size_t len, int count, size_t bpp)
{
    const uint8_t *prior = NULL;
    for (int r = 0; r < count; r++) {
        const uint8_t *row = rows + (size_t)r * len;
        int type = r % 5;
        sk_buf_putc(out, (char)type);
        for (size_t i = 0; i < len; i++) {
            int a = i >= bpp ? row[i - bpp] : 0, b = prior ? prior[i] : 0, c = prior && i >= bpp ? prior[i - bpp] : 0;
            int pred = type == 1 ? a : type == 2 ? b : type == 3 ? (a + b) / 2 : type == 4 ? paeth(a, b, c) : 0;
            sk_buf_putc(out, (char)(uint8_t)(row[i] - pred));
        }
        prior = row;
    }
}

static const int PASSES[7][4] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4},
                                 {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}};

uint8_t *sk_test_png(int w, int h, int type, int depth, bool interlace, const uint8_t *rows, size_t stride,
                     const uint8_t *plte, size_t nplte, const uint8_t *trns, size_t ntrns, size_t *n)
{
    int channels = type == 2 ? 3 : type == 4 ? 2 : type == 6 ? 4 : 1;
    size_t bits = (size_t)channels * (size_t)depth, bpp = bits < 8 ? 1 : bits / 8;
    sk_buf raw = {0};
    if (!interlace) {
        filter(&raw, rows, ((size_t)w * bits + 7) / 8, h, bpp);
    } else {
        for (int p = 0; p < 7; p++) {
            int pw = (w - PASSES[p][0] + PASSES[p][2] - 1) / PASSES[p][2];
            int ph = (h - PASSES[p][1] + PASSES[p][3] - 1) / PASSES[p][3];
            if (pw <= 0 || ph <= 0)
                continue;
            uint8_t *sub = sk_xmalloc((size_t)pw * (size_t)ph * bpp);
            for (int y = 0; y < ph; y++)
                for (int x = 0; x < pw; x++) {
                    size_t sx = (size_t)(PASSES[p][0] + x * PASSES[p][2]), sy = (size_t)(PASSES[p][1] + y * PASSES[p][3]);
                    memcpy(sub + ((size_t)y * (size_t)pw + (size_t)x) * bpp, rows + sy * stride + sx * bpp, bpp);
                }
            filter(&raw, sub, (size_t)pw * bpp, ph, bpp);
            free(sub);
        }
    }
    /* zlib's stored blocks, then the data's Adler-32. */
    sk_buf z = {0};
    sk_buf_putc(&z, 0x78);
    sk_buf_putc(&z, 0x01);
    size_t at = 0;
    do {
        size_t k = SK_MIN(raw.len - at, (size_t)65535);
        sk_buf_putc(&z, (char)(at + k == raw.len));
        le16(&z, (unsigned)k);
        le16(&z, (unsigned)~k & 0xffff);
        if (k)
            sk_buf_append(&z, raw.ptr + at, k);
        at += k;
    } while (at < raw.len);
    uint32_t s1 = 1, s2 = 0;
    for (size_t i = 0; i < raw.len; i++) {
        s1 = (s1 + (uint8_t)raw.ptr[i]) % 65521;
        s2 = (s2 + s1) % 65521;
    }
    be32(&z, s2 << 16 | s1);
    sk_buf_free(&raw);

    sk_buf out = {0};
    sk_buf_append(&out, "\x89PNG\r\n\x1a\n", 8);
    uint8_t ihdr[13] = {0};
    for (int i = 0; i < 4; i++) {
        ihdr[i] = (uint8_t)((uint32_t)w >> (24 - 8 * i));
        ihdr[4 + i] = (uint8_t)((uint32_t)h >> (24 - 8 * i));
    }
    ihdr[8] = (uint8_t)depth;
    ihdr[9] = (uint8_t)type;
    ihdr[12] = interlace;
    chunk(&out, "IHDR", ihdr, 13);
    if (plte)
        chunk(&out, "PLTE", plte, 3 * nplte);
    if (trns)
        chunk(&out, "tRNS", trns, ntrns);
    chunk(&out, "IDAT", (const uint8_t *)z.ptr, z.len);
    chunk(&out, "IEND", NULL, 0);
    sk_buf_free(&z);
    *n = out.len;
    return (uint8_t *)out.ptr;
}

uint8_t *sk_test_png_header(uint32_t w, uint32_t h, size_t *n)
{
    sk_buf out = {0};
    sk_buf_append(&out, "\x89PNG\r\n\x1a\n", 8);
    uint8_t ihdr[13] = {(uint8_t)(w >> 24), (uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w,
                        (uint8_t)(h >> 24), (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h,
                        8, 6, 0, 0, 0};
    chunk(&out, "IHDR", ihdr, 13);
    *n = out.len;
    return (uint8_t *)out.ptr;
}

uint8_t *sk_test_wav(unsigned format, unsigned channels, uint32_t rate, unsigned bits, const void *data,
                     size_t bytes, size_t *n)
{
    sk_buf out = {0};
    sk_buf_append(&out, "RIFF", 4);
    le32(&out, (uint32_t)(4 + 8 + 16 + 8 + bytes + (bytes & 1)));
    sk_buf_append(&out, "WAVEfmt ", 8);
    le32(&out, 16);
    unsigned align = channels * bits / 8;
    le16(&out, format);
    le16(&out, channels);
    le32(&out, rate);
    le32(&out, rate * align);
    le16(&out, align);
    le16(&out, bits);
    sk_buf_append(&out, "data", 4);
    le32(&out, (uint32_t)bytes);
    if (bytes)
        sk_buf_append(&out, data, bytes);
    if (bytes & 1)
        sk_buf_putc(&out, 0);
    *n = out.len;
    return (uint8_t *)out.ptr;
}
