/* qoi2png: a QOI image (a golden image, a skin pack's) as a PNG, to look
 * at in any viewer. The PNG is stored, not compressed: deflate's stored
 * blocks need no compressor, and the file is only for looking.
 *
 *   qoi2png IN.qoi OUT.png */
#include "../src/gfx/image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t crc_table[256];

static void crc_init(void)
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++)
            c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}

static uint32_t crc(const uint8_t *p, size_t n)
{
    uint32_t c = 0xffffffffu;
    while (n--)
        c = crc_table[(c ^ *p++) & 255] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

static void be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static bool chunk(FILE *f, const char *type, const uint8_t *data, size_t n)
{
    uint8_t *body = sk_xmalloc(4 + n), len[4], sum[4];
    memcpy(body, type, 4);
    if (n)
        memcpy(body + 4, data, n);
    be32(len, (uint32_t)n);
    be32(sum, crc(body, 4 + n));
    bool ok = fwrite(len, 1, 4, f) == 4 && fwrite(body, 1, 4 + n, f) == 4 + n && fwrite(sum, 1, 4, f) == 4;
    free(body);
    return ok;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: qoi2png IN.qoi OUT.png\n");
        return 2;
    }
    FILE *in = fopen(argv[1], "rb");
    if (!in) {
        perror(argv[1]);
        return 1;
    }
    uint8_t *q = NULL;
    size_t n = 0, cap = 0;
    for (;;) {
        if (n == cap)
            q = sk_xrealloc(q, cap = cap ? cap * 2 : 65536);
        size_t got = fread(q + n, 1, cap - n, in);
        n += got;
        if (got == 0)
            break;
    }
    fclose(in);
    sk_image im;
    if (!sk_qoi_decode(q, n, (size_t)1 << 28, &im)) {
        fprintf(stderr, "%s: not a QOI image\n", argv[1]);
        return 1;
    }
    free(q);

    /* Rows of filter 0 then RGBA, in stored deflate blocks. */
    size_t row = 1 + (size_t)im.w * 4, raw_n = row * (size_t)im.h;
    uint8_t *raw = sk_xmalloc(raw_n);
    for (int y = 0; y < im.h; y++) {
        uint8_t *o = raw + row * (size_t)y;
        *o++ = 0;
        for (int x = 0; x < im.w; x++) {
            sk_color c = sk_unpremul(sk_image_row(&im, y)[x]);
            *o++ = (uint8_t)(c >> 16);
            *o++ = (uint8_t)(c >> 8);
            *o++ = (uint8_t)c;
            *o++ = (uint8_t)(c >> 24);
        }
    }
    size_t blocks = raw_n / 65535 + 1, z_n = 2 + raw_n + blocks * 5 + 4;
    uint8_t *z = sk_xmalloc(z_n), *o = z;
    *o++ = 0x78;
    *o++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t at = 0; at < raw_n;) {
        size_t len = SK_MIN(raw_n - at, (size_t)65535);
        *o++ = at + len == raw_n ? 1 : 0;
        *o++ = (uint8_t)len;
        *o++ = (uint8_t)(len >> 8);
        *o++ = (uint8_t)~len;
        *o++ = (uint8_t)(~len >> 8);
        memcpy(o, raw + at, len);
        for (size_t i = 0; i < len; i++) {
            a = (a + raw[at + i]) % 65521;
            b = (b + a) % 65521;
        }
        o += len;
        at += len;
    }
    be32(o, b << 16 | a);
    o += 4;

    crc_init();
    FILE *out = fopen(argv[2], "wb");
    if (!out) {
        perror(argv[2]);
        return 1;
    }
    uint8_t ihdr[13];
    be32(ihdr, (uint32_t)im.w);
    be32(ihdr + 4, (uint32_t)im.h);
    ihdr[8] = 8;  /* bits per channel */
    ihdr[9] = 6;  /* RGBA */
    ihdr[10] = 0; /* deflate */
    ihdr[11] = 0; /* adaptive filters */
    ihdr[12] = 0; /* no interlace */
    bool ok = fwrite("\x89PNG\r\n\x1a\n", 1, 8, out) == 8 && chunk(out, "IHDR", ihdr, sizeof ihdr) &&
              chunk(out, "IDAT", z, (size_t)(o - z)) && chunk(out, "IEND", NULL, 0);
    if (fclose(out) != 0 || !ok) {
        perror(argv[2]);
        return 1;
    }
    free(raw);
    free(z);
    sk_image_free(&im);
    return 0;
}
