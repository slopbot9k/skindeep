/* skinc's decoders (decisions 0036 and 0037): a skin's images and
 * sounds, which only skinc reads, turned into what a pack holds. PNG
 * and JPEG through stb_image, SVG through nanosvg, Ogg Vorbis through
 * stb_vorbis, WAV's PCM here; QOI and IMA ADPCM out. None of the
 * decoders is linked into skinned, and each is called only on what
 * the checks before it let through.
 *
 * Messages, appended to err when it is not NULL, end a sentence about
 * the file: "is 20000 x 10 pixels: an image may be at most 16384 a
 * side and 67108864 pixels in all". */
#ifndef SKINC_MEDIA_H
#define SKINC_MEDIA_H

#include "../src/base/str.h"
#include "../src/gfx/image.h"

/* The pack loader's limits on an image, and the decoders'. */
#define SK_MEDIA_MAX_SIDE 16384
#define SK_MEDIA_MAX_PIXELS ((size_t)1 << 26)
/* An SVG file, and the paths and points it may draw. */
#define SK_MEDIA_MAX_SVG_BYTES ((size_t)1 << 20)
#define SK_MEDIA_MAX_SVG_POINTS 65536
/* A sound: its rate, the loader's, and its length. */
#define SK_MEDIA_MIN_RATE 8000
#define SK_MEDIA_MAX_RATE 48000
#define SK_MEDIA_MAX_SECONDS 300

typedef enum {
    SK_MEDIA_UNKNOWN,
    SK_MEDIA_PNG,
    SK_MEDIA_JPEG,
    SK_MEDIA_SVG,
    SK_MEDIA_OGG,
    SK_MEDIA_WAV,
} sk_media_type;

/* What the bytes are, by their first ones; an SVG by its '<' after any
 * byte order mark and spaces. */
sk_media_type sk_media_sniff(const uint8_t *data, size_t n);

/* ---- Raster images ---- */

/* A PNG or a JPEG decoded into *out, premultiplied, a JPEG turned as
 * its Exif orientation says, as browsers draw it. At most
 * SK_MEDIA_MAX_SIDE a side and max_pixels in all (0 for
 * SK_MEDIA_MAX_PIXELS), which the header is checked for before
 * anything is decoded. False, *out empty, for anything else. */
bool sk_media_image(const uint8_t *data, size_t n, size_t max_pixels, sk_image *out, sk_buf *err);

/* ---- SVG ---- */

typedef struct sk_svg sk_svg;

/* Parsed once, checked for what the rasterizer is given: a size
 * within the limits, finite coordinates, at most
 * SK_MEDIA_MAX_SVG_POINTS points, strokes and dashes that draw in
 * bounded time. NULL for anything else. */
sk_svg *sk_svg_parse(const uint8_t *data, size_t n, sk_buf *err);
void sk_svg_free(sk_svg *s);
/* Its own size in CSS pixels: its width and height, or its viewBox's. */
void sk_svg_size(const sk_svg *s, float *w, float *h);
/* Drawn into w x h CSS pixels at scale, as CSS draws an SVG image into
 * a box: scaled to fit (xMidYMid meet) and centred. *out is
 * round(w * scale) x round(h * scale) pixels, premultiplied. */
bool sk_svg_render(const sk_svg *s, float w, float h, float scale, size_t max_pixels, sk_image *out, sk_buf *err);

/* ---- Sounds ---- */

/* 16-bit mono at the file's own rate. */
typedef struct {
    int16_t *pcm;
    size_t frames;
    uint32_t rate;
} sk_sound;

/* An Ogg Vorbis or a WAV file (PCM of 8, 16, 24 or 32 bits, or 32-bit
 * float), mono or stereo, at 8 to 48 kHz, a stereo one mixed down, at
 * most max_seconds long (0 for SK_MEDIA_MAX_SECONDS). False, *out
 * empty, for anything else. */
bool sk_media_sound(const uint8_t *data, size_t n, double max_seconds, sk_sound *out, sk_buf *err);
void sk_sound_free(sk_sound *s);

/* ---- What a pack holds ---- */

/* An image as a VARI record's QOI bytes, malloc'd, their size in *n;
 * NULL for an empty one. */
uint8_t *sk_media_qoi(const sk_image *im, size_t *n);
/* A sound as an SNDS record's IMA ADPCM blocks of block_bytes
 * (audio/adpcm.h), malloc'd, their size in *n; NULL for an empty one. */
uint8_t *sk_media_adpcm(const sk_sound *s, unsigned block_bytes, size_t *n);

#endif
