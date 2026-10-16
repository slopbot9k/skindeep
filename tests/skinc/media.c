/* skinc's decoders: the four skins' images, SVGs and sounds, PNGs and
 * WAVs made in code in each of their forms, and what is refused. */
#include "../../skinc/media.h"
#include "../../src/audio/adpcm.h"
#include "../test.h"
#include "mediawrite.h"

#include <math.h>
#include <stdlib.h>

static uint8_t *load(const char *path, size_t *n)
{
    uint8_t *d = (uint8_t *)sk_test_read(path, n);
    if (!d)
        sk_test_fail(__FILE__, __LINE__, "cannot read %s", path);
    return d;
}

/* The message an operation left, freed after the check. */
#define CHECK_SAID(buf, want)                                                                                          \
    do {                                                                                                               \
        if (!strstr(sk_buf_cstr(&(buf)), want))                                                                       \
            sk_test_fail(__FILE__, __LINE__, "\"%s\" for \"%s\"", sk_buf_cstr(&(buf)), want);                          \
        sk_buf_clear(&(buf));                                                                                          \
    } while (0)

static void test_sniff(void)
{
    CHECK_INT(sk_media_sniff((const uint8_t *)"\x89PNG\r\n\x1a\n", 8), SK_MEDIA_PNG);
    CHECK_INT(sk_media_sniff((const uint8_t *)"\x89PNG\r\n\x1a", 7), SK_MEDIA_UNKNOWN);
    CHECK_INT(sk_media_sniff((const uint8_t *)"\xff\xd8\xff\xe0", 4), SK_MEDIA_JPEG);
    CHECK_INT(sk_media_sniff((const uint8_t *)"OggS", 4), SK_MEDIA_OGG);
    CHECK_INT(sk_media_sniff((const uint8_t *)"RIFF\0\0\0\0WAVE", 12), SK_MEDIA_WAV);
    CHECK_INT(sk_media_sniff((const uint8_t *)"RIFF\0\0\0\0AVI ", 12), SK_MEDIA_UNKNOWN);
    CHECK_INT(sk_media_sniff((const uint8_t *)"\xef\xbb\xbf \n<svg", 9), SK_MEDIA_SVG);
    CHECK_INT(sk_media_sniff((const uint8_t *)"<?xml", 5), SK_MEDIA_SVG);
    CHECK_INT(sk_media_sniff((const uint8_t *)"svg", 3), SK_MEDIA_UNKNOWN);
    CHECK_INT(sk_media_sniff(NULL, 0), SK_MEDIA_UNKNOWN);
}

/* Every pixel premultiplied: no channel above its alpha. */
static bool premultiplied(const sk_image *im)
{
    for (int y = 0; y < im->h; y++)
        for (int x = 0; x < im->w; x++) {
            sk_px p = sk_image_row(im, y)[x];
            uint32_t a = p >> 24;
            if (((p >> 16) & 255) > a || ((p >> 8) & 255) > a || (p & 255) > a)
                return false;
        }
    return true;
}

/* The pack's QOI of an image decodes to the same pixels. */
static void check_qoi(const sk_image *im)
{
    size_t n = 0;
    uint8_t *q = sk_media_qoi(im, &n);
    CHECK(q != NULL);
    sk_image back;
    CHECK(q && sk_qoi_decode(q, n, SK_MEDIA_MAX_PIXELS, &back));
    CHECK_INT(back.w, im->w);
    CHECK_INT(back.h, im->h);
    bool same = back.w == im->w && back.h == im->h;
    for (int y = 0; same && y < im->h; y++)
        same = memcmp(sk_image_row(&back, y), sk_image_row(im, y), (size_t)im->w * sizeof(sk_px)) == 0;
    CHECK(same);
    sk_image_free(&back);
    free(q);
}

static void test_skin_images(void)
{
    static const struct {
        const char *path;
        int w, h;
    } images[] = {
        {"skins/coffee/images/amp.png", 568, 336},        {"skins/coffee/images/cork.png", 456, 280},
        {"skins/coffee/images/counter.jpg", 512, 512},    {"skins/coffee/images/deck.png", 568, 400},
        {"skins/coffee/images/doodle.png", 184, 232},     {"skins/coffee/images/doodles.png", 600, 56},
        {"skins/coffee/images/jar.png", 112, 200},        {"skins/coffee/images/knob.png", 192, 192},
        {"skins/coffee/images/lava.png", 136, 300},       {"skins/coffee/images/lights-off.png", 480, 60},
        {"skins/coffee/images/lights-on.png", 480, 60},   {"skins/coffee/images/lps.png", 568, 420},
        {"skins/coffee/images/mugs.png", 1860, 168},      {"skins/coffee/images/plant.png", 172, 240},
        {"skins/coffee/images/press.png", 140, 236},      {"skins/coffee/images/record.png", 280, 280},
        {"skins/coffee/images/slate.jpg", 900, 400},      {"skins/coffee/images/tonearm.png", 568, 400},
        {"skins/coffee/images/typewriter.png", 336, 192}, {"skins/coffee/images/vu.png", 480, 360},
        {"skins/coffee/images/wall.jpg", 1024, 512},      {"skins/deck/images/bevel-inlay.png", 44, 44},
        {"skins/deck/images/bevel-pressed.png", 48, 48},  {"skins/deck/images/bevel-raised.png", 48, 48},
        {"skins/deck/images/brushed-metal.png", 512, 512}, {"skins/deck/images/slider-cache.png", 160, 392},
        {"skins/deck/images/slider-context.png", 160, 392}, {"skins/library/images/books.png", 640, 128},
        {"skins/library/images/chalkboard.jpg", 900, 640}, {"skins/library/images/knob.png", 96, 96},
        {"skins/library/images/lamp.png", 280, 120},      {"skins/library/images/oak.jpg", 512, 512},
        {"skins/library/images/paneling.jpg", 512, 512},  {"skins/phosphor/images/radar.png", 2304, 96},
    };
    for (size_t i = 0; i < SK_LEN(images); i++) {
        size_t n = 0;
        uint8_t *d = load(images[i].path, &n);
        if (!d)
            continue;
        sk_image im;
        sk_buf err = {0};
        if (!sk_media_image(d, n, 0, &im, &err))
            sk_test_fail(__FILE__, __LINE__, "%s %s", images[i].path, sk_buf_cstr(&err));
        CHECK_INT(im.w, images[i].w);
        CHECK_INT(im.h, images[i].h);
        CHECK(premultiplied(&im));
        check_qoi(&im);
        sk_image_free(&im);
        sk_buf_free(&err);
        free(d);
    }
}

/* A pixel of a decoded image against the colour it should be. */
#define CHECK_PX(im, x, y, color) CHECK_INT(sk_image_row(&(im), y)[x], sk_premul(color))

static sk_image decoded(const uint8_t *png, size_t n)
{
    sk_image im;
    sk_buf err = {0};
    if (!sk_media_image(png, n, 0, &im, &err))
        sk_test_fail(__FILE__, __LINE__, "%s", sk_buf_cstr(&err));
    sk_buf_free(&err);
    return im;
}

/* PNG's colour types and depths, each row through another filter. */
static void test_png_forms(void)
{
    size_t n;
    /* RGBA, 5 x 3, through all five filters by row and Adam7. */
    uint8_t rgba[3][20];
    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 5; x++) {
            uint8_t *p = rgba[y] + 4 * x;
            p[0] = (uint8_t)(40 * x + 7), p[1] = (uint8_t)(90 * y + 3), p[2] = (uint8_t)(x * y * 11), p[3] = (uint8_t)(255 - 60 * x);
        }
    for (int il = 0; il < 2; il++) {
        uint8_t *png = sk_test_png(5, 3, 6, 8, il, rgba[0], 20, NULL, 0, NULL, 0, &n);
        sk_image im = decoded(png, n);
        CHECK_INT(im.w, 5);
        CHECK_INT(im.h, 3);
        for (int y = 0; y < 3 && im.w == 5; y++)
            for (int x = 0; x < 5; x++) {
                const uint8_t *p = rgba[y] + 4 * x;
                CHECK_PX(im, x, y, SK_RGBA(p[0], p[1], p[2], p[3]));
            }
        sk_image_free(&im);
        free(png);
    }
    /* Grey of 1 bit: 0 and 1 are black and white. */
    const uint8_t grey1[2] = {0xA0, 0x40};
    uint8_t *png = sk_test_png(3, 2, 0, 1, false, grey1, 1, NULL, 0, NULL, 0, &n);
    sk_image im = decoded(png, n);
    CHECK_PX(im, 0, 0, SK_RGB(255, 255, 255));
    CHECK_PX(im, 1, 0, SK_RGB(0, 0, 0));
    CHECK_PX(im, 2, 0, SK_RGB(255, 255, 255));
    CHECK_PX(im, 1, 1, SK_RGB(255, 255, 255));
    sk_image_free(&im);
    free(png);
    /* Grey and alpha of 16 bits: the high bytes. */
    const uint8_t ga16[8] = {0x12, 0x34, 0xFF, 0xFF, 0xAB, 0xCD, 0x80, 0x01};
    png = sk_test_png(2, 1, 4, 16, false, ga16, 8, NULL, 0, NULL, 0, &n);
    im = decoded(png, n);
    CHECK_PX(im, 0, 0, SK_RGBA(0x12, 0x12, 0x12, 0xFF));
    CHECK_PX(im, 1, 0, SK_RGBA(0xAB, 0xAB, 0xAB, 0x80));
    sk_image_free(&im);
    free(png);
    /* RGB of 16 bits, Adam7, and its tRNS key. */
    uint8_t rgb16[2][12];
    for (int i = 0; i < 24; i++)
        rgb16[i / 12][i % 12] = (uint8_t)(i * 10);
    png = sk_test_png(2, 2, 2, 16, true, rgb16[0], 12, NULL, 0, NULL, 0, &n);
    im = decoded(png, n);
    CHECK_PX(im, 1, 1, SK_RGB(180, 200, 220));
    sk_image_free(&im);
    free(png);
    const uint8_t rgb8[6] = {10, 20, 30, 40, 50, 60}, key[6] = {0, 40, 0, 50, 0, 60};
    png = sk_test_png(2, 1, 2, 8, false, rgb8, 6, NULL, 0, key, 6, &n);
    im = decoded(png, n);
    CHECK_PX(im, 0, 0, SK_RGB(10, 20, 30));
    CHECK_INT(sk_image_row(&im, 0)[1], 0);
    sk_image_free(&im);
    free(png);
    /* A palette of 2 bits with alpha from tRNS, as deck's sliders; an
     * index past the palette is opaque black, not what stb_image's
     * stack held. */
    const uint8_t plte[9] = {255, 0, 0, 0, 255, 0, 0, 0, 255}, trns[2] = {128, 0};
    const uint8_t idx[1] = {0x1B}; /* 0, 1, 2, 3 */
    png = sk_test_png(4, 1, 3, 2, false, idx, 1, plte, 3, trns, 2, &n);
    im = decoded(png, n);
    CHECK_PX(im, 0, 0, SK_RGBA(255, 0, 0, 128));
    CHECK_INT(sk_image_row(&im, 0)[1], 0);
    CHECK_PX(im, 2, 0, SK_RGB(0, 0, 255));
    CHECK_PX(im, 3, 0, SK_RGB(0, 0, 0));
    sk_image_free(&im);
    free(png);
    /* An empty IDAT before the data, as the fuzzer made one, decodes
     * as the PNG without it: stb_image never sees it. */
    png = sk_test_png(5, 3, 6, 8, false, rgba[0], 20, NULL, 0, NULL, 0, &n);
    size_t at = 8;
    while (at + 8 <= n && memcmp(png + at + 4, "IDAT", 4))
        at += 12 + ((size_t)png[at] << 24 | (size_t)png[at + 1] << 16 | (size_t)png[at + 2] << 8 | png[at + 3]);
    CHECK(at + 8 <= n);
    uint8_t *with = malloc(n + 12);
    memcpy(with, png, at);
    memcpy(with + at, "\0\0\0\0IDAT\0\0\0\0", 12);
    memcpy(with + at + 12, png + at, n - at);
    im = decoded(with, n + 12);
    sk_image plain = decoded(png, n);
    CHECK(im.w == plain.w && im.h == plain.h && !memcmp(im.px, plain.px, (size_t)im.w * (size_t)im.h * 4));
    sk_image_free(&im);
    sk_image_free(&plain);
    free(with);
    free(png);
}

/* An Exif segment saying orientation o, after a JPEG's SOI: the APP1
 * marker and length, "Exif\0\0", a TIFF header in either byte order
 * whose IFD0, at 8, holds one entry, Orientation (0x0112), a SHORT
 * whose value is at the segment's byte 28, then no next IFD. */
static uint8_t *with_orientation(const uint8_t *jpeg, size_t n, int o, bool little, size_t *out_n)
{
    static const uint8_t mm[36] = {0xFF, 0xE1, 0, 34, 'E', 'x', 'i', 'f', 0, 0, 'M', 'M', 0, 42, 0, 0, 0, 8,
                                   0, 1, 0x01, 0x12, 0, 3, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0};
    static const uint8_t ii[36] = {0xFF, 0xE1, 0, 34, 'E', 'x', 'i', 'f', 0, 0, 'I', 'I', 42, 0, 8, 0, 0, 0,
                                   1, 0, 0x12, 0x01, 3, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t *d = sk_xmalloc(n + 36);
    memcpy(d, jpeg, 2);
    memcpy(d + 2, little ? ii : mm, 36);
    d[2 + (little ? 28 : 29)] = (uint8_t)o;
    memcpy(d + 38, jpeg + 2, n - 2);
    *out_n = n + 36;
    return d;
}

/* Browsers turn a JPEG as its Exif orientation says: each of the eight
 * against the source, by the pixel each result pixel shows. */
static void test_jpeg_orientation(void)
{
    size_t n = 0;
    uint8_t *jpeg = load("skins/coffee/images/slate.jpg", &n);
    if (!jpeg)
        return;
    sk_image src = decoded(jpeg, n);
    int W = src.w, H = src.h;
    for (int o = 1; o <= 8; o++)
        for (int little = 0; little < 2; little++) {
            size_t m = 0;
            uint8_t *d = with_orientation(jpeg, n, o, little, &m);
            sk_image im = decoded(d, m);
            bool swap = o >= 5;
            CHECK_INT(im.w, swap ? H : W);
            CHECK_INT(im.h, swap ? W : H);
            bool same = im.w == (swap ? H : W);
            static const int probes[][2] = {{0, 0}, {1, 0}, {0, 1}, {17, 3}, {3, 17}};
            for (size_t k = 0; same && k < SK_LEN(probes); k++) {
                int x = probes[k][0], y = probes[k][1];
                int sx = o == 2 || o == 3 ? W - 1 - x : o == 5 || o == 6 ? y : o == 7 || o == 8 ? W - 1 - y : x;
                int sy = o == 3 || o == 4 ? H - 1 - y : o == 5 || o == 8 ? x : o == 6 || o == 7 ? H - 1 - x : y;
                same = sk_image_row(&im, y)[x] == sk_image_row(&src, sy)[sx];
            }
            if (!same)
                sk_test_fail(__FILE__, __LINE__, "orientation %d (%s)", o, little ? "II" : "MM");
            sk_image_free(&im);
            free(d);
        }
    /* An orientation past 8, or an Exif segment too short for its TIFF
     * header, is ignored; stb_image finds the next marker past it. */
    size_t m = 0;
    uint8_t *d = with_orientation(jpeg, n, 6, false, &m);
    d[2 + 29] = 9;
    sk_image im = decoded(d, m);
    CHECK_INT(im.w, W);
    sk_image_free(&im);
    d[2 + 29] = 6;
    d[2 + 3] = 12;
    im = decoded(d, m);
    CHECK_INT(im.w, W);
    sk_image_free(&im);
    free(d);
    sk_image_free(&src);
    free(jpeg);
}

static void test_image_refusals(void)
{
    sk_buf err = {0};
    sk_image im;
    size_t n = 0;
    CHECK(!sk_media_image((const uint8_t *)"GIF89a", 6, 0, &im, &err));
    CHECK_SAID(err, "is not a PNG or a JPEG");
    CHECK(im.px == NULL && im.w == 0);
    /* Sizes are refused from the header, before anything is decoded. */
    uint8_t *png = sk_test_png_header(20000, 10, &n);
    CHECK(!sk_media_image(png, n, 0, &im, &err));
    CHECK_SAID(err, "is 20000 x 10 pixels: an image may be at most 16384 a side and 67108864 pixels in all");
    free(png);
    png = sk_test_png_header(16384, 4097, &n);
    CHECK(!sk_media_image(png, n, 0, &im, &err));
    CHECK_SAID(err, "is 16384 x 4097 pixels");
    free(png);
    png = sk_test_png_header(100, 100, &n);
    CHECK(!sk_media_image(png, n, 9999, &im, &err));
    CHECK_SAID(err, "at most 16384 a side and 9999 pixels in all");
    CHECK(!sk_media_image(png, n, 0, &im, &err));
    CHECK_SAID(err, "is a PNG stb_image cannot read");
    free(png);
    /* A PNG cut short. */
    uint8_t rows[4] = {1, 2, 3, 4};
    png = sk_test_png(2, 2, 0, 8, false, rows, 2, NULL, 0, NULL, 0, &n);
    CHECK(!sk_media_image(png, n - 20, 0, &im, &err));
    CHECK_SAID(err, "is a PNG stb_image cannot read");
    free(png);
    /* A JPEG cut before its frame header; one cut in its scan decodes,
     * the rest grey, as browsers show it. */
    uint8_t *jpeg = load("skins/library/images/oak.jpg", &n);
    if (jpeg) {
        CHECK(!sk_media_image(jpeg, 120, 0, &im, &err));
        CHECK_SAID(err, "is a JPEG stb_image cannot read");
        CHECK(sk_media_image(jpeg, 1000, 0, &im, &err));
        CHECK_INT(im.w, 512);
        sk_image_free(&im);
        /* Its frame header says 512 x 512: made 40000 wide, it is
         * refused by that before stb_image sees it. */
        size_t at = 2;
        while (at + 4 < n && !(jpeg[at] == 0xFF && jpeg[at + 1] == 0xC0))
            at += 2 + (size_t)(jpeg[at + 2] << 8 | jpeg[at + 3]);
        jpeg[at + 7] = 0x9C, jpeg[at + 8] = 0x40;
        CHECK(!sk_media_image(jpeg, n, 0, &im, &err));
        CHECK_SAID(err, "is 40000 x 512 pixels");
        free(jpeg);
    }
    sk_buf_free(&err);
}

static sk_svg *parsed(const char *text)
{
    sk_buf err = {0};
    sk_svg *s = sk_svg_parse((const uint8_t *)text, strlen(text), &err);
    if (!s)
        sk_test_fail(__FILE__, __LINE__, "%s", sk_buf_cstr(&err));
    sk_buf_free(&err);
    return s;
}

static void test_skin_svgs(void)
{
    static const struct {
        const char *path;
        float w, h;
    } svgs[] = {
        {"skins/coffee/images/menu.svg", 36, 28},  {"skins/deck/images/menu.svg", 34, 20},
        {"skins/deck/images/menu-pressed.svg", 34, 20}, {"skins/library/images/menu.svg", 36, 28},
        {"skins/phosphor/images/menu.svg", 26, 26},
    };
    for (size_t i = 0; i < SK_LEN(svgs); i++) {
        size_t n = 0;
        uint8_t *d = load(svgs[i].path, &n);
        if (!d)
            continue;
        sk_buf err = {0};
        sk_svg *s = sk_svg_parse(d, n, &err);
        if (!s) {
            sk_test_fail(__FILE__, __LINE__, "%s %s", svgs[i].path, sk_buf_cstr(&err));
            free(d);
            continue;
        }
        float w, h;
        sk_svg_size(s, &w, &h);
        CHECK(w == svgs[i].w && h == svgs[i].h);
        for (int scale = 1; scale <= 2; scale++) {
            sk_image im;
            CHECK(sk_svg_render(s, w, h, (float)scale, 0, &im, &err));
            CHECK_INT(im.w, (int)w * scale);
            CHECK_INT(im.h, (int)h * scale);
            CHECK(premultiplied(&im));
            check_qoi(&im);
            sk_image_free(&im);
        }
        sk_svg_free(s);
        sk_buf_free(&err);
        free(d);
    }
    /* coffee's menu: inside its dark inner rect, (4, 4) to (32, 24), the
     * colour is the rect's, at either scale. */
    size_t n = 0;
    uint8_t *d = load("skins/coffee/images/menu.svg", &n);
    sk_svg *s = d ? sk_svg_parse(d, n, NULL) : NULL;
    if (s) {
        sk_image a, b;
        CHECK(sk_svg_render(s, 36, 28, 1, 0, &a, NULL) && sk_svg_render(s, 36, 28, 2, 0, &b, NULL));
        CHECK_INT(sk_image_row(&a, 6)[6], sk_premul(SK_RGB(0x26, 0x24, 0x22)));
        CHECK_INT(sk_image_row(&b, 12)[12], sk_premul(SK_RGB(0x26, 0x24, 0x22)));
        sk_image_free(&a);
        sk_image_free(&b);
    }
    sk_svg_free(s);
    free(d);
}

/* Drawn into a box of another shape, an SVG keeps its own and is
 * centred, as CSS draws it. */
static void test_svg_fit(void)
{
    sk_svg *s = parsed("<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10' viewBox='0 0 100 100'>"
                       "<rect width='100' height='100' fill='#ff0000'/></svg>");
    if (!s)
        return;
    sk_image im;
    CHECK(sk_svg_render(s, 30, 10, 1, 0, &im, NULL));
    CHECK_INT(im.w, 30);
    CHECK_INT(im.h, 10);
    CHECK_INT(sk_image_row(&im, 5)[9], 0);
    CHECK_INT(sk_image_row(&im, 5)[10], sk_premul(SK_RGB(255, 0, 0)));
    CHECK_INT(sk_image_row(&im, 5)[19], sk_premul(SK_RGB(255, 0, 0)));
    CHECK_INT(sk_image_row(&im, 5)[20], 0);
    sk_image_free(&im);
    CHECK(sk_svg_render(s, 10, 10, 2.5f, 0, &im, NULL));
    CHECK_INT(im.w, 25);
    CHECK_INT(sk_image_row(&im, 24)[24], sk_premul(SK_RGB(255, 0, 0)));
    sk_image_free(&im);
    sk_svg_free(s);
}

/* A style sheet's blocks as editors write them, a ';' and a space or
 * a line before the '}' or the element's end, which nanosvg's style
 * parser read a byte past (found by fuzzing; the sanitizers of make
 * debug see it), still apply. */
static void test_svg_styles(void)
{
    sk_svg *s = parsed("<svg xmlns='http://www.w3.org/2000/svg' width='4' height='4'>\n"
                       "<style>\n .a { fill: #00ff00; }\n .b { fill: #0000ff; \n</style>\n"
                       "<rect class='a' width='2' height='4'/><rect class='b' x='2' width='2' height='4' "
                       "style='opacity: 1; '/></svg>");
    if (!s)
        return;
    sk_image im;
    CHECK(sk_svg_render(s, 4, 4, 1, 0, &im, NULL));
    CHECK_INT(sk_image_row(&im, 1)[0], sk_premul(SK_RGB(0, 255, 0)));
    CHECK_INT(sk_image_row(&im, 1)[3], sk_premul(SK_RGB(0, 0, 255)));
    sk_image_free(&im);
    sk_svg_free(s);
}

static void test_svg_refusals(void)
{
    sk_buf err = {0};
    const char *nosize = "<svg xmlns='http://www.w3.org/2000/svg'></svg>";
    CHECK(sk_svg_parse((const uint8_t *)nosize, strlen(nosize), &err) == NULL);
    CHECK_SAID(err, "is an SVG of 0 x 0: it must have a size");
    const char *text = "just text";
    CHECK(sk_svg_parse((const uint8_t *)text, strlen(text), &err) == NULL);
    CHECK_SAID(err, "it must have a size");
    const char *big = "<svg xmlns='http://www.w3.org/2000/svg' width='20000' height='10'/>";
    CHECK(sk_svg_parse((const uint8_t *)big, strlen(big), &err) == NULL);
    CHECK_SAID(err, "is an SVG of 20000 x 10");
    uint8_t *huge = sk_xcalloc(SK_MEDIA_MAX_SVG_BYTES + 1, 1);
    CHECK(sk_svg_parse(huge, SK_MEDIA_MAX_SVG_BYTES + 1, &err) == NULL);
    CHECK_SAID(err, "is an SVG larger than 1048576 bytes");
    free(huge);
    /* Numbers past a float's range. */
    const char *inf = "<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'>"
                      "<path d='M0 0 L1e39 5 Z' stroke='red'/></svg>";
    CHECK(sk_svg_parse((const uint8_t *)inf, strlen(inf), &err) == NULL);
    CHECK_SAID(err, "is an SVG with a shape nanosvg cannot draw");
    /* Drawable at its size, not at any. */
    sk_svg *s = parsed("<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'>"
                       "<path d='M0 5 L10 5' stroke='red' stroke-width='1'/></svg>");
    sk_image im;
    if (s) {
        CHECK(sk_svg_render(s, 10, 10, 1, 0, &im, NULL));
        sk_image_free(&im);
        CHECK(!sk_svg_render(s, 20000, 10, 1, 0, &im, &err));
        CHECK_SAID(err, "is an SVG to be drawn at 20000 x 10 pixels");
        CHECK(!sk_svg_render(s, 0.2f, 10, 1, 0, &im, &err));
        CHECK_SAID(err, "is an SVG to be drawn at 0 x 10 pixels");
        CHECK(!sk_svg_render(s, NAN, 10, 1, 0, &im, &err));
        CHECK_SAID(err, "is an SVG to be drawn at nan x 10 pixels");
        CHECK(!sk_svg_render(s, 100, 100, 1, 50, &im, &err));
        CHECK_SAID(err, "and 50 pixels in all");
        CHECK(im.px == NULL);
        sk_svg_free(s);
    }
    /* Strokes too wide, and dashes too many, to draw in bounded time. */
    s = parsed("<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'>"
               "<path d='M0 5 L10 5' stroke='red' stroke-width='5000'/></svg>");
    if (s) {
        CHECK(!sk_svg_render(s, 10, 10, 1, 0, &im, &err));
        CHECK_SAID(err, "is an SVG too detailed to draw at 10 x 10 pixels");
        sk_svg_free(s);
    }
    s = parsed("<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'>"
               "<path d='M0 5 L1000000 5' stroke='red' stroke-dasharray='0.001'/></svg>");
    if (s) {
        CHECK(!sk_svg_render(s, 10, 10, 1, 0, &im, &err));
        CHECK_SAID(err, "too detailed to draw");
        sk_svg_free(s);
    }
    sk_buf_free(&err);
}

static sk_sound decoded_sound(const uint8_t *d, size_t n)
{
    sk_sound s;
    sk_buf err = {0};
    if (!sk_media_sound(d, n, 0, &s, &err))
        sk_test_fail(__FILE__, __LINE__, "%s", sk_buf_cstr(&err));
    sk_buf_free(&err);
    return s;
}

/* coffee's sounds: Ogg Vorbis, mono at 44.1 kHz, 0.6 to 36 s; each
 * through the pack's ADPCM and back keeps its noise 15 dB under it. */
static void test_skin_sounds(void)
{
    static const struct {
        const char *path;
        size_t frames;
    } sounds[] = {
        {"skins/coffee/sounds/beads.ogg", 246959},  {"skins/coffee/sounds/bongos.ogg", 264600},
        {"skins/coffee/sounds/grinder.ogg", 242550}, {"skins/coffee/sounds/guitar.ogg", 553455},
        {"skins/coffee/sounds/mug-1.ogg", 26460},   {"skins/coffee/sounds/mug-2.ogg", 26460},
        {"skins/coffee/sounds/murmur.ogg", 1587600}, {"skins/coffee/sounds/needle.ogg", 299880},
        {"skins/coffee/sounds/pour.ogg", 255780},   {"skins/coffee/sounds/snaps.ogg", 149940},
        {"skins/coffee/sounds/typewriter.ogg", 242550},
    };
    for (size_t i = 0; i < SK_LEN(sounds); i++) {
        size_t n = 0;
        uint8_t *d = load(sounds[i].path, &n);
        if (!d)
            continue;
        sk_sound s = decoded_sound(d, n);
        CHECK_INT(s.rate, 44100);
        CHECK_INT(s.frames, sounds[i].frames);
        size_t len = 0;
        uint8_t *blocks = sk_media_adpcm(&s, SK_ADPCM_BLOCK, &len);
        int16_t *back = sk_xmalloc(s.frames * sizeof *back);
        CHECK(blocks && sk_adpcm_decode(blocks, len, SK_ADPCM_BLOCK, s.frames, back));
        double sig = 0, noise = 0;
        for (size_t k = 0; k < s.frames; k++) {
            sig += (double)s.pcm[k] * s.pcm[k];
            noise += (double)(back[k] - s.pcm[k]) * (back[k] - s.pcm[k]);
        }
        /* 18.1 dB for typewriter's clicks to 40.5 for bongos as written:
         * IMA's step adapts slowly to a transient. */
        if (sig > 0 && 10 * log10(sig / (noise + 1)) < 15)
            sk_test_fail(__FILE__, __LINE__, "%s: %.1f dB", sounds[i].path, 10 * log10(sig / (noise + 1)));
        free(back);
        free(blocks);
        sk_sound_free(&s);
        free(d);
    }
}

static void test_wav_forms(void)
{
    size_t n = 0;
    /* Stereo of 16 bits mixed down. */
    const int16_t lr[6] = {16000, -16000, 1000, 1000, -32768, -32768};
    uint8_t *wav = sk_test_wav(1, 2, 22050, 16, lr, sizeof lr, &n);
    sk_sound s = decoded_sound(wav, n);
    CHECK_INT(s.rate, 22050);
    CHECK_INT(s.frames, 3);
    if (s.frames == 3) {
        CHECK_INT(s.pcm[0], 0);
        CHECK_INT(s.pcm[1], 1000);
        CHECK_INT(s.pcm[2], -32767);
    }
    sk_sound_free(&s);
    free(wav);
    /* 8 bits, unsigned. */
    const uint8_t u8[3] = {128, 255, 0};
    wav = sk_test_wav(1, 1, 8000, 8, u8, 3, &n);
    s = decoded_sound(wav, n);
    if (s.frames == 3) {
        CHECK_INT(s.pcm[0], 0);
        CHECK_INT(s.pcm[1], 32511);
        CHECK_INT(s.pcm[2], -32767);
    }
    sk_sound_free(&s);
    free(wav);
    /* 24 bits. */
    const uint8_t s24[6] = {0x00, 0x00, 0x40, 0x00, 0x00, 0xC0};
    wav = sk_test_wav(1, 1, 48000, 24, s24, 6, &n);
    s = decoded_sound(wav, n);
    if (s.frames == 2) {
        CHECK_INT(s.pcm[0], 16384);
        CHECK_INT(s.pcm[1], -16384);
    }
    sk_sound_free(&s);
    free(wav);
    /* 32-bit float, clamped; NaN is silence. */
    const float f32[4] = {0.5f, -2.0f, 1.0f, NAN};
    wav = sk_test_wav(3, 1, 44100, 32, f32, sizeof f32, &n);
    s = decoded_sound(wav, n);
    if (s.frames == 4) {
        CHECK_INT(s.pcm[0], 16384);
        CHECK_INT(s.pcm[1], -32767);
        CHECK_INT(s.pcm[2], 32767);
        CHECK_INT(s.pcm[3], 0);
    }
    sk_sound_free(&s);
    free(wav);
    /* 32 bits, signed. */
    const int32_t s32[2] = {INT32_MIN, 1 << 30};
    wav = sk_test_wav(1, 1, 44100, 32, s32, sizeof s32, &n);
    s = decoded_sound(wav, n);
    if (s.frames == 2) {
        CHECK_INT(s.pcm[0], -32767);
        CHECK_INT(s.pcm[1], 16384);
    }
    sk_sound_free(&s);
    free(wav);
}

static void test_sound_refusals(void)
{
    sk_buf err = {0};
    sk_sound s;
    size_t n = 0;
    CHECK(!sk_media_sound((const uint8_t *)"ID3\x04", 4, 0, &s, &err));
    CHECK_SAID(err, "is not an Ogg Vorbis or a WAV file");
    CHECK(s.pcm == NULL && s.frames == 0);
    const int16_t pcm[6] = {0};
    uint8_t *wav = sk_test_wav(1, 3, 44100, 16, pcm, sizeof pcm, &n);
    CHECK(!sk_media_sound(wav, n, 0, &s, &err));
    CHECK_SAID(err, "has 3 channels: a skin's sounds are mono or stereo");
    free(wav);
    wav = sk_test_wav(1, 1, 96000, 16, pcm, sizeof pcm, &n);
    CHECK(!sk_media_sound(wav, n, 0, &s, &err));
    CHECK_SAID(err, "is at 96000 Hz: a skin's sounds are at 8000 to 48000 Hz");
    free(wav);
    wav = sk_test_wav(0x11, 1, 44100, 4, pcm, sizeof pcm, &n);
    CHECK(!sk_media_sound(wav, n, 0, &s, &err));
    CHECK_SAID(err, "is a WAV file of format 17 and 4 bits");
    free(wav);
    wav = sk_test_wav(1, 1, 8000, 16, pcm, 0, &n);
    CHECK(!sk_media_sound(wav, n, 0, &s, &err));
    CHECK_SAID(err, "is a WAV file with no sound in it");
    CHECK(!sk_media_sound(wav, 20, 0, &s, &err));
    CHECK_SAID(err, "is a WAV file without its fmt and data chunks");
    free(wav);
    int16_t *second = sk_xcalloc(8000, sizeof *second);
    wav = sk_test_wav(1, 1, 8000, 16, second, 8000 * sizeof *second, &n);
    CHECK(!sk_media_sound(wav, n, 0.5, &s, &err));
    CHECK_SAID(err, "is longer than 0.5 seconds");
    CHECK(sk_media_sound(wav, n, 1, &s, &err));
    sk_sound_free(&s);
    free(wav);
    free(second);
    uint8_t *ogg = load("skins/coffee/sounds/mug-1.ogg", &n);
    if (ogg) {
        CHECK(!sk_media_sound(ogg, n, 0.25, &s, &err));
        CHECK_SAID(err, "is longer than 0.25 seconds");
        CHECK(!sk_media_sound(ogg, 100, 0, &s, &err));
        CHECK_SAID(err, "is not an Ogg Vorbis file whose comment header stb_vorbis can read");
        ogg[0x1C + 11] ^= 0xff; /* the identification header's channels */
        CHECK(!sk_media_sound(ogg, n, 0, &s, &err));
        sk_buf_clear(&err);
        ogg[0x1C + 11] ^= 0xff;
        /* The comment header, on the second page: a count of comments
         * past its bytes, which stb_vorbis would allocate for and then,
         * the allocation failed, read the list it never made (found by
         * fuzzing); and the vendor string's length past them. */
        size_t body = 58 + 27 + ogg[58 + 26], vendor = body + 7, count = vendor + 4 + (ogg[vendor] | ogg[vendor + 1] << 8);
        uint8_t keep[4];
        memcpy(keep, ogg + count, 4);
        memcpy(ogg + count, "\x00\x00\x00\x40", 4);
        CHECK(!sk_media_sound(ogg, n, 0, &s, &err));
        CHECK_SAID(err, "is not an Ogg Vorbis file whose comment header stb_vorbis can read");
        memcpy(ogg + count, keep, 4);
        ogg[vendor + 3] = 0x7f;
        CHECK(!sk_media_sound(ogg, n, 0, &s, &err));
        CHECK_SAID(err, "comment header");
        ogg[vendor + 3] = 0;
        CHECK(sk_media_sound(ogg, n, 0, &s, &err));
        sk_sound_free(&s);
        free(ogg);
    }
    sk_buf_free(&err);
}

const sk_test sk_suite_skinc_media[] = {
    T(test_sniff),         T(test_skin_images), T(test_png_forms),   T(test_jpeg_orientation),
    T(test_image_refusals), T(test_skin_svgs),  T(test_svg_fit),     T(test_svg_styles), T(test_svg_refusals),
    T(test_skin_sounds),   T(test_wav_forms),   T(test_sound_refusals), {0},
};
