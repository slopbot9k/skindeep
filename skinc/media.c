#include "media.h"

#include "../src/audio/adpcm.h"
#include "media_config.h"

#include "../third_party/stb/stb_image.h"
#define STB_VORBIS_HEADER_ONLY
#include "../third_party/stb/stb_vorbis.c"

#include "../third_party/nanosvg/nanosvg.h"
#include "../third_party/nanosvg/nanosvgrast.h"

#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>

static bool say(sk_buf *err, const char *fmt, ...) SK_PRINTF(2, 3);

static bool say(sk_buf *err, const char *fmt, ...)
{
    if (err) {
        va_list ap;
        va_start(ap, fmt);
        sk_buf_vprintf(err, fmt, ap);
        va_end(ap);
    }
    return false;
}

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

sk_media_type sk_media_sniff(const uint8_t *data, size_t n)
{
    static const uint8_t png[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    if (n >= 8 && memcmp(data, png, 8) == 0)
        return SK_MEDIA_PNG;
    if (n >= 3 && data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff)
        return SK_MEDIA_JPEG;
    if (n >= 4 && memcmp(data, "OggS", 4) == 0)
        return SK_MEDIA_OGG;
    if (n >= 12 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "WAVE", 4) == 0)
        return SK_MEDIA_WAV;
    size_t i = n >= 3 && data[0] == 0xef && data[1] == 0xbb && data[2] == 0xbf ? 3 : 0;
    while (i < n && (data[i] == ' ' || data[i] == '\t' || data[i] == '\r' || data[i] == '\n'))
        i++;
    return i < n && data[i] == '<' ? SK_MEDIA_SVG : SK_MEDIA_UNKNOWN;
}

/* ---- Raster images ---- */

static bool too_large(sk_buf *err, long long w, long long h, size_t max_pixels)
{
    return say(err, "is %lld x %lld pixels: an image may be at most %d a side and %zu pixels in all", w, h,
               SK_MEDIA_MAX_SIDE, max_pixels);
}

static bool fits(long long w, long long h, size_t max_pixels)
{
    return w > 0 && h > 0 && w <= SK_MEDIA_MAX_SIDE && h <= SK_MEDIA_MAX_SIDE &&
           (unsigned long long)w * (unsigned long long)h <= max_pixels;
}

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* The next segment of a JPEG before its scan, from *at: its marker and
 * its data, walked by their lengths. False at the scan, the end, or a
 * length that runs past the data. */
static bool jpeg_segment(const uint8_t *d, size_t n, size_t *at, uint8_t *marker, const uint8_t **seg, size_t *len)
{
    while (*at + 4 <= n && d[*at] == 0xff) {
        uint8_t m = d[*at + 1];
        if (m == 0xff) {
            (*at)++;
            continue;
        }
        if (m == 0xd8 || m == 0x01 || (m >= 0xd0 && m <= 0xd7)) {
            *at += 2;
            continue;
        }
        if (m == 0xda || m == 0xd9)
            return false;
        size_t l = (size_t)(d[*at + 2] << 8 | d[*at + 3]);
        if (l < 2 || *at + 2 + l > n)
            return false;
        *marker = m;
        *seg = d + *at + 4;
        *len = l - 2;
        *at += 2 + l;
        return true;
    }
    return false;
}

/* A PNG as stb_image can be given it, or NULL when it needs no change:
 * - each PLTE chunk of fewer than 256 entries made 256, the new ones
 *   opaque black. stb_image keeps its palette on the stack,
 *   uninitialized past the entries the file gives, and reads an index
 *   past them from there;
 * - each empty IDAT chunk dropped. Before any data, stb_image adds its
 *   offset to its still-null buffer for one (undefined in C), and an
 *   empty chunk adds nothing to the image.
 * It checks no CRC, so the new chunks keep the old ones' places and
 * carry none. */
static uint8_t *png_fixed(const uint8_t *d, size_t n, size_t *out_n)
{
    sk_buf out = {0};
    size_t done = 0;
    for (size_t at = 8; at + 8 <= n;) {
        size_t len = be32(d + at);
        /* An empty IDAT goes even when the file ends before its CRC:
         * stb_image reads its length and type all the same. */
        if (!memcmp(d + at + 4, "IDAT", 4) && len == 0) {
            sk_buf_append(&out, d + done, at - done);
            done = SK_MIN(at + 12, n);
            at += 12;
            continue;
        }
        if (at + 12 > n || len > n - at - 12)
            break;
        if (!memcmp(d + at + 4, "PLTE", 4) && len < 768 && len % 3 == 0) {
            static const uint8_t head[8] = {0, 0, 3, 0, 'P', 'L', 'T', 'E'};
            sk_buf_append(&out, d + done, at - done);
            sk_buf_append(&out, head, 8);
            if (len)
                sk_buf_append(&out, d + at + 8, len);
            for (size_t i = len; i < 768 + 4; i++)
                sk_buf_putc(&out, 0);
            done = at + 12 + len;
        }
        at += 12 + len;
    }
    if (!out.len)
        return NULL;
    sk_buf_append(&out, d + done, n - done);
    *out_n = out.len;
    return (uint8_t *)out.ptr;
}

/* The size a PNG's IHDR or a JPEG's frame header gives, read here so a
 * size past the limits is refused before stb_image allocates for it;
 * false when neither is found, which stb_image then reports. */
static bool header_size(const uint8_t *d, size_t n, sk_media_type type, long long *w, long long *h)
{
    if (type == SK_MEDIA_PNG) {
        if (n < 24 || memcmp(d + 12, "IHDR", 4) != 0)
            return false;
        *w = be32(d + 16);
        *h = be32(d + 20);
        return true;
    }
    size_t at = 2, len;
    uint8_t m;
    const uint8_t *seg;
    while (jpeg_segment(d, n, &at, &m, &seg, &len))
        if (m >= 0xc0 && m <= 0xcf && m != 0xc4 && m != 0xc8 && m != 0xcc && len >= 5) {
            *h = seg[1] << 8 | seg[2];
            *w = seg[3] << 8 | seg[4];
            return true;
        }
    return false;
}

/* The orientation of a JPEG's Exif (APP1) segment, 1 to 8, or 1 when
 * it has none: the TIFF header and IFD0 inside the segment, every
 * offset within it. */
static int exif_orientation(const uint8_t *d, size_t n)
{
    size_t at = 2, len;
    uint8_t m;
    const uint8_t *seg;
    while (jpeg_segment(d, n, &at, &m, &seg, &len)) {
        if (m != 0xe1 || len < 14 || memcmp(seg, "Exif\0\0", 6) != 0)
            continue;
        const uint8_t *t = seg + 6;
        size_t tn = len - 6;
        bool le = t[0] == 'I' && t[1] == 'I';
        if (!le && !(t[0] == 'M' && t[1] == 'M'))
            return 1;
#define T16(p) (le ? (uint32_t)le16(p) : (uint32_t)((p)[0] << 8 | (p)[1]))
#define T32(p) (le ? le32(p) : be32(p))
        if (T16(t + 2) != 42)
            return 1;
        uint64_t ifd = T32(t + 4);
        if (ifd + 2 > tn)
            return 1;
        uint32_t count = T16(t + ifd);
        for (uint32_t i = 0; i < count && ifd + 2 + 12 * (uint64_t)(i + 1) <= tn; i++) {
            const uint8_t *e = t + ifd + 2 + 12 * i;
            if (T16(e) == 0x0112 && T16(e + 2) == 3 && T32(e + 4) == 1) {
                uint32_t o = T16(e + 8);
                return o >= 1 && o <= 8 ? (int)o : 1;
            }
        }
#undef T16
#undef T32
        return 1;
    }
    return 1;
}

/* rgba, w x h, into *out premultiplied and turned as Exif orientation
 * o says: each pixel of the result from the source pixel it shows. */
static void orient(const uint8_t *rgba, int w, int h, int o, sk_image *out)
{
    bool swap = o >= 5;
    sk_image_init(out, swap ? h : w, swap ? w : h);
    for (int y = 0; y < out->h; y++) {
        sk_px *row = sk_image_row(out, y);
        for (int x = 0; x < out->w; x++) {
            int sx = x, sy = y;
            switch (o) {
            case 2:
                sx = w - 1 - x;
                break;
            case 3:
                sx = w - 1 - x;
                sy = h - 1 - y;
                break;
            case 4:
                sy = h - 1 - y;
                break;
            case 5:
                sx = y;
                sy = x;
                break;
            case 6:
                sx = y;
                sy = h - 1 - x;
                break;
            case 7:
                sx = w - 1 - y;
                sy = h - 1 - x;
                break;
            case 8:
                sx = w - 1 - y;
                sy = x;
                break;
            default:
                break;
            }
            const uint8_t *p = rgba + 4 * ((size_t)sy * (size_t)w + (size_t)sx);
            row[x] = sk_premul(SK_RGBA(p[0], p[1], p[2], p[3]));
        }
    }
}

static bool decode(const uint8_t *data, size_t n, sk_media_type type, size_t max_pixels, sk_image *out,
                   sk_buf *err)
{
    const char *kind = type == SK_MEDIA_PNG ? "PNG" : "JPEG";
    if (n > INT_MAX)
        return say(err, "is a %s larger than stb_image reads", kind);
    long long hw, hh;
    if (header_size(data, n, type, &hw, &hh) && !fits(hw, hh, max_pixels))
        return too_large(err, hw, hh, max_pixels);
    int w = 0, h = 0, comp = 0;
    if (!stbi_info_from_memory(data, (int)n, &w, &h, &comp))
        return say(err, "is a %s stb_image cannot read: %s", kind, stbi_failure_reason());
    if (!fits(w, h, max_pixels))
        return too_large(err, w, h, max_pixels);
    int dw = 0, dh = 0;
    stbi_uc *rgba = stbi_load_from_memory(data, (int)n, &dw, &dh, &comp, 4);
    if (!rgba)
        return say(err, "is a %s stb_image cannot read: %s", kind, stbi_failure_reason());
    if (dw != w || dh != h) {
        stbi_image_free(rgba);
        return say(err, "is a %s whose header and image disagree on its size", kind);
    }
    orient(rgba, w, h, type == SK_MEDIA_JPEG ? exif_orientation(data, n) : 1, out);
    stbi_image_free(rgba);
    return true;
}

bool sk_media_image(const uint8_t *data, size_t n, size_t max_pixels, sk_image *out, sk_buf *err)
{
    memset(out, 0, sizeof *out);
    if (!max_pixels)
        max_pixels = SK_MEDIA_MAX_PIXELS;
    sk_media_type type = sk_media_sniff(data, n);
    if (type != SK_MEDIA_PNG && type != SK_MEDIA_JPEG)
        return say(err, "is not a PNG or a JPEG");
    size_t padded_n = 0;
    uint8_t *padded = type == SK_MEDIA_PNG ? png_fixed(data, n, &padded_n) : NULL;
    bool ok = padded ? decode(padded, padded_n, type, max_pixels, out, err) : decode(data, n, type, max_pixels, out, err);
    free(padded);
    return ok;
}

/* ---- SVG ---- */

struct sk_svg {
    NSVGimage *im;
    float w, h;
};

void sk_svg_free(sk_svg *s)
{
    if (!s)
        return;
    nsvgDelete(s->im);
    free(s);
}

void sk_svg_size(const sk_svg *s, float *w, float *h)
{
    *w = s->w;
    *h = s->h;
}

/* Whether each of n numbers is finite and at most bound across. */
static bool bounded(const float *v, size_t n, float bound)
{
    for (size_t i = 0; i < n; i++)
        if (!(fabsf(v[i]) <= bound))
            return false;
    return true;
}

#define MAX_MITER 100.0f

/* A transform's entries at most this far from 0: a gradient's
 * coordinates, its inverse applied to a pixel's, then stay far from
 * overflow, so never infinite, never NaN, where the rasterizer turns
 * them into a colour's index. */
#define MAX_XFORM 1.0e9f

/* What the rasterizer reads of a shape, each number finite. */
static bool shape_ok(const NSVGshape *sh, size_t *points)
{
    if (!isfinite(sh->opacity) || !isfinite(sh->strokeWidth) || sh->strokeWidth < 0 ||
        !isfinite(sh->strokeDashOffset) || !isfinite(sh->miterLimit) || !bounded(sh->xform, 6, MAX_XFORM))
        return false;
    signed char dashes = (signed char)sh->strokeDashCount;
    if (dashes < 0 || dashes > 8 || !bounded(sh->strokeDashArray, 8, INFINITY))
        return false;
    const NSVGpaint *paints[2] = {&sh->fill, &sh->stroke};
    for (int k = 0; k < 2; k++) {
        const NSVGpaint *p = paints[k];
        if (p->type != NSVG_PAINT_LINEAR_GRADIENT && p->type != NSVG_PAINT_RADIAL_GRADIENT)
            continue;
        const NSVGgradient *g = p->gradient;
        /* nanosvg sets the focus of a radial gradient alone: a linear
         * one's fx and fy are left as malloc gave them. */
        if (!g || !bounded(g->xform, 6, MAX_XFORM) || g->nstops < 0 ||
            (p->type == NSVG_PAINT_RADIAL_GRADIENT && (!isfinite(g->fx) || !isfinite(g->fy))))
            return false;
        for (int i = 0; i < g->nstops; i++)
            if (!isfinite(g->stops[i].offset))
                return false;
    }
    for (const NSVGpath *pa = sh->paths; pa; pa = pa->next) {
        if (pa->npts < 1 || (size_t)pa->npts > SK_MEDIA_MAX_SVG_POINTS)
            return false;
        *points += (size_t)pa->npts;
        if (*points > SK_MEDIA_MAX_SVG_POINTS || !bounded(pa->pts, 2 * (size_t)pa->npts, INFINITY))
            return false;
    }
    return true;
}

sk_svg *sk_svg_parse(const uint8_t *data, size_t n, sk_buf *err)
{
    if (n > SK_MEDIA_MAX_SVG_BYTES) {
        say(err, "is an SVG larger than %zu bytes", SK_MEDIA_MAX_SVG_BYTES);
        return NULL;
    }
    /* nanosvg parses a C string, in place. Its style parser, given a
     * declaration list that ends in a ';' and spaces, or in spaces
     * alone, reads a byte past the string's end. A style sheet's block
     * it copies into a string of its own, ending at its '}' or where
     * the element's text ends, at the next '<' or the end: so the
     * spaces before each of those go. Two NULs end the text, for an
     * attribute's value that runs to the end. */
    sk_buf text = {0};
    sk_buf_reserve(&text, n + 2);
    for (size_t i = 0; i <= n; i++) {
        if (i == n || data[i] == '}' || data[i] == '<')
            while (text.len && (text.ptr[text.len - 1] == ' ' || text.ptr[text.len - 1] == '\t' ||
                                text.ptr[text.len - 1] == '\n' || text.ptr[text.len - 1] == '\r'))
                text.len--;
        sk_buf_putc(&text, i < n ? (char)data[i] : 0);
    }
    NSVGimage *im = nsvgParse(text.ptr, "px", 96.0f);
    sk_buf_free(&text);
    if (!im) {
        say(err, "is not an SVG nanosvg can read");
        return NULL;
    }
    if (!(im->width > 0 && im->height > 0 && im->width <= SK_MEDIA_MAX_SIDE && im->height <= SK_MEDIA_MAX_SIDE)) {
        say(err, "is an SVG of %g x %g: it must have a size, at most %d a side", (double)im->width,
            (double)im->height, SK_MEDIA_MAX_SIDE);
        nsvgDelete(im);
        return NULL;
    }
    size_t points = 0;
    for (NSVGshape *sh = im->shapes; sh; sh = sh->next) {
        if (!shape_ok(sh, &points)) {
            say(err, "is an SVG with a shape nanosvg cannot draw: a number past all reason, or more than %d points",
                SK_MEDIA_MAX_SVG_POINTS);
            nsvgDelete(im);
            return NULL;
        }
        /* A miter's spike is at most this many half widths long, so a
         * stroke ends near its path, inside the coordinates drawable()
         * allows: past it, a corner too sharp to see the difference. */
        sh->miterLimit = fminf(sh->miterLimit, MAX_MITER);
    }
    sk_svg *s = sk_xcalloc(1, sizeof *s);
    s->im = im;
    s->w = im->width;
    s->h = im->height;
    return s;
}

/* The points nsvg__flattenCubicBez adds for a cubic, by its own test
 * of flatness, at most 1024. */
static long cubic_points(float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4, int level)
{
    if (level > 10)
        return 0;
    float dx = x4 - x1, dy = y4 - y1;
    float d2 = fabsf((x2 - x4) * dy - (y2 - y4) * dx), d3 = fabsf((x3 - x4) * dy - (y3 - y4) * dx);
    if ((d2 + d3) * (d2 + d3) < 0.25f * (dx * dx + dy * dy))
        return 1;
    float x12 = (x1 + x2) * 0.5f, y12 = (y1 + y2) * 0.5f, x23 = (x2 + x3) * 0.5f, y23 = (y2 + y3) * 0.5f;
    float x34 = (x3 + x4) * 0.5f, y34 = (y3 + y4) * 0.5f, x123 = (x12 + x23) * 0.5f, y123 = (y12 + y23) * 0.5f;
    float x234 = (x23 + x34) * 0.5f, y234 = (y23 + y34) * 0.5f, x1234 = (x123 + x234) * 0.5f,
          y1234 = (y123 + y234) * 0.5f;
    return cubic_points(x1, y1, x12, y12, x123, y123, x1234, y1234, level + 1) +
           cubic_points(x1234, y1234, x234, y234, x34, y34, x4, y4, level + 1);
}

/* The rasterizer's work at scale k, bounded before it starts: the
 * points it flattens the paths into, the dashes it cuts them into, the
 * width of the strokes it widens them by, and where they all lie. */
#define MAX_FLAT_POINTS (1L << 20)
#define MAX_DASHES 16384
#define MAX_STROKE_PX 4096.0f
#define MAX_COORD_PX 1.0e6f

static bool drawable(const NSVGimage *im, float k, float tx, float ty)
{
    long flat = 0;
    for (const NSVGshape *sh = im->shapes; sh; sh = sh->next) {
        bool stroked = sh->stroke.type != NSVG_PAINT_NONE && sh->strokeWidth * k > 0.01f;
        if (stroked && sh->strokeWidth * k > MAX_STROKE_PX)
            return false;
        double length = 0;
        for (const NSVGpath *pa = sh->paths; pa; pa = pa->next) {
            const float *p = pa->pts;
            for (int i = 0; i < pa->npts; i++)
                if (!(fabsf(p[2 * i] * k + tx) <= MAX_COORD_PX && fabsf(p[2 * i + 1] * k + ty) <= MAX_COORD_PX))
                    return false;
            flat += 1;
            for (int i = 0; i + 3 < pa->npts; i += 3) {
                const float *c = p + 2 * i;
                flat += cubic_points(c[0] * k, c[1] * k, c[2] * k, c[3] * k, c[4] * k, c[5] * k, c[6] * k, c[7] * k, 0);
                for (int j = 0; j < 3; j++)
                    length += hypot((double)(c[2 * j + 2] - c[2 * j]), (double)(c[2 * j + 3] - c[2 * j + 1])) * k;
                if (flat > MAX_FLAT_POINTS)
                    return false;
            }
        }
        if (stroked && sh->strokeDashCount > 0) {
            double all = 0;
            for (int j = 0; j < sh->strokeDashCount; j++)
                all += sh->strokeDashArray[j];
            if (sh->strokeDashCount & 1)
                all *= 2;
            /* A pattern's dashes along the polygon round the curves,
             * which is no shorter than they are. */
            if (!(all * k > 0) || length / (all * k) * 2 * sh->strokeDashCount > MAX_DASHES)
                return false;
        }
    }
    return true;
}

bool sk_svg_render(const sk_svg *s, float w, float h, float scale, size_t max_pixels, sk_image *out, sk_buf *err)
{
    memset(out, 0, sizeof *out);
    if (!max_pixels)
        max_pixels = SK_MEDIA_MAX_PIXELS;
    double pw = round((double)w * scale), ph = round((double)h * scale);
    if (!(pw >= 1 && ph >= 1 && pw <= SK_MEDIA_MAX_SIDE && ph <= SK_MEDIA_MAX_SIDE) || pw * ph > (double)max_pixels)
        return say(err, "is an SVG to be drawn at %g x %g pixels: an image may be at most %d a side and %zu pixels "
                        "in all",
                   pw, ph, SK_MEDIA_MAX_SIDE, max_pixels);
    int iw = (int)pw, ih = (int)ph;
    float k = (float)fmin(pw / s->w, ph / s->h);
    float tx = (float)((pw - s->w * k) / 2), ty = (float)((ph - s->h * k) / 2);
    if (!drawable(s->im, k, tx, ty))
        return say(err, "is an SVG too detailed to draw at %d x %d pixels", iw, ih);
    uint8_t *rgba = sk_xcalloc((size_t)iw * (size_t)ih, 4);
    NSVGrasterizer *r = nsvgCreateRasterizer();
    if (!r)
        sk_die("out of memory");
    nsvgRasterize(r, s->im, tx, ty, k, rgba, iw, ih, iw * 4);
    nsvgDeleteRasterizer(r);
    sk_image_init(out, iw, ih);
    const uint8_t *p = rgba;
    for (int y = 0; y < ih; y++) {
        sk_px *row = sk_image_row(out, y);
        for (int x = 0; x < iw; x++, p += 4)
            row[x] = sk_premul(SK_RGBA(p[0], p[1], p[2], p[3]));
    }
    free(rgba);
    return true;
}

/* ---- Sounds ---- */

typedef SK_VEC(int16_t) samples;

static bool rate_ok(uint32_t rate, sk_buf *err)
{
    if (rate >= SK_MEDIA_MIN_RATE && rate <= SK_MEDIA_MAX_RATE)
        return true;
    return say(err, "is at %u Hz: a skin's sounds are at %d to %d Hz", (unsigned)rate, SK_MEDIA_MIN_RATE,
               SK_MEDIA_MAX_RATE);
}

static bool channels_ok(int channels, sk_buf *err)
{
    if (channels == 1 || channels == 2)
        return true;
    return say(err, "has %d channels: a skin's sounds are mono or stereo", channels);
}

static bool too_long(sk_buf *err, double max_seconds)
{
    return say(err, "is longer than %g seconds, the most a skin's sound may be", max_seconds);
}

static int16_t to16(float v)
{
    if (!(v > -1.0f))
        return v < 0 ? -32767 : 0; /* NaN is silence */
    if (v >= 1.0f)
        return 32767;
    return (int16_t)lrintf(v * 32767.0f);
}

/* All its memory from one buffer, zeroed: stb_vorbis takes what a
 * stream's headers ask for from it, or fails, and never from the
 * stack. coffee's sounds take 177,456 bytes to set up and 4 KiB a
 * frame. */
#define VORBIS_ARENA (4 << 20)
/* The most the comment header's text may take. */
#define VORBIS_COMMENTS (1 << 20)

/* The second packet of an Ogg stream, the Vorbis comment header, as
 * far as the data holds it: its segments gathered across the pages in
 * their order in the data, as stb_vorbis reads them. */
static void ogg_comments(const uint8_t *d, size_t n, sk_buf *out)
{
    int packet = 0;
    for (size_t at = 0; at + 27 <= n && memcmp(d + at, "OggS", 4) == 0;) {
        size_t segs = d[at + 26], body = at + 27 + segs;
        if (body > n)
            return;
        for (size_t s = 0; s < segs; s++) {
            size_t len = d[at + 27 + s];
            if (packet == 1 && body < n)
                sk_buf_append(out, d + body, SK_MIN(len, n - body));
            body += len;
            if (len < 255 && packet++ == 1)
                return;
        }
        at = body;
    }
}

/* stb_vorbis reads the comment header's lengths with no check, reading
 * one past the data as 0xFFFFFFFF, allocates what they say, and its
 * cleanup after an allocation that failed reads the list it could not
 * make. So each length must lie inside the header, its text too, and
 * all of it fit in VORBIS_COMMENTS, which the arena always has. */
/* A 32-bit length at *at, *at then past it; false past the data. */
static bool take_len(const uint8_t *p, size_t len, size_t *at, uint64_t *v)
{
    if (*at + 4 > len)
        return false;
    *v = le32(p + *at);
    *at += 4;
    return true;
}

static bool comments_ok(const uint8_t *d, size_t n)
{
    sk_buf packet = {0};
    ogg_comments(d, n, &packet);
    const uint8_t *p = (const uint8_t *)packet.ptr;
    size_t len = packet.len, at = 7;
    uint64_t v = 0, count = 0, total = 0;
    /* The packet's type and "vorbis", the vendor string, the count of
     * comments, then each. */
    bool ok = len >= at && take_len(p, len, &at, &v) && v <= len - at;
    if (ok) {
        at += (size_t)v;
        total = v + 8;
        ok = take_len(p, len, &at, &count) && count <= (len - at) / 4;
        total += 8 * count;
    }
    for (uint64_t i = 0; ok && i < count; i++) {
        ok = take_len(p, len, &at, &v) && v <= len - at;
        if (ok)
            at += (size_t)v;
        total += v + 8;
    }
    sk_buf_free(&packet);
    return ok && total <= VORBIS_COMMENTS;
}

static bool vorbis(const uint8_t *data, size_t n, double max_seconds, sk_sound *out, sk_buf *err)
{
    if (n > INT_MAX)
        return say(err, "is an Ogg Vorbis file larger than stb_vorbis reads");
    if (!comments_ok(data, n))
        return say(err, "is not an Ogg Vorbis file whose comment header stb_vorbis can read");
    stb_vorbis_alloc arena = {sk_xcalloc(VORBIS_ARENA, 1), VORBIS_ARENA};
    int e = 0;
    stb_vorbis *v = stb_vorbis_open_memory(data, (int)n, &e, &arena);
    if (!v) {
        free(arena.alloc_buffer);
        return say(err, "is not an Ogg Vorbis file stb_vorbis can read (error %d)", e);
    }
    stb_vorbis_info info = stb_vorbis_get_info(v);
    bool ok = channels_ok(info.channels, err) && rate_ok(info.sample_rate, err);
    samples pcm = {0};
    double most = max_seconds * info.sample_rate;
    while (ok) {
        int channels = 0;
        float **frame = NULL;
        int got = stb_vorbis_get_frame_float(v, &channels, &frame);
        if (got <= 0)
            break;
        if (channels != info.channels) {
            ok = say(err, "is an Ogg Vorbis file whose channels change");
            break;
        }
        if ((double)pcm.len + got > most) {
            ok = too_long(err, max_seconds);
            break;
        }
        for (int i = 0; i < got; i++)
            sk_vec_push(&pcm, to16(channels == 2 ? (frame[0][i] + frame[1][i]) * 0.5f : frame[0][i]));
    }
    /* The end of the data ends a stream that has no last page. */
    int stopped = stb_vorbis_get_error(v);
    if (ok && stopped > VORBIS_need_more_data && stopped != VORBIS_unexpected_eof)
        ok = say(err, "is an Ogg Vorbis file stb_vorbis stopped reading (error %d)", stopped);
    stb_vorbis_close(v);
    free(arena.alloc_buffer);
    if (ok && !pcm.len)
        ok = say(err, "is an Ogg Vorbis file with no sound in it");
    if (!ok) {
        sk_vec_free(&pcm);
        return false;
    }
    out->pcm = pcm.ptr;
    out->frames = pcm.len;
    out->rate = info.sample_rate;
    return true;
}

/* One sample of a WAV's PCM as a float from -1 to 1. */
static float wav_sample(const uint8_t *p, unsigned bits, bool fl)
{
    if (fl) {
        uint32_t u = le32(p);
        float f;
        memcpy(&f, &u, 4);
        return f;
    }
    /* Two's complement of the width, by hand. */
    switch (bits) {
    case 8:
        return (float)(p[0] - 128) / 128.0f;
    case 16: {
        long v = le16(p);
        return (float)(v >= 0x8000 ? v - 0x10000 : v) / 32768.0f;
    }
    case 24: {
        long v = (long)p[0] | (long)p[1] << 8 | (long)p[2] << 16;
        return (float)(v >= 0x800000 ? v - 0x1000000 : v) / 8388608.0f;
    }
    default: {
        long long v = le32(p);
        return (float)((double)(v >= 0x80000000LL ? v - 0x100000000LL : v) / 2147483648.0);
    }
    }
}

/* RIFF's chunks walked by their lengths, each padded to even: fmt's
 * format (PCM 1, float 3, or the extensible form's subformat), then
 * the data's whole frames. */
static bool wav(const uint8_t *data, size_t n, double max_seconds, sk_sound *out, sk_buf *err)
{
    const uint8_t *fmt = NULL, *body = NULL;
    size_t fmt_len = 0, body_len = 0;
    for (size_t at = 12; at + 8 <= n;) {
        size_t len = le32(data + at + 4), room = n - at - 8;
        if (!memcmp(data + at, "fmt ", 4) && !fmt) {
            fmt = data + at + 8;
            fmt_len = SK_MIN(len, room);
        } else if (!memcmp(data + at, "data", 4) && !body) {
            body = data + at + 8;
            body_len = SK_MIN(len, room);
        }
        if (len > room)
            break;
        at += 8 + len + (len & 1);
    }
    if (!fmt || fmt_len < 16 || !body)
        return say(err, "is a WAV file without its fmt and data chunks");
    unsigned format = le16(fmt), channels = le16(fmt + 2), align = le16(fmt + 12), bits = le16(fmt + 14);
    uint32_t rate = le32(fmt + 4);
    if (format == 0xfffe && fmt_len >= 26)
        format = le16(fmt + 24);
    bool fl = format == 3;
    if (!(format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) && !(fl && bits == 32))
        return say(err, "is a WAV file of format %u and %u bits: skinc reads PCM of 8, 16, 24 or 32 bits, or "
                        "32-bit float",
                   format, bits);
    if (!channels_ok((int)channels, err) || !rate_ok(rate, err))
        return false;
    if (align != channels * bits / 8)
        return say(err, "is a WAV file whose frames are %u bytes, not %u", align, channels * bits / 8);
    size_t frames = body_len / align;
    if (!frames)
        return say(err, "is a WAV file with no sound in it");
    if ((double)frames > max_seconds * rate)
        return too_long(err, max_seconds);
    out->pcm = sk_xmalloc(frames * sizeof *out->pcm);
    unsigned width = bits / 8;
    for (size_t i = 0; i < frames; i++) {
        const uint8_t *f = body + i * align;
        float v = wav_sample(f, bits, fl);
        if (channels == 2)
            v = (v + wav_sample(f + width, bits, fl)) * 0.5f;
        out->pcm[i] = to16(v);
    }
    out->frames = frames;
    out->rate = rate;
    return true;
}

bool sk_media_sound(const uint8_t *data, size_t n, double max_seconds, sk_sound *out, sk_buf *err)
{
    memset(out, 0, sizeof *out);
    if (!(max_seconds > 0))
        max_seconds = SK_MEDIA_MAX_SECONDS;
    switch (sk_media_sniff(data, n)) {
    case SK_MEDIA_OGG:
        return vorbis(data, n, max_seconds, out, err);
    case SK_MEDIA_WAV:
        return wav(data, n, max_seconds, out, err);
    default:
        return say(err, "is not an Ogg Vorbis or a WAV file: skinc reads those");
    }
}

void sk_sound_free(sk_sound *s)
{
    free(s->pcm);
    memset(s, 0, sizeof *s);
}

/* ---- What a pack holds ---- */

uint8_t *sk_media_qoi(const sk_image *im, size_t *n)
{
    if (im->w <= 0 || im->h <= 0)
        return NULL;
    return sk_qoi_encode(im, n);
}

uint8_t *sk_media_adpcm(const sk_sound *s, unsigned block_bytes, size_t *n)
{
    return sk_adpcm_encode(s->pcm, s->frames, block_bytes, n);
}
