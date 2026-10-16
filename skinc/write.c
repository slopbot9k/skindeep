/* skinc's pack writer. The front end gives the tree, its inputs and
 * state tables, the deduplicated styles and the keyframe tracks; this
 * decodes the skin's images, fonts and sounds and lays all of it out as
 * decision 0037's file: a header, the section table, then each section
 * 4-byte aligned, every string once in STRS.
 *
 * The elements, keyframes and images keep the front end's indexes, so a
 * style's image source and animation name mean in the pack what they
 * meant in skinc; the manifest's own images (the menu's, a needle's
 * face, a knob's image, a sprite's sheet) follow the stylesheet's. The
 * styles are those some state table holds, in the front end's order:
 * one the counters replaced, which still holds counter(), is left out.
 *
 * Images. A PNG or JPEG is kept at its own pixels, scale 0. An SVG,
 * which only skinc can draw, is drawn at each size it is drawn at that
 * is known before layout, at 1x and 2x:
 *   - the menu button's image, hover and pressed images, at the
 *     manifest rect's width and height;
 *   - a background layer whose background-size is auto or lengths in
 *     px: auto auto is the SVG's own size, one auto keeps its ratio.
 * The rest need the layout, and the layer pass draws them: a background
 * sized by percentages, cover or contain; a border image, sliced to its
 * box; a needle's face and a knob's image, which fill an svg CSS sizes.
 * Until then those SVGs have no variant for those uses.
 *
 * Sounds are decoded to mono 16-bit at their own rate and kept as IMA
 * ADPCM in blocks of SK_ADPCM_BLOCK bytes; fonts are kept as they are,
 * once the checker passes them. Everything decoded goes to BLOB in the
 * order images, fonts, sounds. */
#include "write.h"

#include "../src/audio/adpcm.h"
#include "../src/base/base64.h"
#include "../src/base/map.h"
#include "../src/base/paths.h"
#include "../src/base/utf8.h"
#include "../src/skin/packfmt.h"
#include "../src/text/fontcheck.h"
#include "media.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

/* The sections in decision 0037's order, which is the file's. */
enum {
    W_STRS,
    W_META,
    W_SPLT,
    W_SPAN,
    W_RECT,
    W_WIDG,
    W_MARK,
    W_ELEM,
    W_SVGG,
    W_INPT,
    W_STAB,
    W_STYL,
    W_KEYF,
    W_IMGS,
    W_VARI,
    W_LAYR,
    W_FONT,
    W_SNDS,
    W_BLOB,
    W_COUNT,
};

static const uint32_t IDS[W_COUNT] = {
    SK_SECT_STRS, SK_SECT_META, SK_SECT_SPLT, SK_SECT_SPAN, SK_SECT_RECT, SK_SECT_WIDG, SK_SECT_MARK,
    SK_SECT_ELEM, SK_SECT_SVGG, SK_SECT_INPT, SK_SECT_STAB, SK_SECT_STYL, SK_SECT_KEYF, SK_SECT_IMGS,
    SK_SECT_VARI, SK_SECT_LAYR, SK_SECT_FONT, SK_SECT_SNDS, SK_SECT_BLOB,
};

/* A size an SVG is drawn at, in CSS pixels. */
typedef struct {
    float w, h;
} svg_size;

typedef struct {
    const char *path; /* relative to the skin, or a data url */
    const char *real; /* its realpath, or the data url */
    char *owned;      /* real, when the writer found it */
    bool is_svg;
    sk_svg *svg;
    sk_image raster;
    uint16_t w, h;
    SK_VEC(svg_size) sizes;
    uint32_t first, nvariants;
} wimage;

typedef struct {
    const sk_front *f;
    sk_diags *d;
    bool failed;
    sk_buf strs;
    uint32_t nstrs;
    sk_map pool;  /* string to its offset + 1 */
    sk_map probe; /* string to an id, for telling payloads apart */
    uintptr_t nprobe;
    sk_buf blob;
    SK_VEC(wimage) images;
    sk_buf sec[W_COUNT];
    uint32_t count[W_COUNT];
    uint32_t *widget_rect, *toggle_rect;  /* RECT indexes */
    uint32_t *widget_el, *toggle_el;      /* ELEM indexes */
    uint32_t *face, *image, *sheet;       /* IMGS indexes */
    uint32_t menu[3];
    /* The styles some table holds, in the front end's order: a style
     * the counters replaced is in none. */
    uint32_t *style_at; /* the front end's index to the pack's, or SK_PACK_NONE */
    SK_VEC(uint32_t) kept;
    bool said[SK_PROP_COUNT];
} writer;

/* ---- Messages, strings, bytes ---- */

static const char *css_file(const writer *w)
{
    return w->f->css_shown ? w->f->css_shown : "skin.css";
}

/* A file of the skin as messages name it: the folder as given. */
static char *shown(const writer *w, const char *rel)
{
    return sk_path_join(w->f->skin.dir, rel);
}

static void fail_file(writer *w, const char *rel, const char *why)
{
    char *s = shown(w, rel);
    sk_diag(w->d, s, "(file)", "%s", why);
    free(s);
    w->failed = true;
}

/* A string's offset in STRS, made valid UTF-8 as a browser shows it,
 * each string once. */
static uint32_t intern(void *data, const char *s)
{
    writer *w = data;
    if (!s)
        return SK_PACK_NONE;
    sk_buf clean = {0};
    sk_buf_reserve(&clean, 1);
    sk_utf8_sanitize(&clean, s, strlen(s));
    void **slot = sk_map_put(&w->pool, sk_buf_str(&clean));
    if (!*slot) {
        *slot = (void *)(uintptr_t)(w->strs.len + 1);
        sk_buf_append(&w->strs, sk_buf_cstr(&clean), clean.len + 1);
        w->nstrs++;
    }
    sk_buf_free(&clean);
    return (uint32_t)((uintptr_t)*slot - 1);
}

/* The same strings the same id, and nothing pooled: for comparing a
 * payload with the initial value's. */
static uint32_t probe(void *data, const char *s)
{
    writer *w = data;
    if (!s)
        return SK_PACK_NONE;
    void **slot = sk_map_put(&w->probe, sk_str_c(s));
    if (!*slot)
        *slot = (void *)++w->nprobe;
    return (uint32_t)(uintptr_t)*slot;
}

/* n bytes into BLOB, 4-byte aligned; their offset. */
static uint32_t blob_add(writer *w, const void *p, size_t n)
{
    while (w->blob.len % 4)
        sk_put8(&w->blob, 0);
    uint32_t at = (uint32_t)w->blob.len;
    sk_buf_append(&w->blob, p, n);
    return at;
}

static uint8_t *read_file(const char *path, size_t *n)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return NULL;
    sk_buf b = {0};
    char chunk[65536];
    ssize_t r;
    while ((r = read(fd, chunk, sizeof chunk)) != 0) {
        if (r < 0 && errno == EINTR)
            continue;
        if (r < 0) {
            close(fd);
            sk_buf_free(&b);
            return NULL;
        }
        sk_buf_append(&b, chunk, (size_t)r);
    }
    close(fd);
    if (!b.ptr)
        sk_buf_reserve(&b, 1);
    *n = b.len;
    return (uint8_t *)b.ptr;
}

/* A file of the skin, by its path relative to it. */
static uint8_t *skin_file(const writer *w, const char *rel, size_t *n)
{
    char *p = sk_path_join(w->f->skin.real, rel);
    uint8_t *b = read_file(p, n);
    free(p);
    return b;
}

static int hex_digit(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* A data: url's bytes: base64 when ";base64" ends its head, else
 * percent-encoded. */
static bool data_url(const char *url, sk_buf *out)
{
    const char *comma = strchr(url, ',');
    if (!comma)
        return false;
    bool b64 = comma - url >= 7 && strncasecmp(comma - 7, ";base64", 7) == 0;
    const char *p = comma + 1;
    if (b64) {
        sk_buf clean = {0};
        for (; *p; p++)
            if (!strchr(" \t\n\r\f", *p))
                sk_buf_putc(&clean, *p);
        bool ok = sk_base64url_decode(out, sk_str_n(clean.ptr ? clean.ptr : "", clean.len));
        sk_buf_free(&clean);
        return ok;
    }
    for (; *p; p++) {
        if (*p == '%' && hex_digit(p[1]) >= 0 && hex_digit(p[2]) >= 0) {
            sk_put8(out, (unsigned)(hex_digit(p[1]) * 16 + hex_digit(p[2])));
            p += 2;
        } else {
            sk_buf_putc(out, *p);
        }
    }
    return true;
}

static void put_rect(sk_buf *b, const sk_pack_rect *r)
{
    for (int i = 0; i < 6; i++)
        sk_putf32(b, r->v[i]);
    sk_put8(b, r->has);
    for (int i = 0; i < 6; i++)
        sk_put8(b, (uint8_t)r->follow[i]);
    sk_put8(b, 0);
}

/* ---- Images ---- */

/* The image a manifest's path names, added after the stylesheet's when
 * it is none of them. */
static uint32_t image_of(writer *w, const char *rel)
{
    if (!rel)
        return SK_PACK_NONE;
    char *full = sk_path_join(w->f->skin.real, rel);
    char *real = realpath(full, NULL);
    free(full);
    if (!real) {
        fail_file(w, rel, "cannot be read");
        return SK_PACK_NONE;
    }
    for (size_t i = 0; i < w->images.len; i++)
        if (!strcmp(w->images.ptr[i].real, real)) {
            free(real);
            return (uint32_t)i;
        }
    wimage im;
    memset(&im, 0, sizeof im);
    size_t rl = strlen(w->f->skin.real);
    im.path = !strncmp(real, w->f->skin.real, rl) && real[rl] == '/' ? real + rl + 1 : rel;
    im.real = im.owned = real;
    sk_vec_push(&w->images, im);
    return (uint32_t)(w->images.len - 1);
}

static void add_size(wimage *im, float cw, float ch)
{
    if (!(cw > 0 && ch > 0))
        return;
    for (size_t i = 0; i < im->sizes.len; i++)
        if (im->sizes.ptr[i].w == cw && im->sizes.ptr[i].h == ch)
            return;
    svg_size z = {cw, ch};
    sk_vec_push(&im->sizes, z);
}

/* Each image read and decoded: an SVG parsed, anything else drawn into
 * its pixels. */
static void decode_images(writer *w)
{
    for (size_t i = 0; i < w->images.len; i++) {
        wimage *im = &w->images.ptr[i];
        bool data = !strncasecmp(im->real, "data:", 5);
        sk_buf bytes = {0};
        if (data) {
            if (!data_url(im->real, &bytes)) {
                sk_diag(w->d, css_file(w), "(data url)", "a data: url skinc cannot decode");
                w->failed = true;
                continue;
            }
        } else {
            size_t n = 0;
            uint8_t *b = read_file(im->real, &n);
            if (!b) {
                fail_file(w, im->path, "cannot be read");
                continue;
            }
            bytes.ptr = (char *)b;
            bytes.len = bytes.cap = n;
        }
        const uint8_t *p = (const uint8_t *)bytes.ptr;
        sk_buf why = {0};
        if (sk_media_sniff(p, bytes.len) == SK_MEDIA_SVG) {
            im->is_svg = true;
            im->svg = sk_svg_parse(p, bytes.len, &why);
            if (im->svg) {
                float sw, sh;
                sk_svg_size(im->svg, &sw, &sh);
                im->w = (uint16_t)SK_MAX(1, SK_MIN(lroundf(sw), (long)SK_PACK_MAX_SIDE));
                im->h = (uint16_t)SK_MAX(1, SK_MIN(lroundf(sh), (long)SK_PACK_MAX_SIDE));
            }
        } else if (sk_media_image(p, bytes.len, 0, &im->raster, &why)) {
            im->w = (uint16_t)im->raster.w;
            im->h = (uint16_t)im->raster.h;
        }
        if (why.len) {
            if (data)
                sk_diag(w->d, css_file(w), "(data url)", "%s", sk_buf_cstr(&why));
            else
                fail_file(w, im->path, sk_buf_cstr(&why));
            w->failed = true;
        }
        sk_buf_free(&why);
        sk_buf_free(&bytes);
    }
}

/* The sizes each SVG is drawn at that are known before layout. */
static void svg_sizes(writer *w)
{
    const sk_front *f = w->f;
    const sk_pack_rect *mr = &f->skin.menu_rect;
    /* The rect's width and height, keys 2 and 5. */
    if ((mr->has & (1u << 2)) && (mr->has & (1u << 5)))
        for (int k = 0; k < 3; k++)
            if (w->menu[k] != SK_PACK_NONE && w->images.ptr[w->menu[k]].svg)
                add_size(&w->images.ptr[w->menu[k]], mr->v[2], mr->v[5]);
    for (size_t i = 0; i < w->kept.len; i++) {
        const sk_css_style *s = f->styles.ptr[w->kept.ptr[i]];
        for (uint16_t k = 0; k < s->nbg_image; k++) {
            const sk_css_image *x = &s->bg_image[k];
            if (x->kind != SK_IMAGE_URL || x->source >= w->images.len || !w->images.ptr[x->source].svg)
                continue;
            wimage *im = &w->images.ptr[x->source];
            const sk_css_bgsize *z = &s->bg_size[k % s->nbg_size];
            bool wa = z->w.kind == SK_LEN_AUTO, ha = z->h.kind == SK_LEN_AUTO;
            bool wp = z->w.kind == SK_LEN_PX && z->w.pct == 0, hp = z->h.kind == SK_LEN_PX && z->h.pct == 0;
            if (z->kind != SK_BGSIZE_EXPLICIT || !(wa || wp) || !(ha || hp))
                continue; /* the layer pass's */
            float sw, sh;
            sk_svg_size(im->svg, &sw, &sh);
            float cw = wp ? z->w.px : sw, ch = hp ? z->h.px : sh;
            if (wa && hp)
                cw = ch * sw / sh;
            if (wp && ha)
                ch = cw * sh / sw;
            add_size(im, cw, ch);
        }
    }
}

typedef struct {
    uint16_t w, h;
    uint8_t scale;
    uint32_t blob, len;
} wvariant;

typedef SK_VEC(wvariant) wvariants;

static int by_variant(const void *a, const void *b)
{
    const wvariant *x = a, *y = b;
    if (x->scale != y->scale)
        return x->scale < y->scale ? -1 : 1;
    if (x->w != y->w)
        return x->w < y->w ? -1 : 1;
    return x->h < y->h ? -1 : x->h > y->h;
}

static void variant(writer *w, wvariants *out, const sk_image *im, uint8_t scale)
{
    size_t n = 0;
    uint8_t *qoi = sk_media_qoi(im, &n);
    if (!qoi)
        return;
    wvariant v = {(uint16_t)im->w, (uint16_t)im->h, scale, blob_add(w, qoi, n), (uint32_t)n};
    free(qoi);
    for (size_t i = 0; i < out->len; i++)
        if (out->ptr[i].w == v.w && out->ptr[i].h == v.h && out->ptr[i].scale == v.scale)
            return;
    sk_vec_push(out, v);
}

/* IMGS and VARI, and the pixels into BLOB. */
static void images(writer *w)
{
    for (size_t i = 0; i < w->images.len; i++) {
        wimage *im = &w->images.ptr[i];
        wvariants vs = {0};
        if (im->raster.w) {
            variant(w, &vs, &im->raster, 0);
        } else if (im->svg) {
            for (size_t k = 0; k < im->sizes.len; k++)
                for (uint8_t scale = 1; scale <= 2; scale++) {
                    sk_image px;
                    sk_buf why = {0};
                    if (sk_svg_render(im->svg, im->sizes.ptr[k].w, im->sizes.ptr[k].h, scale, 0, &px, &why))
                        variant(w, &vs, &px, scale);
                    else
                        fail_file(w, im->path, sk_buf_cstr(&why));
                    sk_buf_free(&why);
                    sk_image_free(&px);
                }
        }
        if (vs.len > 1)
            qsort(vs.ptr, vs.len, sizeof *vs.ptr, by_variant);
        sk_buf *b = &w->sec[W_IMGS];
        sk_put32(b, intern(w, im->path));
        sk_put16(b, im->w ? im->w : 1);
        sk_put16(b, im->h ? im->h : 1);
        sk_put32(b, w->count[W_VARI]);
        sk_put16(b, (unsigned)vs.len);
        sk_put16(b, 0);
        w->count[W_IMGS]++;
        for (size_t k = 0; k < vs.len; k++) {
            sk_buf *v = &w->sec[W_VARI];
            sk_put16(v, vs.ptr[k].w);
            sk_put16(v, vs.ptr[k].h);
            sk_put8(v, vs.ptr[k].scale);
            sk_put8(v, 0);
            sk_put16(v, 0);
            sk_put32(v, vs.ptr[k].blob);
            sk_put32(v, vs.ptr[k].len);
            w->count[W_VARI]++;
        }
        sk_vec_free(&vs);
    }
}

/* ---- The manifest ---- */

/* The pack font a family names for the terminal: the skin's face of
 * that family nearest to regular, or none (DejaVu Sans Mono). */
static uint32_t terminal_font(const writer *w)
{
    const sk_skin *s = &w->f->skin;
    uint32_t best = SK_PACK_NONE;
    long best_key = 0;
    for (size_t i = 0; s->terminal_font && i < s->nfonts; i++) {
        if (strcasecmp(s->fonts[i].family, s->terminal_font) != 0)
            continue;
        long key = labs((long)s->fonts[i].weight - 400) + (s->fonts[i].style != SK_FSTYLE_NORMAL ? 10000 : 0);
        if (best == SK_PACK_NONE || key < best_key) {
            best = (uint32_t)i;
            best_key = key;
        }
    }
    return best;
}

static void meta(writer *w)
{
    const sk_skin *s = &w->f->skin;
    sk_buf *b = &w->sec[W_META];
    const char *str[5] = {s->name, s->version, s->author, s->description, s->license};
    for (int i = 0; i < 5; i++)
        sk_put32(b, intern(w, str[i]));
    sk_putf32(b, s->width);
    sk_putf32(b, s->height);
    sk_putf32(b, s->min_width);
    sk_putf32(b, s->min_height);
    put_rect(b, &s->menu_rect);
    for (int i = 0; i < 3; i++)
        sk_put32(b, w->menu[i]);
    put_rect(b, &s->terminal_rect);
    sk_put32(b, terminal_font(w));
    sk_putf32(b, s->terminal_font_size);
    for (int i = 0; i < 23; i++)
        sk_put32(b, s->theme[i]);
    sk_put32(b, s->has_theme);
    w->count[W_META] = 1;
}

static void splits(writer *w)
{
    const sk_skin *s = &w->f->skin;
    for (size_t i = 0; i < s->nsplits; i++) {
        sk_buf *b = &w->sec[W_SPLT];
        sk_put32(b, intern(w, s->splits[i].id));
        sk_put8(b, s->splits[i].edge);
        sk_put8(b, 0);
        sk_put16(b, 0);
        sk_putf32(b, s->splits[i].min);
        sk_putf32(b, s->splits[i].max);
        w->count[W_SPLT]++;
    }
    for (size_t i = 0; i < w->f->spans.len; i++) {
        sk_put32(&w->sec[W_SPAN], intern(w, w->f->spans.ptr[i]));
        w->count[W_SPAN]++;
    }
}

/* RECT: the panels' in order, then each widget's and its toggle's. */
static void rects(writer *w)
{
    const sk_skin *s = &w->f->skin;
    for (size_t i = 0; i < s->npanels; i++) {
        put_rect(&w->sec[W_RECT], &s->panels[i].rect);
        w->count[W_RECT]++;
    }
    for (size_t i = 0; i < s->nwidgets; i++) {
        w->widget_rect[i] = w->count[W_RECT]++;
        put_rect(&w->sec[W_RECT], &s->widgets[i].rect);
        w->toggle_rect[i] = SK_PACK_NONE;
        if (s->widgets[i].has_toggle) {
            w->toggle_rect[i] = w->count[W_RECT]++;
            put_rect(&w->sec[W_RECT], &s->widgets[i].toggle_rect);
        }
    }
}

static void widgets(writer *w)
{
    const sk_skin *s = &w->f->skin;
    for (size_t i = 0; i < s->nwidgets; i++) {
        const sk_pack_widget *x = &s->widgets[i].w;
        sk_buf *b = &w->sec[W_WIDG];
        sk_put8(b, x->type);
        sk_put8(b, x->format);
        sk_put8(b, x->digits);
        sk_put8(b, (unsigned)(x->has_on | x->pulse << 1 | x->vertical << 2 | x->column << 3 | x->run << 4 |
                              x->needle_meters << 5 | x->across << 6));
        const char *str[9] = {x->id,     x->bind,          x->label,          x->prefix,         x->suffix,
                              x->resets, x->fallback_bind, x->fallback_label, x->fallback_resets};
        for (int k = 0; k < 9; k++)
            sk_put32(b, intern(w, str[k]));
        const float num[13] = {x->min, x->max,      x->damping, x->start_angle, x->end_angle, x->thickness, x->gap,
                               x->on,  x->pulse_ms, x->blink,   x->fps,         x->frame_w,   x->frame_h};
        for (int k = 0; k < 13; k++)
            sk_putf32(b, num[k]);
        sk_put16(b, x->ticks);
        sk_put16(b, x->segments);
        sk_put16(b, x->frames);
        sk_put16(b, x->rows);
        sk_put32(b, w->count[W_MARK]);
        sk_put16(b, x->nmarks);
        sk_put16(b, 0);
        sk_put32(b, w->widget_el[i]);
        sk_put32(b, w->toggle_el[i]);
        sk_put32(b, w->face[i]);
        sk_put32(b, w->image[i]);
        sk_put32(b, w->sheet[i]);
        w->count[W_WIDG]++;
        for (uint16_t k = 0; k < x->nmarks; k++) {
            sk_buf *m = &w->sec[W_MARK];
            sk_put32(m, intern(w, x->marks[k].bind));
            sk_putf32(m, x->marks[k].value);
            sk_put32(m, intern(w, x->marks[k].klass));
            w->count[W_MARK]++;
        }
    }
}

/* ---- The tree ---- */

static uint32_t index_of(const sk_el *e)
{
    return e ? e->index : SK_PACK_NONE;
}

/* The meter e is part of, or NULL. */
static const sk_el *meter_of(const sk_el *e)
{
    for (; e; e = e->parent)
        if (e->group)
            return e;
    return NULL;
}

static uint32_t rect_index(const writer *w, int rect)
{
    if (rect == 0)
        return SK_PACK_NONE;
    if (rect >= 1000000)
        return w->toggle_rect[rect - 1000000];
    if (rect < 0)
        return w->widget_rect[-rect - 1];
    return (uint32_t)(rect - 1);
}

static void elements(writer *w)
{
    const sk_front *f = w->f;
    if (f->els.len > SK_PACK_MAX_ELEMENTS) {
        sk_diag(w->d, css_file(w), "(pack)", "%zu elements, more than a pack's %u", f->els.len,
                SK_PACK_MAX_ELEMENTS);
        w->failed = true;
        return;
    }
    for (size_t i = 0; i < f->els.len; i++) {
        const sk_el *e = f->els.ptr[i];
        const sk_el *p = e->parent;
        /* ::before first among its parent's children, ::after last. */
        const sk_el *first = e->before ? e->before : e->first ? e->first : e->after;
        const sk_el *next = NULL;
        if (e->kind == SK_EL_BEFORE)
            next = p->first ? p->first : p->after;
        else if (e->kind != SK_EL_AFTER)
            next = e->next ? e->next : p ? p->after : NULL;
        const sk_el *m = meter_of(e);
        uint32_t svg = SK_PACK_NONE;
        if (e->svg.kind != SK_SVG_NONE) {
            sk_pack_svg g = e->svg;
            if (e->widget >= 0 && e->role == SK_ROLE_FACE)
                g.image = w->face[e->widget];
            if (e->widget >= 0 && e->role == SK_ROLE_KNOB_IMAGE)
                g.image = w->image[e->widget];
            sk_buf *s = &w->sec[W_SVGG];
            sk_put8(s, g.kind);
            sk_put8(s, g.flag);
            sk_put16(s, 0);
            for (int k = 0; k < 5; k++)
                sk_putf32(s, g.v[k]);
            sk_put32(s, g.image);
            svg = w->count[W_SVGG]++;
        }
        sk_buf *b = &w->sec[W_ELEM];
        sk_put32(b, index_of(p));
        sk_put32(b, index_of(first));
        sk_put32(b, index_of(next));
        sk_put8(b, e->kind);
        sk_put8(b, e->role);
        sk_put8(b, m ? m->group : 0);
        sk_put8(b, m ? m->meter : 0);
        sk_put32(b, e->widget >= 0 ? (uint32_t)e->widget : SK_PACK_NONE);
        sk_put32(b, rect_index(w, e->rect));
        sk_put32(b, svg);
        sk_put8(b, e->text_source);
        sk_put8(b, e->ninputs);
        sk_put16(b, 0);
        sk_put32(b, intern(w, e->text));
        sk_put32(b, w->count[W_INPT]);
        sk_put32(b, w->count[W_STAB]);
        sk_put32(b, 0); /* the layers: none until the layer pass */
        sk_put32(b, 0);
        w->count[W_ELEM]++;
        for (uint8_t k = 0; k < e->ninputs; k++) {
            sk_buf *in = &w->sec[W_INPT];
            sk_put32(in, e->inputs[k].owner->index);
            sk_put8(in, e->inputs[k].var);
            sk_put8(in, e->inputs[k].radix);
            sk_put16(in, 0);
            w->count[W_INPT]++;
        }
        for (size_t k = 0; k < e->ntable; k++) {
            sk_put32(&w->sec[W_STAB], w->style_at[e->table[k]]);
            w->count[W_STAB]++;
        }
    }
}

/* ---- Styles and keyframes ---- */

static void say_prop(writer *w, uint16_t id, const char *fmt, ...) SK_PRINTF(3, 4);

static void say_prop(writer *w, uint16_t id, const char *fmt, ...)
{
    w->failed = true;
    if (w->said[id])
        return;
    w->said[id] = true;
    char why[300];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, sizeof why, fmt, ap);
    va_end(ap);
    const sk_propdef *d = sk_prop_def(id);
    sk_diag(w->d, css_file(w), "(pack)", "a style's \"%s\" %s", d ? d->name : "?", why);
}

/* One style: the properties whose payload differs from their initial
 * value's, in increasing id. */
static void style(writer *w, const sk_css_style *s, sk_buf *out)
{
    sk_css_style init;
    sk_style_initial(&init, s->color);
    sk_buf props = {0};
    unsigned n = 0;
    uint32_t nfaces = SK_PACK_EMBEDDED_FACES + (uint32_t)w->f->skin.nfonts;
    for (uint16_t id = 1; id < SK_PROP_COUNT; id++) {
        sk_buf a = {0}, b = {0};
        bool held = sk_pack_put_prop(&a, id, s, probe, w);
        if (held)
            sk_pack_put_prop(&b, id, &init, probe, w);
        bool same = !held || (a.len == b.len && memcmp(a.ptr, b.ptr, a.len) == 0);
        sk_buf_free(&a);
        sk_buf_free(&b);
        if (same)
            continue;
        sk_buf v = {0};
        sk_pack_put_prop(&v, id, s, intern, w);
        char why[256];
        if (v.len > 0xffff)
            say_prop(w, id, "takes %zu bytes, more than a property's 65535", v.len);
        else if (!sk_pack_check_prop(id, (const uint8_t *)v.ptr, v.len, w->strs.ptr, w->strs.len,
                                     (uint32_t)w->images.len, (uint32_t)w->f->keyframes.len, nfaces, why,
                                     sizeof why))
            say_prop(w, id, "holds what a pack cannot: %s", why);
        sk_put16(&props, id);
        sk_put16(&props, (unsigned)SK_MIN(v.len, 0xffff));
        sk_buf_append(&props, v.ptr, SK_MIN(v.len, 0xffff));
        sk_buf_free(&v);
        n++;
    }
    sk_put16(out, n);
    if (props.len)
        sk_buf_append(out, props.ptr, props.len);
    sk_buf_free(&props);
}

/* A section of count records bound by count + 1 offsets. */
static void bounded(sk_buf *out, const sk_buf *body, const uint32_t *at, size_t count)
{
    uint32_t head = (uint32_t)(count + 1) * 4;
    for (size_t i = 0; i < count; i++)
        sk_put32(out, head + at[i]);
    sk_put32(out, head + (uint32_t)body->len);
    if (body->len)
        sk_buf_append(out, body->ptr, body->len);
}

static void styles(writer *w)
{
    const sk_front *f = w->f;
    sk_buf body = {0};
    uint32_t *at = sk_xcalloc(w->kept.len + 1, sizeof *at);
    for (size_t i = 0; i < w->kept.len; i++) {
        at[i] = (uint32_t)body.len;
        style(w, f->styles.ptr[w->kept.ptr[i]], &body);
    }
    bounded(&w->sec[W_STYL], &body, at, w->kept.len);
    w->count[W_STYL] = (uint32_t)w->kept.len;
    free(at);
    sk_buf_free(&body);
}

static void keyframes(writer *w)
{
    const sk_front *f = w->f;
    sk_buf body = {0};
    uint32_t *at = sk_xcalloc(f->keyframes.len + 1, sizeof *at);
    for (size_t i = 0; i < f->keyframes.len; i++) {
        const sk_pack_keyframes *t = &f->keyframes.ptr[i];
        at[i] = (uint32_t)body.len;
        if (t->nkeys > SK_PACK_MAX_LIST) {
            sk_diag(w->d, css_file(w), "(pack)", "@keyframes %s has %u keyframes, more than a pack's %u", t->name,
                    (unsigned)t->nkeys, SK_PACK_MAX_LIST);
            w->failed = true;
        }
        sk_put32(&body, intern(w, t->name));
        sk_put16(&body, t->nkeys);
        for (uint16_t k = 0; k < t->nkeys; k++)
            sk_pack_put_key(&body, &t->keys[k]);
    }
    bounded(&w->sec[W_KEYF], &body, at, f->keyframes.len);
    w->count[W_KEYF] = (uint32_t)f->keyframes.len;
    free(at);
    sk_buf_free(&body);
}

/* ---- Layers ---- */

/* The layer pass goes here, once the layout and the paint are in the
 * library (src/skin/layout*, paint*): lay the stage out at the skin's
 * size, paint at 1x and 2x what decision 0037 pre-renders (STATIC,
 * STATE, SPRITE, NINE and MASK layers) into BLOB, give each element its
 * first layer and count in ELEM, and draw the SVGs whose size the
 * layout decides. Until then LAYR is empty and no element has a
 * layer: the runtime paints everything from the styles. */
static void layers(writer *w)
{
    w->count[W_LAYR] = 0;
}

/* ---- Fonts and sounds ---- */

static void fonts(writer *w)
{
    const sk_skin *s = &w->f->skin;
    for (size_t i = 0; i < s->nfonts; i++) {
        size_t n = 0;
        uint8_t *bytes = skin_file(w, s->fonts[i].file, &n);
        char why[200];
        if (!bytes) {
            fail_file(w, s->fonts[i].file, "cannot be read");
            continue;
        }
        if (!sk_font_check(bytes, n, why, sizeof why)) {
            fail_file(w, s->fonts[i].file, why);
            free(bytes);
            continue;
        }
        sk_buf *b = &w->sec[W_FONT];
        sk_put32(b, intern(w, s->fonts[i].family));
        sk_put16(b, s->fonts[i].weight);
        sk_put8(b, s->fonts[i].style);
        sk_put8(b, 0);
        sk_put32(b, blob_add(w, bytes, n));
        sk_put32(b, (uint32_t)n);
        w->count[W_FONT]++;
        free(bytes);
    }
}

static void sounds(writer *w)
{
    const sk_skin *s = &w->f->skin;
    for (size_t i = 0; i < s->nsounds; i++) {
        const sk_skin_sound *x = &s->sounds[i];
        size_t n = 0;
        uint8_t *bytes = skin_file(w, x->file, &n);
        if (!bytes) {
            fail_file(w, x->file, "cannot be read");
            continue;
        }
        sk_sound snd;
        sk_buf why = {0};
        bool ok = sk_media_sound(bytes, n, 0, &snd, &why);
        free(bytes);
        if (!ok) {
            fail_file(w, x->file, sk_buf_cstr(&why));
            sk_buf_free(&why);
            continue;
        }
        sk_buf_free(&why);
        size_t len = 0;
        uint8_t *adpcm = sk_media_adpcm(&snd, SK_ADPCM_BLOCK, &len);
        sk_buf *b = &w->sec[W_SNDS];
        sk_put32(b, intern(w, x->file));
        sk_put8(b, x->mode);
        sk_put8(b, 0);
        sk_put16(b, SK_ADPCM_BLOCK);
        sk_putf32(b, x->volume);
        /* A loop has no interval, whatever the model left there. */
        sk_putf32(b, x->mode == SK_SOUND_RANDOM ? x->interval[0] : 0);
        sk_putf32(b, x->mode == SK_SOUND_RANDOM ? x->interval[1] : 0);
        sk_put32(b, snd.rate);
        sk_put32(b, (uint32_t)snd.frames);
        sk_put32(b, adpcm ? blob_add(w, adpcm, len) : 0);
        sk_put32(b, (uint32_t)len);
        w->count[W_SNDS]++;
        free(adpcm);
        sk_sound_free(&snd);
    }
}

/* ---- The file ---- */

static void assemble(writer *w, const unsigned char hash[32], sk_buf *out)
{
    w->sec[W_STRS] = w->strs;
    w->count[W_STRS] = w->nstrs;
    w->sec[W_BLOB] = w->blob;
    uint32_t at[W_COUNT];
    uint64_t pos = SK_PACK_HEADER_SIZE + W_COUNT * SK_PACK_ENTRY_SIZE;
    for (int i = 0; i < W_COUNT; i++) {
        pos = (pos + 3) & ~(uint64_t)3;
        at[i] = (uint32_t)pos;
        pos += w->sec[i].len;
    }
    size_t base = out->len;
    sk_buf_append(out, SK_PACK_MAGIC, 8);
    sk_put16(out, SK_PACK_MAJOR);
    sk_put16(out, SK_PACK_MINOR);
    sk_put32(out, SK_PACK_HEADER_SIZE);
    sk_put64(out, pos);
    sk_put32(out, W_COUNT);
    sk_put32(out, 0);
    sk_buf_append(out, hash, 32);
    for (int i = 0; i < W_COUNT; i++) {
        sk_put32(out, IDS[i]);
        sk_put32(out, at[i]);
        sk_put32(out, (uint32_t)w->sec[i].len);
        sk_put32(out, w->count[i]);
    }
    for (int i = 0; i < W_COUNT; i++) {
        while (out->len - base < at[i])
            sk_put8(out, 0);
        if (w->sec[i].len)
            sk_buf_append(out, w->sec[i].ptr, w->sec[i].len);
    }
    w->sec[W_STRS] = (sk_buf){0};
    w->sec[W_BLOB] = (sk_buf){0};
}

void sk_pack_print_sections(const uint8_t *p, size_t n, FILE *out)
{
    if (n < SK_PACK_HEADER_SIZE)
        return;
    uint32_t hsize = sk_le32(p + 12), nsect = sk_le32(p + 24);
    fprintf(out, "pack: %zu bytes, format %u.%u, %u sections\n", n, (unsigned)sk_le16(p + 8),
            (unsigned)sk_le16(p + 10), (unsigned)nsect);
    for (uint32_t i = 0; i < nsect && hsize + (uint64_t)(i + 1) * SK_PACK_ENTRY_SIZE <= n; i++) {
        const uint8_t *e = p + hsize + (size_t)i * SK_PACK_ENTRY_SIZE;
        fprintf(out, "  %.4s %10u bytes %7u records\n", (const char *)e, (unsigned)sk_le32(e + 8),
                (unsigned)sk_le32(e + 12));
    }
}

bool sk_pack_write(const sk_front *f, const unsigned char hash[32], sk_buf *out, sk_diags *d)
{
    writer w;
    memset(&w, 0, sizeof w);
    w.f = f;
    w.d = d;
    sk_map_init(&w.pool);
    sk_map_init(&w.probe);
    const sk_skin *s = &f->skin;
    size_t nw = s->nwidgets ? s->nwidgets : 1;
    w.widget_rect = sk_xcalloc(nw, sizeof *w.widget_rect);
    w.toggle_rect = sk_xcalloc(nw, sizeof *w.toggle_rect);
    w.widget_el = sk_xcalloc(nw, sizeof *w.widget_el);
    w.toggle_el = sk_xcalloc(nw, sizeof *w.toggle_el);
    w.face = sk_xcalloc(nw, sizeof *w.face);
    w.image = sk_xcalloc(nw, sizeof *w.image);
    w.sheet = sk_xcalloc(nw, sizeof *w.sheet);
    for (size_t i = 0; i < f->images.len; i++) {
        wimage im;
        memset(&im, 0, sizeof im);
        im.path = f->images.ptr[i].path;
        im.real = f->images.ptr[i].real;
        sk_vec_push(&w.images, im);
    }
    w.menu[0] = image_of(&w, s->menu_image);
    w.menu[1] = image_of(&w, s->menu_hover);
    w.menu[2] = image_of(&w, s->menu_pressed);
    for (size_t i = 0; i < s->nwidgets; i++) {
        w.face[i] = image_of(&w, s->widgets[i].face);
        w.image[i] = image_of(&w, s->widgets[i].image);
        w.sheet[i] = image_of(&w, s->widgets[i].sheet);
        w.widget_el[i] = w.toggle_el[i] = SK_PACK_NONE;
    }
    w.style_at = sk_xcalloc(f->styles.len + 1, sizeof *w.style_at);
    for (size_t i = 0; i < f->els.len; i++) {
        const sk_el *e = f->els.ptr[i];
        if (e->rect < 0 && e->role == SK_ROLE_WIDGET)
            w.widget_el[-e->rect - 1] = e->index;
        else if (e->rect >= 1000000)
            w.toggle_el[e->rect - 1000000] = e->index;
        for (size_t k = 0; k < e->ntable; k++)
            w.style_at[e->table[k]] = 1;
    }
    for (size_t i = 0; i < f->styles.len; i++) {
        if (!w.style_at[i]) {
            w.style_at[i] = SK_PACK_NONE;
            continue;
        }
        w.style_at[i] = (uint32_t)w.kept.len;
        sk_vec_push(&w.kept, (uint32_t)i);
    }
    decode_images(&w);
    svg_sizes(&w);
    meta(&w);
    splits(&w);
    rects(&w);
    widgets(&w);
    elements(&w);
    styles(&w);
    keyframes(&w);
    images(&w);
    layers(&w);
    fonts(&w);
    sounds(&w);
    if (w.blob.len > SK_PACK_MAX_BYTES) {
        sk_diag(d, s->dir, "(pack)", "%zu bytes of images, fonts and sounds, more than a pack's %u", w.blob.len,
                SK_PACK_MAX_BYTES);
        w.failed = true;
    }
    size_t start = out->len;
    if (!w.failed) {
        assemble(&w, hash, out);
        /* The pack must pass the loader every app runs on it. */
        char why[300];
        sk_pack p;
        if (!sk_pack_load((const uint8_t *)out->ptr + start, out->len - start, &p, why, sizeof why)) {
            sk_diag(d, s->dir, "(pack)", "the pack skinc wrote fails the loader: %s", why);
            w.failed = true;
        }
        sk_pack_free(&p);
    }
    for (size_t i = 0; i < w.images.len; i++) {
        sk_svg_free(w.images.ptr[i].svg);
        sk_image_free(&w.images.ptr[i].raster);
        sk_vec_free(&w.images.ptr[i].sizes);
        free(w.images.ptr[i].owned);
    }
    sk_vec_free(&w.images);
    for (int i = 0; i < W_COUNT; i++)
        sk_buf_free(&w.sec[i]);
    sk_buf_free(&w.strs);
    sk_buf_free(&w.blob);
    sk_map_free(&w.pool);
    sk_map_free(&w.probe);
    free(w.widget_rect);
    free(w.toggle_rect);
    free(w.widget_el);
    free(w.toggle_el);
    free(w.face);
    free(w.image);
    free(w.sheet);
    free(w.style_at);
    sk_vec_free(&w.kept);
    return !w.failed;
}
