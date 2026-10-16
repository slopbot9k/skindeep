#include "internal.h"

#include <math.h>
#include <string.h>

/* The largest em the cache rasterizes; a larger one draws nothing. */
#define MAX_PX 2048.0f
/* Synthetic italic moves a point right by this much of its height above
 * the baseline. */
#define SLANT 0.2f

typedef struct node {
    const sk_font *font;
    uint32_t glyph;
    int32_t size; /* the em in 1/64 pixel */
    uint8_t subpixel, synth;
    sk_glyph g;
    size_t bytes;
    struct node *prev, *next; /* most recently used first */
    struct node *chain;       /* the next in its bucket */
} node;

struct sk_glyph_cache {
    size_t max, used, count;
    node **buckets;
    size_t nbuckets; /* a power of two */
    node *head, *tail;
};

static size_t hash(const sk_font *f, uint32_t glyph, int32_t size, int subpixel, int synth)
{
    uint64_t h = (uint64_t)(uintptr_t)f * 0x9e3779b97f4a7c15ULL;
    h ^= ((uint64_t)glyph << 24 | (uint64_t)(uint32_t)size << 4 | (uint64_t)(subpixel << 2 | synth)) +
         0x632be59bd9b4e019ULL + (h << 6) + (h >> 2);
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    return (size_t)h;
}

sk_glyph_cache *sk_glyph_cache_new(size_t max_bytes)
{
    sk_glyph_cache *c = sk_xcalloc(1, sizeof *c);
    c->max = max_bytes;
    c->nbuckets = 256;
    c->buckets = sk_xcalloc(c->nbuckets, sizeof *c->buckets);
    return c;
}

static void free_node(node *n)
{
    free((void *)n->g.mask.a);
    free(n);
}

void sk_glyph_cache_free(sk_glyph_cache *c)
{
    if (!c)
        return;
    for (node *n = c->head, *next; n; n = next) {
        next = n->next;
        free_node(n);
    }
    free(c->buckets);
    free(c);
}

static void unlink_lru(sk_glyph_cache *c, node *n)
{
    if (n->prev)
        n->prev->next = n->next;
    else
        c->head = n->next;
    if (n->next)
        n->next->prev = n->prev;
    else
        c->tail = n->prev;
    n->prev = n->next = NULL;
}

static void push_front(sk_glyph_cache *c, node *n)
{
    n->next = c->head;
    if (c->head)
        c->head->prev = n;
    c->head = n;
    if (!c->tail)
        c->tail = n;
}

static void grow(sk_glyph_cache *c)
{
    size_t nb = c->nbuckets * 2;
    node **b = sk_xcalloc(nb, sizeof *b);
    for (size_t i = 0; i < c->nbuckets; i++)
        for (node *n = c->buckets[i], *next; n; n = next) {
            next = n->chain;
            size_t k = hash(n->font, n->glyph, n->size, n->subpixel, n->synth) & (nb - 1);
            n->chain = b[k];
            b[k] = n;
        }
    free(c->buckets);
    c->buckets = b;
    c->nbuckets = nb;
}

static void evict(sk_glyph_cache *c, node *n)
{
    node **p = &c->buckets[hash(n->font, n->glyph, n->size, n->subpixel, n->synth) & (c->nbuckets - 1)];
    while (*p != n)
        p = &(*p)->chain;
    *p = n->chain;
    unlink_lru(c, n);
    c->used -= n->bytes;
    c->count--;
    free_node(n);
}

/* The glyph's outline slanted about the baseline, rasterized as stb
 * does an upright one: a box of whole pixels around the control points,
 * which hold the curves. */
static uint8_t *slanted(const sk_font *f, uint32_t glyph, float scale, float shift, int *w, int *h, int *x0, int *y0)
{
    stbtt_vertex *v = NULL;
    int n = stbtt_GetGlyphShape(&f->info, (int)glyph, &v);
    if (n <= 0) {
        stbtt_FreeShape(&f->info, v);
        return NULL;
    }
    int minx = INT32_MAX, miny = INT32_MAX, maxx = INT32_MIN, maxy = INT32_MIN;
#define SLANT_POINT(vx, vy)                                                  \
    do {                                                                     \
        long x_ = lroundf((float)(vx) + SLANT * (float)(vy));                \
        vx = (short)(x_ < -32768 ? -32768 : x_ > 32767 ? 32767 : x_);        \
        minx = SK_MIN(minx, (int)(vx));                                      \
        maxx = SK_MAX(maxx, (int)(vx));                                      \
        miny = SK_MIN(miny, (int)(vy));                                      \
        maxy = SK_MAX(maxy, (int)(vy));                                      \
    } while (0)
    for (int i = 0; i < n; i++) {
        SLANT_POINT(v[i].x, v[i].y);
        if (v[i].type == STBTT_vcurve || v[i].type == STBTT_vcubic)
            SLANT_POINT(v[i].cx, v[i].cy);
        if (v[i].type == STBTT_vcubic)
            SLANT_POINT(v[i].cx1, v[i].cy1);
    }
#undef SLANT_POINT
    *x0 = (int)floorf((float)minx * scale + shift);
    *y0 = (int)floorf((float)-maxy * scale);
    *w = (int)ceilf((float)maxx * scale + shift) - *x0;
    *h = (int)ceilf((float)-miny * scale) - *y0;
    uint8_t *bits = NULL;
    if (*w > 0 && *h > 0) {
        bits = sk_xcalloc((size_t)*w * (size_t)*h, 1);
        stbtt__bitmap bm = {*w, *h, *w, bits};
        stbtt_Rasterize(&bm, 0.35f, v, n, scale, scale, shift, 0, *x0, *y0, 1, f->info.userdata);
    }
    stbtt_FreeShape(&f->info, v);
    return bits;
}

/* Synthetic bold: each row spread right by d pixels, the last of them in
 * part. */
static uint8_t *embolden(const uint8_t *src, int w, int h, float d, int *out_w)
{
    int k = (int)d;
    int part = (int)lroundf((d - (float)k) * 256);
    int ow = w + k + (part > 0);
    uint8_t *dst = sk_xcalloc((size_t)ow * (size_t)h, 1);
    for (int y = 0; y < h; y++) {
        const uint8_t *s = src + (size_t)y * (size_t)w;
        uint8_t *o = dst + (size_t)y * (size_t)ow;
        for (int x = 0; x < ow; x++) {
            int m = 0;
            for (int j = 0; j <= k; j++)
                if (x - j >= 0 && x - j < w)
                    m = SK_MAX(m, s[x - j]);
            if (part && x - k - 1 >= 0 && x - k - 1 < w)
                m = SK_MAX(m, (s[x - k - 1] * part + 128) >> 8);
            o[x] = (uint8_t)m;
        }
    }
    *out_w = ow;
    return dst;
}

static void rasterize(node *n)
{
    const sk_font *f = n->font;
    float px = (float)n->size / 64.0f, scale = sk_text_scale(f, px), shift = (float)n->subpixel / 4.0f;
    int w = 0, h = 0, x0 = 0, y0 = 0;
    uint8_t *bits = NULL;
    if (stbtt_IsGlyphEmpty(&f->info, (int)n->glyph)) {
        bits = NULL;
    } else if (n->synth & SK_STYLE_ITALIC) {
        bits = slanted(f, n->glyph, scale, shift, &w, &h, &x0, &y0);
    } else {
        int x1, y1;
        stbtt_GetGlyphBitmapBoxSubpixel(&f->info, (int)n->glyph, scale, scale, shift, 0, &x0, &y0, &x1, &y1);
        w = x1 - x0;
        h = y1 - y0;
        if (w > 0 && h > 0) {
            bits = sk_xcalloc((size_t)w * (size_t)h, 1);
            stbtt_MakeGlyphBitmapSubpixel(&f->info, bits, w, h, w, scale, scale, shift, 0, (int)n->glyph);
        }
    }
    if (bits && (n->synth & SK_STYLE_BOLD)) {
        int ow;
        uint8_t *bold = embolden(bits, w, h, SK_MAX(1.0f, px / 16.0f), &ow);
        free(bits);
        bits = bold;
        w = ow;
    }
    if (!bits)
        w = h = x0 = y0 = 0;
    n->g.mask.a = bits;
    n->g.mask.w = w;
    n->g.mask.h = h;
    n->g.mask.stride = w;
    n->g.left = x0;
    n->g.top = y0;
    n->bytes = sizeof *n + (size_t)w * (size_t)h;
}

const sk_glyph *sk_glyph_get(sk_glyph_cache *c, sk_glyph_ref ref, float px, int subpixel)
{
    static const sk_glyph none;
    if (!ref.font || !(px > 0) || px > MAX_PX)
        return &none;
    int32_t size = (int32_t)lroundf(px * 64.0f);
    int sub = subpixel & 3, synth = ref.synth & 3;
    if (size <= 0 || ref.glyph >= (uint32_t)ref.font->info.numGlyphs)
        return &none;
    size_t k = hash(ref.font, ref.glyph, size, sub, synth) & (c->nbuckets - 1);
    for (node *n = c->buckets[k]; n; n = n->chain)
        if (n->font == ref.font && n->glyph == ref.glyph && n->size == size && n->subpixel == sub &&
            n->synth == synth) {
            if (c->head != n) {
                unlink_lru(c, n);
                push_front(c, n);
            }
            return &n->g;
        }
    node *n = sk_xcalloc(1, sizeof *n);
    n->font = ref.font;
    n->glyph = ref.glyph;
    n->size = size;
    n->subpixel = (uint8_t)sub;
    n->synth = (uint8_t)synth;
    rasterize(n);
    if (c->count >= c->nbuckets)
        grow(c);
    k = hash(n->font, n->glyph, size, sub, synth) & (c->nbuckets - 1);
    n->chain = c->buckets[k];
    c->buckets[k] = n;
    push_front(c, n);
    c->used += n->bytes;
    c->count++;
    /* The newest stays, however large: the caller holds it. */
    while (c->used > c->max && c->tail != n)
        evict(c, c->tail);
    return &n->g;
}

size_t sk_text_cache_bytes(const sk_glyph_cache *c)
{
    return c->used;
}

size_t sk_text_cache_count(const sk_glyph_cache *c)
{
    return c->count;
}
