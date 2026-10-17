/* The pack loader of decision 0037: a pack's bytes, which may come from
 * a cache directory like any other file, checked before anything reads
 * them, then held as sk_pack.
 *
 * Two passes run the same code. The first checks every check the
 * decision lists, never trusting a count before the section's size
 * bears it out, and counts the memory the second needs; the second
 * takes that memory in one allocation and fills it. Strings, fonts,
 * sounds and BLOB stay in the caller's bytes.
 *
 * Minor versions: a section this loader does not know is passed over,
 * a record larger than it knows is read for the fields it knows, and a
 * style's property it does not know is stepped over by its length. */
#include "packfmt.h"

#include "../audio/adpcm.h"
#include "../base/utf8.h"
#include "../text/fontcheck.h"
#include "../text/text.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>

_Static_assert(SK_PACK_EMBEDDED_FACES == SK_FONT_EMBEDDED_COUNT, "a style's faces start after the embedded ones");

/* ---- Sections ---- */

enum {
    S_STRS,
    S_META,
    S_SPLT,
    S_SPAN,
    S_RECT,
    S_WIDG,
    S_MARK,
    S_ELEM,
    S_SVGG,
    S_INPT,
    S_STAB,
    S_STYL,
    S_KEYF,
    S_IMGS,
    S_VARI,
    S_LAYR,
    S_FONT,
    S_SNDS,
    S_BLOB,
    S_COUNT,
};

/* Format 1.0's sections; rec is a fixed record's size, 0 for a section
 * of its own form. */
static const struct {
    uint32_t id;
    const char *name;
    uint32_t rec;
} SECTS[S_COUNT] = {
    [S_STRS] = {SK_SECT_STRS, "STRS", 0},           [S_META] = {SK_SECT_META, "META", SK_REC_META},
    [S_SPLT] = {SK_SECT_SPLT, "SPLT", SK_REC_SPLT}, [S_SPAN] = {SK_SECT_SPAN, "SPAN", SK_REC_SPAN},
    [S_RECT] = {SK_SECT_RECT, "RECT", SK_REC_RECT}, [S_WIDG] = {SK_SECT_WIDG, "WIDG", SK_REC_WIDG},
    [S_MARK] = {SK_SECT_MARK, "MARK", SK_REC_MARK}, [S_ELEM] = {SK_SECT_ELEM, "ELEM", SK_REC_ELEM},
    [S_SVGG] = {SK_SECT_SVGG, "SVGG", SK_REC_SVGG}, [S_INPT] = {SK_SECT_INPT, "INPT", SK_REC_INPT},
    [S_STAB] = {SK_SECT_STAB, "STAB", SK_REC_STAB}, [S_STYL] = {SK_SECT_STYL, "STYL", 0},
    [S_KEYF] = {SK_SECT_KEYF, "KEYF", 0},           [S_IMGS] = {SK_SECT_IMGS, "IMGS", SK_REC_IMGS},
    [S_VARI] = {SK_SECT_VARI, "VARI", SK_REC_VARI}, [S_LAYR] = {SK_SECT_LAYR, "LAYR", SK_REC_LAYR},
    [S_FONT] = {SK_SECT_FONT, "FONT", SK_REC_FONT}, [S_SNDS] = {SK_SECT_SNDS, "SNDS", SK_REC_SNDS},
    [S_BLOB] = {SK_SECT_BLOB, "BLOB", 0},
};

typedef struct {
    const uint8_t *p;
    uint32_t off, size, count;
    uint32_t rec; /* a fixed record's size in this pack, size / count */
    bool found;
} sect;

typedef struct {
    sk_packfmt f;
    sect s[S_COUNT];
    sk_pack *out; /* NULL in the first pass */
    /* What the second pass builds before the records that point in. */
    sk_pack_mark *marks;
    sk_pack_input *inputs;
    uint32_t *stab;
    sk_pack_variant *variants;
} loader;

/* ---- Messages, strings, memory ---- */

bool sk_packfmt_fail(sk_packfmt *c, const char *fmt, ...)
{
    if (c->failed)
        return false;
    c->failed = true;
    if (!c->err || !c->err_len)
        return false;
    char what[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(what, sizeof what, fmt, ap);
    va_end(ap);
    if (c->sect && c->has_rec)
        snprintf(c->err, c->err_len, "%s %u: %s", c->sect, (unsigned)c->rec, what);
    else if (c->sect)
        snprintf(c->err, c->err_len, "%s: %s", c->sect, what);
    else
        snprintf(c->err, c->err_len, "%s", what);
    return false;
}

bool sk_packfmt_str(sk_packfmt *c, uint32_t off, bool none_ok, const char *what, const char **out)
{
    *out = NULL;
    if (off == SK_PACK_NONE)
        return none_ok || sk_packfmt_fail(c, "%s is none", what);
    if (off >= c->nstrs)
        return sk_packfmt_fail(c, "%s at %u, past STRS's %u bytes", what, (unsigned)off, (unsigned)c->nstrs);
    /* STRS is valid UTF-8 and ends in a NUL, so a string that starts
     * on a character is both. */
    if (((unsigned char)c->strs[off] & 0xc0) == 0x80)
        return sk_packfmt_fail(c, "%s at %u starts inside a character", what, (unsigned)off);
    *out = c->strs + off;
    return true;
}

#define ALIGN ((uint64_t)_Alignof(max_align_t))

static void *grab(sk_packfmt *c, size_t count, size_t size, bool list)
{
    if (!count)
        return NULL;
    uint64_t n = ((uint64_t)count * size + ALIGN - 1) / ALIGN * ALIGN;
    uint64_t at = c->used;
    c->used += n;
    if (!c->mem) {
        if (list && n > SK_PACK_SCRATCH)
            sk_die("skin pack: a list of %zu bytes outgrows the scratch", (size_t)n);
        return list ? c->scratch : NULL;
    }
    if (c->used > c->cap)
        sk_die("skin pack: the loader's second pass needs more than its first counted");
    return c->mem + at;
}

void *sk_packfmt_fixed(sk_packfmt *c, size_t count, size_t size)
{
    return grab(c, count, size, false);
}

void *sk_packfmt_list(sk_packfmt *c, size_t count, size_t size)
{
    return grab(c, count, size, true);
}

/* ---- Checks ---- */

static void at_rec(loader *L, int s, uint32_t i)
{
    L->f.sect = SECTS[s].name;
    L->f.rec = i;
    L->f.has_rec = true;
}

static void at_sect(loader *L, int s)
{
    L->f.sect = s >= 0 ? SECTS[s].name : NULL;
    L->f.has_rec = false;
}

static const uint8_t *rec(const loader *L, int s, uint32_t i)
{
    return L->s[s].p + (size_t)i * L->s[s].rec;
}

static uint32_t count(const loader *L, int s)
{
    return L->s[s].count;
}

/* An index below n, or none when none_ok: "parent 9, past the 4 elements". */
static bool index_ok(loader *L, uint32_t v, uint32_t n, bool none_ok, const char *what, const char *of)
{
    if ((v == SK_PACK_NONE && none_ok) || v < n)
        return true;
    if (v == SK_PACK_NONE)
        return sk_packfmt_fail(&L->f, "%s is none", what);
    return sk_packfmt_fail(&L->f, "%s %u, past the %u %s", what, (unsigned)v, (unsigned)n, of);
}

/* A range of count items from first inside n. */
static bool range_ok(loader *L, uint32_t first, uint64_t n_items, uint32_t n, const char *what, const char *of)
{
    if ((uint64_t)first + n_items <= n)
        return true;
    return sk_packfmt_fail(&L->f, "%s %u to %llu, past the %u %s", what, (unsigned)first,
                           (unsigned long long)(first + n_items), (unsigned)n, of);
}

static bool finite_at(loader *L, const uint8_t *p, const char *what, float *out)
{
    *out = sk_lef32(p);
    return isfinite(*out) || sk_packfmt_fail(&L->f, "%s is not a finite number", what);
}

static bool str_at(loader *L, const uint8_t *p, bool none_ok, const char *what, const char **out)
{
    return sk_packfmt_str(&L->f, sk_le32(p), none_ok, what, out);
}

/* A blob of BLOB: its offset and length at p. */
static bool blob_at(loader *L, const uint8_t *p, uint32_t *off, uint32_t *len)
{
    *off = sk_le32(p);
    *len = sk_le32(p + 4);
    if (!*len)
        return sk_packfmt_fail(&L->f, "an empty blob");
    if ((uint64_t)*off + *len > L->s[S_BLOB].size)
        return sk_packfmt_fail(&L->f, "blob %u, %u bytes, past BLOB's %u", (unsigned)*off, (unsigned)*len,
                               (unsigned)L->s[S_BLOB].size);
    return true;
}

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* Whether n bytes are a QOI image of w x h whose operations make
 * exactly its pixels and end where its end marker starts, as the
 * encoder writes it. */
static bool qoi_ok(const uint8_t *p, size_t n, uint32_t w, uint32_t h)
{
    static const uint8_t END[8] = {0, 0, 0, 0, 0, 0, 0, 1};
    if (n < 14 + 8 || memcmp(p, "qoif", 4) != 0 || be32(p + 4) != w || be32(p + 8) != h)
        return false;
    if (p[12] < 3 || p[12] > 4 || p[13] > 1)
        return false;
    uint64_t want = (uint64_t)w * h, got = 0;
    size_t at = 14, end = n - 8;
    while (got < want) {
        if (at >= end)
            return false;
        uint8_t b = p[at++];
        if (b == 0xfe)
            at += 3; /* RGB */
        else if (b == 0xff)
            at += 4; /* RGBA */
        else if ((b & 0xc0) == 0x80)
            at += 1; /* LUMA */
        if ((b & 0xc0) == 0xc0 && b < 0xfe)
            got += (b & 0x3fu) + 1; /* RUN */
        else
            got++;
        if (at > end)
            return false;
    }
    return got == want && at == end && memcmp(p + end, END, 8) == 0;
}

/* A picture's size: 1 to SK_PACK_MAX_SIDE a side, SK_PACK_MAX_PIXELS in all. */
static bool size_ok(loader *L, uint32_t w, uint32_t h)
{
    if (w >= 1 && h >= 1 && w <= SK_PACK_MAX_SIDE && h <= SK_PACK_MAX_SIDE && (uint64_t)w * h <= SK_PACK_MAX_PIXELS)
        return true;
    return sk_packfmt_fail(&L->f, "%u x %u pixels: at most %u a side and %u in all", (unsigned)w, (unsigned)h,
                           SK_PACK_MAX_SIDE, SK_PACK_MAX_PIXELS);
}

static bool image_ok(loader *L, uint32_t off, uint32_t len, uint32_t w, uint32_t h)
{
    if (L->out)
        return true;
    return qoi_ok(L->s[S_BLOB].p + off, len, w, h) ||
           sk_packfmt_fail(&L->f, "its QOI image is not one of %u x %u pixels", (unsigned)w, (unsigned)h);
}

static bool rect_at(loader *L, const uint8_t *p, const char *what, sk_pack_rect *r)
{
    for (int i = 0; i < 6; i++)
        if (!finite_at(L, p + 4 * i, what, &r->v[i]))
            return false;
    r->has = p[24];
    if (r->has >= 64)
        return sk_packfmt_fail(&L->f, "%s's keys 0x%x: there are 6", what, (unsigned)r->has);
    for (int i = 0; i < 6; i++) {
        r->follow[i] = (int8_t)p[25 + i];
        if (r->follow[i] < -1 || (r->follow[i] >= 0 && (uint32_t)r->follow[i] >= count(L, S_SPLT)))
            return sk_packfmt_fail(&L->f, "%s follows split %d, past the %u splits", what, (int)r->follow[i],
                                   (unsigned)count(L, S_SPLT));
    }
    return true;
}

/* ---- The header and the table ---- */

static void name_of(uint32_t id, char out[16])
{
    char c[4] = {(char)(id & 0xff), (char)(id >> 8 & 0xff), (char)(id >> 16 & 0xff), (char)(id >> 24)};
    bool print = true;
    for (int i = 0; i < 4; i++)
        print = print && c[i] >= 0x20 && c[i] < 0x7f;
    if (print)
        snprintf(out, 16, "%.4s", c);
    else
        snprintf(out, 16, "0x%08x", (unsigned)id);
}

static bool header(loader *L)
{
    sk_packfmt *c = &L->f;
    const uint8_t *d = c->data;
    size_t n = c->n;
    at_sect(L, -1);
    if (n < SK_PACK_HEADER_SIZE)
        return sk_packfmt_fail(c, "%zu bytes, fewer than a pack's header", n);
    if (memcmp(d, SK_PACK_MAGIC, 8) != 0)
        return sk_packfmt_fail(c, "not a skin pack: it does not start SKINPACK");
    unsigned major = sk_le16(d + 8), minor = sk_le16(d + 10);
    if (major != SK_PACK_MAJOR)
        return sk_packfmt_fail(c, "format %u.%u: this loader reads %u.x", major, minor, SK_PACK_MAJOR);
    if (n > SK_PACK_MAX_BYTES)
        return sk_packfmt_fail(c, "%zu bytes, more than a pack's %u", n, SK_PACK_MAX_BYTES);
    uint64_t size = sk_le64(d + 16);
    if (size != n)
        return sk_packfmt_fail(c, "the header says %llu bytes, the pack has %zu", (unsigned long long)size, n);
    uint32_t hsize = sk_le32(d + 12), nsect = sk_le32(d + 24), flags = sk_le32(d + 28);
    if (hsize < SK_PACK_HEADER_SIZE || hsize % 4 || hsize > n)
        return sk_packfmt_fail(c, "a header of %u bytes", (unsigned)hsize);
    if (flags)
        return sk_packfmt_fail(c, "flags 0x%x: this loader knows none", (unsigned)flags);
    if (nsect > SK_PACK_MAX_SECTIONS)
        return sk_packfmt_fail(c, "%u sections, more than %u", (unsigned)nsect, SK_PACK_MAX_SECTIONS);
    uint64_t table_end = (uint64_t)hsize + (uint64_t)nsect * SK_PACK_ENTRY_SIZE;
    if (table_end > n)
        return sk_packfmt_fail(c, "its section table runs past its end");
    struct {
        uint32_t id, off, size;
    } all[SK_PACK_MAX_SECTIONS];
    for (uint32_t i = 0; i < nsect; i++) {
        const uint8_t *e = d + hsize + (size_t)i * SK_PACK_ENTRY_SIZE;
        uint32_t id = sk_le32(e), off = sk_le32(e + 4), sz = sk_le32(e + 8), cnt = sk_le32(e + 12);
        char name[16];
        name_of(id, name);
        if (off % 4)
            return sk_packfmt_fail(c, "section %s at %u is not 4-byte aligned", name, (unsigned)off);
        if (off < table_end || (uint64_t)off + sz > n)
            return sk_packfmt_fail(c, "section %s, %u bytes at %u, lies outside the pack's sections", name,
                                   (unsigned)sz, (unsigned)off);
        for (uint32_t k = 0; k < i; k++)
            if (all[k].id == id)
                return sk_packfmt_fail(c, "section %s twice", name);
        /* In order of offset, to find any two that overlap. */
        uint32_t k = i;
        while (k > 0 && all[k - 1].off > off) {
            all[k] = all[k - 1];
            k--;
        }
        all[k].id = id;
        all[k].off = off;
        all[k].size = sz;
        for (int s = 0; s < S_COUNT; s++)
            if (SECTS[s].id == id) {
                L->s[s] = (sect){d + off, off, sz, cnt, 0, true};
                break;
            }
    }
    for (uint32_t i = 1; i < nsect; i++)
        if ((uint64_t)all[i - 1].off + all[i - 1].size > all[i].off) {
            char a[16], b[16];
            name_of(all[i - 1].id, a);
            name_of(all[i].id, b);
            return sk_packfmt_fail(c, "sections %s and %s overlap", a, b);
        }
    for (int s = 0; s < S_COUNT; s++) {
        sect *x = &L->s[s];
        at_sect(L, s);
        if (!x->found)
            return sk_packfmt_fail(c, "missing");
        if (!SECTS[s].rec)
            continue;
        if (!x->count) {
            if (x->size)
                return sk_packfmt_fail(c, "no records in %u bytes", (unsigned)x->size);
            continue;
        }
        if ((uint64_t)x->count * SECTS[s].rec > x->size || x->size % x->count)
            return sk_packfmt_fail(c, "%u records of at least %u bytes do not fill its %u", (unsigned)x->count,
                                   (unsigned)SECTS[s].rec, (unsigned)x->size);
        x->rec = x->size / x->count;
    }
    return true;
}

/* ---- The sections ---- */

static bool strs(loader *L)
{
    sect *s = &L->s[S_STRS];
    at_sect(L, S_STRS);
    const char *p = (const char *)s->p;
    if (s->size && p[s->size - 1] != 0)
        return sk_packfmt_fail(&L->f, "its last string is not terminated");
    if (!L->out) {
        if (!sk_utf8_valid(p, s->size))
            return sk_packfmt_fail(&L->f, "not valid UTF-8");
        uint32_t nuls = 0;
        for (uint32_t i = 0; i < s->size; i++)
            nuls += p[i] == 0;
        if (nuls != s->count)
            return sk_packfmt_fail(&L->f, "%u strings, its count says %u", (unsigned)nuls, (unsigned)s->count);
    }
    L->f.strs = p;
    L->f.nstrs = s->size;
    return true;
}

static bool splits(loader *L)
{
    uint32_t n = count(L, S_SPLT);
    sk_pack_split *out = sk_packfmt_fixed(&L->f, n, sizeof *out);
    for (uint32_t i = 0; i < n; i++) {
        at_rec(L, S_SPLT, i);
        const uint8_t *p = rec(L, S_SPLT, i);
        sk_pack_split x;
        if (!str_at(L, p, false, "its id", &x.id) || !finite_at(L, p + 8, "its min", &x.min) ||
            !finite_at(L, p + 12, "its max", &x.max))
            return false;
        x.edge = p[4];
        if (x.edge > SK_SPLIT_BOTTOM)
            return sk_packfmt_fail(&L->f, "edge %u is none", (unsigned)x.edge);
        if (out)
            out[i] = x;
    }
    if (L->out) {
        L->out->splits = out;
        L->out->nsplits = n;
    }
    return true;
}

static bool spans(loader *L)
{
    uint32_t n = count(L, S_SPAN);
    at_sect(L, S_SPAN);
    /* A meter's span variable has a value for each and two more. */
    if (n > 253)
        return sk_packfmt_fail(&L->f, "%u span classes, more than 253", (unsigned)n);
    const char **out = sk_packfmt_fixed(&L->f, n, sizeof *out);
    for (uint32_t i = 0; i < n; i++) {
        at_rec(L, S_SPAN, i);
        const char *s;
        if (!str_at(L, rec(L, S_SPAN, i), false, "its class", &s))
            return false;
        if (out)
            out[i] = s;
    }
    if (L->out) {
        L->out->spans = out;
        L->out->nspans = n;
    }
    return true;
}

static bool rects(loader *L)
{
    uint32_t n = count(L, S_RECT);
    sk_pack_rect *out = sk_packfmt_fixed(&L->f, n, sizeof *out);
    for (uint32_t i = 0; i < n; i++) {
        at_rec(L, S_RECT, i);
        sk_pack_rect r;
        if (!rect_at(L, rec(L, S_RECT, i), "the rect", &r))
            return false;
        if (out)
            out[i] = r;
    }
    if (L->out) {
        L->out->rects = out;
        L->out->nrects = n;
    }
    return true;
}

static bool variants(loader *L)
{
    uint32_t n = count(L, S_VARI);
    sk_pack_variant *out = sk_packfmt_fixed(&L->f, n, sizeof *out);
    for (uint32_t i = 0; i < n; i++) {
        at_rec(L, S_VARI, i);
        const uint8_t *p = rec(L, S_VARI, i);
        sk_pack_variant v;
        v.w = sk_le16(p);
        v.h = sk_le16(p + 2);
        v.scale = p[4];
        if (v.scale > 2)
            return sk_packfmt_fail(&L->f, "scale %u: 0, 1 or 2", (unsigned)v.scale);
        if (!size_ok(L, v.w, v.h) || !blob_at(L, p + 8, &v.blob, &v.len) || !image_ok(L, v.blob, v.len, v.w, v.h))
            return false;
        if (out)
            out[i] = v;
    }
    L->variants = out;
    return true;
}

static bool images(loader *L)
{
    uint32_t n = count(L, S_IMGS);
    at_sect(L, S_IMGS);
    if (n > SK_PACK_MAX_IMAGES)
        return sk_packfmt_fail(&L->f, "%u images, more than %u", (unsigned)n, SK_PACK_MAX_IMAGES);
    sk_pack_image *out = sk_packfmt_fixed(&L->f, n, sizeof *out);
    for (uint32_t i = 0; i < n; i++) {
        at_rec(L, S_IMGS, i);
        const uint8_t *p = rec(L, S_IMGS, i);
        sk_pack_image im;
        uint32_t first = sk_le32(p + 8);
        im.w = sk_le16(p + 4);
        im.h = sk_le16(p + 6);
        im.nvariants = sk_le16(p + 12);
        if (!str_at(L, p, false, "its path", &im.path) || !size_ok(L, im.w, im.h) ||
            !range_ok(L, first, im.nvariants, count(L, S_VARI), "variants", "variants"))
            return false;
        for (uint16_t k = 0; k < im.nvariants; k++) {
            const uint8_t *v = rec(L, S_VARI, first + k);
            if (v[4] == 0 && (sk_le16(v) != im.w || sk_le16(v + 2) != im.h))
                return sk_packfmt_fail(&L->f, "variant %u is its own pixels at %u x %u, not %u x %u",
                                       (unsigned)(first + k), (unsigned)sk_le16(v), (unsigned)sk_le16(v + 2),
                                       (unsigned)im.w, (unsigned)im.h);
        }
        im.variants = L->variants ? L->variants + first : NULL;
        if (!im.nvariants)
            im.variants = NULL;
        if (out)
            out[i] = im;
    }
    if (L->out) {
        L->out->images = out;
        L->out->nimages = n;
    }
    return true;
}

static bool meta(loader *L)
{
    at_sect(L, S_META);
    if (count(L, S_META) != 1)
        return sk_packfmt_fail(&L->f, "%u records, not 1", (unsigned)count(L, S_META));
    at_rec(L, S_META, 0);
    const uint8_t *p = rec(L, S_META, 0);
    sk_pack_meta m;
    memset(&m, 0, sizeof m);
    if (!str_at(L, p, false, "the name", &m.name) || !str_at(L, p + 4, true, "the version", &m.version) ||
        !str_at(L, p + 8, true, "the author", &m.author) ||
        !str_at(L, p + 12, true, "the description", &m.description) ||
        !str_at(L, p + 16, true, "the licence", &m.license) || !finite_at(L, p + 20, "the width", &m.width) ||
        !finite_at(L, p + 24, "the height", &m.height) || !finite_at(L, p + 28, "the min width", &m.min_width) ||
        !finite_at(L, p + 32, "the min height", &m.min_height) || !rect_at(L, p + 36, "the menu", &m.menu_rect))
        return false;
    m.menu_image = sk_le32(p + 68);
    m.menu_hover = sk_le32(p + 72);
    m.menu_pressed = sk_le32(p + 76);
    if (!index_ok(L, m.menu_image, count(L, S_IMGS), true, "the menu's image", "images") ||
        !index_ok(L, m.menu_hover, count(L, S_IMGS), true, "the menu's hover image", "images") ||
        !index_ok(L, m.menu_pressed, count(L, S_IMGS), true, "the menu's pressed image", "images") ||
        !rect_at(L, p + 80, "the terminal", &m.terminal_rect))
        return false;
    m.terminal_font = sk_le32(p + 112);
    if (!index_ok(L, m.terminal_font, count(L, S_FONT), true, "the terminal's font", "fonts") ||
        !finite_at(L, p + 116, "the terminal's font size", &m.terminal_font_size))
        return false;
    /* skinc's window sizes: a hostile pack's would reach past any
     * integer once scaled, where the view rounds the stage to pixels. */
    const float sizes[4] = {m.width, m.height, m.min_width, m.min_height};
    static const char *const size_names[4] = {"width", "height", "min width", "min height"};
    for (int i = 0; i < 4; i++)
        if (!(sizes[i] >= 0 && sizes[i] <= SK_PACK_MAX_SIDE))
            return sk_packfmt_fail(&L->f, "a %s of %g: at most %u pixels", size_names[i], (double)sizes[i],
                                   SK_PACK_MAX_SIDE);
    if (!(m.terminal_font_size > 0 && m.terminal_font_size <= SK_PACK_MAX_FLOAT))
        return sk_packfmt_fail(&L->f, "a terminal font size of %g", (double)m.terminal_font_size);
    for (int i = 0; i < 23; i++)
        m.theme[i] = sk_le32(p + 120 + 4 * i);
    m.has_theme = sk_le32(p + 212);
    if (m.has_theme >> 23)
        return sk_packfmt_fail(&L->f, "theme bits 0x%x: there are 23 colours", (unsigned)m.has_theme);
    if (L->out)
        L->out->meta = m;
    return true;
}

static bool marks(loader *L)
{
    uint32_t n = count(L, S_MARK);
    sk_pack_mark *out = sk_packfmt_fixed(&L->f, n, sizeof *out);
    for (uint32_t i = 0; i < n; i++) {
        at_rec(L, S_MARK, i);
        const uint8_t *p = rec(L, S_MARK, i);
        sk_pack_mark m;
        if (!str_at(L, p, true, "its bind", &m.bind) || !finite_at(L, p + 4, "its value", &m.value) ||
            !str_at(L, p + 8, true, "its class", &m.klass))
            return false;
        if (out)
            out[i] = m;
    }
    L->marks = out;
    return true;
}

static uint8_t radix_of(const loader *L, uint8_t var)
{
    switch (var) {
    case SK_VAR_LIST:
    case SK_VAR_ITEM:
        return 3;
    case SK_VAR_COUNT:
        return SK_PACK_MAX_LIMITS + 1;
    case SK_VAR_SPAN:
        return (uint8_t)(count(L, S_SPAN) + 2);
    default:
        return 2;
    }
}

static bool inputs(loader *L)
{
    uint32_t n = count(L, S_INPT);
    sk_pack_input *out = sk_packfmt_fixed(&L->f, n, sizeof *out);
    for (uint32_t i = 0; i < n; i++) {
        at_rec(L, S_INPT, i);
        const uint8_t *p = rec(L, S_INPT, i);
        sk_pack_input in = {sk_le32(p), p[4], p[5]};
        if (!index_ok(L, in.owner, count(L, S_ELEM), false, "owner", "elements"))
            return false;
        if (in.var >= SK_VAR_COUNT_OF_VARS)
            return sk_packfmt_fail(&L->f, "variable %u is none", (unsigned)in.var);
        if (in.radix != radix_of(L, in.var))
            return sk_packfmt_fail(&L->f, "variable %u takes %u values, not %u", (unsigned)in.var,
                                   (unsigned)radix_of(L, in.var), (unsigned)in.radix);
        if (out)
            out[i] = in;
    }
    L->inputs = out;
    return true;
}

static bool stab(loader *L)
{
    uint32_t n = count(L, S_STAB);
    uint32_t *out = sk_packfmt_fixed(&L->f, n, sizeof *out);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t v = sk_le32(rec(L, S_STAB, i));
        if (v >= count(L, S_STYL)) {
            at_rec(L, S_STAB, i);
            return index_ok(L, v, count(L, S_STYL), false, "style", "styles");
        }
        if (out)
            out[i] = v;
    }
    L->stab = out;
    return true;
}

static bool svgs(loader *L)
{
    uint32_t n = count(L, S_SVGG);
    sk_pack_svg *out = sk_packfmt_fixed(&L->f, n, sizeof *out);
    for (uint32_t i = 0; i < n; i++) {
        at_rec(L, S_SVGG, i);
        const uint8_t *p = rec(L, S_SVGG, i);
        sk_pack_svg g;
        g.kind = p[0];
        g.flag = p[1];
        if (g.kind > SK_SVG_TURN || g.flag > 1)
            return sk_packfmt_fail(&L->f, "kind %u, flag %u", (unsigned)g.kind, (unsigned)g.flag);
        for (int k = 0; k < 5; k++)
            if (!finite_at(L, p + 4 + 4 * k, "its geometry", &g.v[k]))
                return false;
        g.image = sk_le32(p + 24);
        if (!index_ok(L, g.image, count(L, S_IMGS), true, "image", "images"))
            return false;
        if (out)
            out[i] = g;
    }
    if (L->out) {
        L->out->svgs = out;
        L->out->nsvgs = n;
    }
    return true;
}

/* STYL and KEYF: count + 1 offsets from the section's start, the first
 * just past them and the last at its end, then the records they bound. */
static bool offsets_ok(loader *L, int s, uint32_t most)
{
    sect *x = &L->s[s];
    at_sect(L, s);
    if (x->count > most)
        return sk_packfmt_fail(&L->f, "%u records, more than %u", (unsigned)x->count, (unsigned)most);
    if (((uint64_t)x->count + 1) * 4 > x->size)
        return sk_packfmt_fail(&L->f, "%u offsets do not fit its %u bytes", (unsigned)x->count + 1,
                               (unsigned)x->size);
    if (sk_le32(x->p) != (x->count + 1) * 4)
        return sk_packfmt_fail(&L->f, "its first record at %u, not after its offsets", (unsigned)sk_le32(x->p));
    if (sk_le32(x->p + 4 * (size_t)x->count) != x->size)
        return sk_packfmt_fail(&L->f, "its last record ends at %u, not at its end",
                               (unsigned)sk_le32(x->p + 4 * (size_t)x->count));
    return true;
}

static bool bounds(loader *L, int s, uint32_t i, uint32_t *start, uint32_t *end)
{
    sect *x = &L->s[s];
    at_rec(L, s, i);
    *start = sk_le32(x->p + 4 * (size_t)i);
    *end = sk_le32(x->p + 4 * (size_t)i + 4);
    if (*end <= *start || *end > x->size)
        return sk_packfmt_fail(&L->f, "from %u to %u: the offsets rise inside the section", (unsigned)*start,
                               (unsigned)*end);
    return true;
}

static bool styles(loader *L)
{
    uint32_t n = count(L, S_STYL), start, end;
    if (!offsets_ok(L, S_STYL, SK_PACK_MAX_STYLES))
        return false;
    sk_css_style *out = sk_packfmt_fixed(&L->f, n, sizeof *out);
    for (uint32_t i = 0; i < n; i++) {
        sk_css_style one;
        if (!bounds(L, S_STYL, i, &start, &end) ||
            !sk_packfmt_style(&L->f, L->s[S_STYL].p + start, end - start, out ? &out[i] : &one))
            return false;
    }
    if (L->out) {
        L->out->styles = out;
        L->out->nstyles = n;
    }
    return true;
}

static bool keyframes(loader *L)
{
    uint32_t n = count(L, S_KEYF), start, end;
    if (!offsets_ok(L, S_KEYF, SK_PACK_MAX_KEYFRAMES))
        return false;
    sk_pack_keyframes *out = sk_packfmt_fixed(&L->f, n, sizeof *out);
    for (uint32_t i = 0; i < n; i++) {
        sk_pack_keyframes one;
        if (!bounds(L, S_KEYF, i, &start, &end) ||
            !sk_packfmt_track(&L->f, L->s[S_KEYF].p + start, end - start, out ? &out[i] : &one))
            return false;
    }
    if (L->out) {
        L->out->keyframes = out;
        L->out->nkeyframes = n;
    }
    return true;
}

static bool layers(loader *L)
{
    uint32_t n = count(L, S_LAYR);
    sk_pack_layer *out = sk_packfmt_fixed(&L->f, n, sizeof *out);
    for (uint32_t i = 0; i < n; i++) {
        at_rec(L, S_LAYR, i);
        const uint8_t *p = rec(L, S_LAYR, i);
        sk_pack_layer y;
        y.el = sk_le32(p);
        y.style = sk_le32(p + 4);
        y.kind = p[8];
        y.scale = p[9];
        y.w = sk_le16(p + 28);
        y.h = sk_le16(p + 30);
        for (int k = 0; k < 4; k++)
            y.slice[k] = sk_le16(p + 32 + 2 * k);
        if (!index_ok(L, y.el, count(L, S_ELEM), false, "element", "elements") ||
            !index_ok(L, y.style, count(L, S_STYL), true, "style", "styles"))
            return false;
        if (y.kind > SK_LAYER_MASK)
            return sk_packfmt_fail(&L->f, "kind %u is none", (unsigned)y.kind);
        if (y.scale != 1 && y.scale != 2)
            return sk_packfmt_fail(&L->f, "scale %u: 1 or 2", (unsigned)y.scale);
        if (!finite_at(L, p + 12, "its x", &y.x) || !finite_at(L, p + 16, "its y", &y.y) ||
            !finite_at(L, p + 20, "its origin", &y.ox) || !finite_at(L, p + 24, "its origin", &y.oy) ||
            !size_ok(L, y.w, y.h))
            return false;
        if (fabsf(y.x) > SK_PACK_MAX_FLOAT || fabsf(y.y) > SK_PACK_MAX_FLOAT || fabsf(y.ox) > SK_PACK_MAX_FLOAT ||
            fabsf(y.oy) > SK_PACK_MAX_FLOAT)
            return sk_packfmt_fail(&L->f, "its place %g, %g or origin %g, %g past %g", (double)y.x, (double)y.y,
                                   (double)y.ox, (double)y.oy, (double)SK_PACK_MAX_FLOAT);
        if ((uint32_t)y.slice[SK_LEFT] + y.slice[SK_RIGHT] > y.w || (uint32_t)y.slice[SK_TOP] + y.slice[SK_BOTTOM] > y.h)
            return sk_packfmt_fail(&L->f, "slices wider than its %u x %u", (unsigned)y.w, (unsigned)y.h);
        /* The layers of one element follow one another. */
        if (i > 0 && y.el < sk_le32(rec(L, S_LAYR, i - 1)))
            return sk_packfmt_fail(&L->f, "element %u after a layer of element %u", (unsigned)y.el,
                                   (unsigned)sk_le32(rec(L, S_LAYR, i - 1)));
        if ((y.kind == SK_LAYER_STATE) != (y.style != SK_PACK_NONE) && y.kind != SK_LAYER_SPRITE &&
            y.kind != SK_LAYER_NINE && y.kind != SK_LAYER_MASK)
            return sk_packfmt_fail(&L->f, "a %s layer %s", y.kind == SK_LAYER_STATE ? "STATE" : "STATIC",
                                   y.kind == SK_LAYER_STATE ? "of no style" : "of one style");
        if (y.kind == SK_LAYER_NINE && (!(y.ox > 0) || !(y.oy > 0) || (uint32_t)y.slice[SK_LEFT] + y.slice[SK_RIGHT] >= y.w ||
                                        (uint32_t)y.slice[SK_TOP] + y.slice[SK_BOTTOM] >= y.h))
            return sk_packfmt_fail(&L->f, "a nine-slice for a box of %g x %g with no middle", (double)y.ox,
                                   (double)y.oy);
        if (!blob_at(L, p + 40, &y.blob, &y.len) || !image_ok(L, y.blob, y.len, y.w, y.h))
            return false;
        if (out)
            out[i] = y;
    }
    if (L->out) {
        L->out->layers = out;
        L->out->nlayers = n;
    }
    return true;
}

/* The tree: one, from element 0, each element reached once by its
 * parent's first child and its siblings, each naming that parent. */
static bool tree(loader *L)
{
    uint32_t n = count(L, S_ELEM);
#define PARENT(e) sk_le32(rec(L, S_ELEM, e))
#define FIRST(e) sk_le32(rec(L, S_ELEM, e) + 4)
#define NEXT(e) sk_le32(rec(L, S_ELEM, e) + 8)
    at_rec(L, S_ELEM, 0);
    if (rec(L, S_ELEM, 0)[12] != SK_EL_HOST || PARENT(0) != SK_PACK_NONE || NEXT(0) != SK_PACK_NONE)
        return sk_packfmt_fail(&L->f, "the root is not a host without a parent or siblings");
    uint8_t *seen = sk_xcalloc(n / 8 + 1, 1);
    uint32_t e = 0, reached = 1;
    seen[0] = 1;
    bool ok = true;
    while (ok) {
        uint32_t next = FIRST(e), parent = e;
        while (next == SK_PACK_NONE && e != 0) {
            next = NEXT(e);
            parent = PARENT(e);
            if (next == SK_PACK_NONE)
                e = parent;
        }
        if (next == SK_PACK_NONE)
            break;
        at_rec(L, S_ELEM, next);
        if (seen[next / 8] & (1u << next % 8))
            ok = sk_packfmt_fail(&L->f, "reached twice");
        else if (PARENT(next) != parent)
            ok = sk_packfmt_fail(&L->f, "its parent is %u, not %u, which reaches it", (unsigned)PARENT(next),
                                 (unsigned)parent);
        seen[next / 8] |= (uint8_t)(1u << next % 8);
        reached++;
        e = next;
    }
    for (uint32_t i = 0; ok && reached != n && i < n; i++)
        if (!(seen[i / 8] & (1u << i % 8))) {
            at_rec(L, S_ELEM, i);
            ok = sk_packfmt_fail(&L->f, "not in the tree");
        }
    free(seen);
    return ok;
#undef PARENT
#undef FIRST
#undef NEXT
}

static bool elements(loader *L)
{
    uint32_t n = count(L, S_ELEM);
    at_sect(L, S_ELEM);
    if (!n || n > SK_PACK_MAX_ELEMENTS)
        return sk_packfmt_fail(&L->f, "%u elements: 1 to %u", (unsigned)n, SK_PACK_MAX_ELEMENTS);
    sk_pack_el *out = sk_packfmt_fixed(&L->f, n, sizeof *out);
    uint64_t states = 0, owned = 0;
    for (uint32_t i = 0; i < n; i++) {
        at_rec(L, S_ELEM, i);
        const uint8_t *p = rec(L, S_ELEM, i);
        sk_pack_el e;
        memset(&e, 0, sizeof e);
        e.parent = sk_le32(p);
        e.first_child = sk_le32(p + 4);
        e.next_sibling = sk_le32(p + 8);
        e.kind = p[12];
        e.role = p[13];
        e.group = p[14];
        e.meter = p[15];
        e.widget = sk_le32(p + 16);
        e.rect = sk_le32(p + 20);
        e.svg = sk_le32(p + 24);
        e.text_source = p[28];
        e.ninputs = p[29];
        uint32_t first_input = sk_le32(p + 36), table = sk_le32(p + 40);
        e.layer = sk_le32(p + 44);
        e.nlayers = sk_le32(p + 48);
        if (!index_ok(L, e.parent, n, true, "parent", "elements") ||
            !index_ok(L, e.first_child, n, true, "first child", "elements") ||
            !index_ok(L, e.next_sibling, n, true, "next sibling", "elements"))
            return false;
        if (e.kind > SK_EL_AFTER || e.role > SK_ROLE_OPTION_OTHER || e.text_source > SK_TEXT_ITEM_TEXT)
            return sk_packfmt_fail(&L->f, "kind %u, role %u, text source %u", (unsigned)e.kind, (unsigned)e.role,
                                   (unsigned)e.text_source);
        if (e.group > SK_PACK_MAX_LIMITS || (e.group ? e.meter < 1 || e.meter > e.group : e.meter != 0))
            return sk_packfmt_fail(&L->f, "meter %u of a group of %u", (unsigned)e.meter, (unsigned)e.group);
        if (!index_ok(L, e.widget, count(L, S_WIDG), true, "widget", "widgets") ||
            !index_ok(L, e.rect, count(L, S_RECT), true, "rect", "rects") ||
            !index_ok(L, e.svg, count(L, S_SVGG), true, "svg", "svgs") ||
            !str_at(L, p + 32, true, "its text", &e.text))
            return false;
        if (e.ninputs > SK_PACK_MAX_INPUTS)
            return sk_packfmt_fail(&L->f, "%u inputs, more than %u", (unsigned)e.ninputs, SK_PACK_MAX_INPUTS);
        if (!range_ok(L, first_input, e.ninputs, count(L, S_INPT), "inputs", "inputs"))
            return false;
        uint32_t entries = 1;
        for (uint8_t k = 0; k < e.ninputs; k++) {
            entries *= rec(L, S_INPT, first_input + k)[5];
            if (entries > SK_PACK_MAX_STATES)
                return sk_packfmt_fail(&L->f, "more than %u states", SK_PACK_MAX_STATES);
        }
        states += entries;
        if (states > SK_PACK_MAX_TOTAL_STATES)
            return sk_packfmt_fail(&L->f, "the tables reach more than %u states in all", SK_PACK_MAX_TOTAL_STATES);
        if (!range_ok(L, table, entries, count(L, S_STAB), "its table", "STAB entries") ||
            !range_ok(L, e.layer, e.nlayers, count(L, S_LAYR), "layers", "layers"))
            return false;
        /* Its layers are its own, each for no style or one its table
         * gives. */
        for (uint32_t k = 0; k < e.nlayers; k++) {
            const uint8_t *y = rec(L, S_LAYR, e.layer + k);
            uint32_t st = sk_le32(y + 4);
            if (sk_le32(y) != i)
                return sk_packfmt_fail(&L->f, "layer %u is element %u's", (unsigned)(e.layer + k),
                                       (unsigned)sk_le32(y));
            bool given = st == SK_PACK_NONE;
            for (uint32_t t = 0; t < entries && !given; t++)
                given = sk_le32(rec(L, S_STAB, table + t)) == st;
            if (!given)
                return sk_packfmt_fail(&L->f, "layer %u is for style %u, which its table never gives",
                                       (unsigned)(e.layer + k), (unsigned)st);
        }
        owned += e.nlayers;
        e.inputs = L->inputs && e.ninputs ? L->inputs + first_input : NULL;
        e.styles = L->stab ? L->stab + table : NULL;
        if (out)
            out[i] = e;
    }
    if (owned != count(L, S_LAYR)) {
        at_sect(L, S_LAYR);
        return sk_packfmt_fail(&L->f, "%u layers, %llu of them an element's", (unsigned)count(L, S_LAYR),
                               (unsigned long long)owned);
    }
    if (!L->out && !tree(L))
        return false;
    if (L->out) {
        L->out->els = out;
        L->out->nels = n;
    }
    return true;
}

static bool widgets(loader *L)
{
    uint32_t n = count(L, S_WIDG);
    sk_pack_widget *out = sk_packfmt_fixed(&L->f, n, sizeof *out);
    static const char *const STRS[9] = {"its id",           "its bind",           "its label",
                                        "its prefix",       "its suffix",         "its resets",
                                        "its fallback bind", "its fallback label", "its fallback resets"};
    static const char *const NUMS[13] = {"min",   "max",   "damping", "start angle", "end angle",   "thickness",
                                         "gap",   "on",    "pulse",   "blink",       "fps",         "frame width",
                                         "frame height"};
    for (uint32_t i = 0; i < n; i++) {
        at_rec(L, S_WIDG, i);
        const uint8_t *p = rec(L, S_WIDG, i);
        sk_pack_widget w;
        memset(&w, 0, sizeof w);
        w.type = p[0];
        w.format = p[1];
        w.digits = p[2];
        uint8_t flags = p[3];
        if (w.type > SK_WIDGET_LIMITS || w.format > SK_FORMAT_TEXT || w.digits > 4 || flags >= 128)
            return sk_packfmt_fail(&L->f, "type %u, format %u, digits %u, flags 0x%x", (unsigned)w.type,
                                   (unsigned)w.format, (unsigned)w.digits, (unsigned)flags);
        w.has_on = flags & 1;
        w.pulse = flags >> 1 & 1;
        w.vertical = flags >> 2 & 1;
        w.column = flags >> 3 & 1;
        w.run = flags >> 4 & 1;
        w.needle_meters = flags >> 5 & 1;
        w.across = flags >> 6 & 1;
        const char **s[9] = {&w.id,     &w.bind,   &w.label,         &w.prefix,         &w.suffix,
                             &w.resets, &w.fallback_bind, &w.fallback_label, &w.fallback_resets};
        for (int k = 0; k < 9; k++)
            if (!str_at(L, p + 4 + 4 * k, k > 0, STRS[k], s[k]))
                return false;
        float *f[13] = {&w.min, &w.max, &w.damping, &w.start_angle, &w.end_angle, &w.thickness,  &w.gap,
                        &w.on,  &w.pulse_ms, &w.blink, &w.fps,      &w.frame_w,   &w.frame_h};
        for (int k = 0; k < 13; k++)
            if (!finite_at(L, p + 40 + 4 * k, NUMS[k], f[k]))
                return false;
        w.ticks = sk_le16(p + 92);
        w.segments = sk_le16(p + 94);
        w.frames = sk_le16(p + 96);
        w.rows = sk_le16(p + 98);
        uint32_t first_mark = sk_le32(p + 100);
        w.nmarks = sk_le16(p + 104);
        w.el = sk_le32(p + 108);
        w.toggle = sk_le32(p + 112);
        w.face = sk_le32(p + 116);
        w.image = sk_le32(p + 120);
        w.sheet = sk_le32(p + 124);
        if (!range_ok(L, first_mark, w.nmarks, count(L, S_MARK), "marks", "marks") ||
            !index_ok(L, w.el, count(L, S_ELEM), false, "element", "elements") ||
            !index_ok(L, w.toggle, count(L, S_ELEM), true, "toggle", "elements") ||
            !index_ok(L, w.face, count(L, S_IMGS), true, "face", "images") ||
            !index_ok(L, w.image, count(L, S_IMGS), true, "image", "images") ||
            !index_ok(L, w.sheet, count(L, S_IMGS), true, "sheet", "images"))
            return false;
        w.marks = L->marks && w.nmarks ? L->marks + first_mark : NULL;
        if (out)
            out[i] = w;
    }
    if (L->out) {
        L->out->widgets = out;
        L->out->nwidgets = n;
    }
    return true;
}

static bool fonts(loader *L)
{
    uint32_t n = count(L, S_FONT);
    sk_pack_font *out = sk_packfmt_fixed(&L->f, n, sizeof *out);
    for (uint32_t i = 0; i < n; i++) {
        at_rec(L, S_FONT, i);
        const uint8_t *p = rec(L, S_FONT, i);
        sk_pack_font f;
        uint32_t off;
        f.weight = sk_le16(p + 4);
        f.style = p[6];
        if (!str_at(L, p, false, "its family", &f.family) || !blob_at(L, p + 8, &off, &f.len))
            return false;
        if (f.weight < 1 || f.weight > 1000 || f.style > SK_FSTYLE_OBLIQUE)
            return sk_packfmt_fail(&L->f, "weight %u, style %u", (unsigned)f.weight, (unsigned)f.style);
        f.data = L->s[S_BLOB].p + off;
        char why[160];
        if (!L->out && !sk_font_check(f.data, f.len, why, sizeof why))
            return sk_packfmt_fail(&L->f, "the font %s", why);
        if (out)
            out[i] = f;
    }
    if (L->out) {
        L->out->fonts = out;
        L->out->nfonts = n;
    }
    return true;
}

static bool sounds(loader *L)
{
    uint32_t n = count(L, S_SNDS);
    sk_pack_sound *out = sk_packfmt_fixed(&L->f, n, sizeof *out);
    for (uint32_t i = 0; i < n; i++) {
        at_rec(L, S_SNDS, i);
        const uint8_t *p = rec(L, S_SNDS, i);
        sk_pack_sound s;
        uint32_t off;
        s.mode = p[4];
        s.block_bytes = sk_le16(p + 6);
        s.rate = sk_le32(p + 20);
        s.frames = sk_le32(p + 24);
        if (!str_at(L, p, false, "its path", &s.path) || !finite_at(L, p + 8, "its volume", &s.volume) ||
            !finite_at(L, p + 12, "its interval", &s.interval[0]) ||
            !finite_at(L, p + 16, "its interval", &s.interval[1]) || !blob_at(L, p + 28, &off, &s.len))
            return false;
        if (s.mode > SK_SOUND_RANDOM)
            return sk_packfmt_fail(&L->f, "mode %u is none", (unsigned)s.mode);
        if (!(s.volume >= 0 && s.volume <= 1))
            return sk_packfmt_fail(&L->f, "volume %g: 0 to 1", (double)s.volume);
        if (s.mode == SK_SOUND_LOOP ? s.interval[0] != 0 || s.interval[1] != 0
                                    : !(s.interval[0] >= 1 && s.interval[0] <= s.interval[1] && s.interval[1] <= 3600))
            return sk_packfmt_fail(&L->f, "an interval of %g to %g s for a %s sound", (double)s.interval[0],
                                   (double)s.interval[1], s.mode == SK_SOUND_LOOP ? "looping" : "random");
        if (s.rate < 8000 || s.rate > 48000)
            return sk_packfmt_fail(&L->f, "%u Hz: 8 to 48 kHz", (unsigned)s.rate);
        if (!sk_adpcm_block_samples(s.block_bytes) || !s.frames)
            return sk_packfmt_fail(&L->f, "blocks of %u bytes for %u frames", (unsigned)s.block_bytes,
                                   (unsigned)s.frames);
        s.data = L->s[S_BLOB].p + off;
        if (!L->out && !sk_adpcm_valid(s.data, s.len, s.block_bytes, s.frames))
            return sk_packfmt_fail(&L->f, "its %u bytes are not the blocks of %u frames", (unsigned)s.len,
                                   (unsigned)s.frames);
        if (out)
            out[i] = s;
    }
    if (L->out) {
        L->out->sounds = out;
        L->out->nsounds = n;
    }
    return true;
}

/* Every section in an order where what a record points into is read
 * first. */
static bool pass(loader *L)
{
    L->f.nimages = count(L, S_IMGS);
    L->f.nkeyframes = count(L, S_KEYF);
    L->f.nfaces = SK_PACK_EMBEDDED_FACES + count(L, S_FONT);
    L->f.used = 0;
    if (!strs(L) || !splits(L) || !spans(L) || !rects(L) || !variants(L) || !images(L) || !meta(L) || !marks(L) ||
        !inputs(L) || !stab(L) || !svgs(L) || !styles(L) || !keyframes(L) || !layers(L) || !elements(L) ||
        !widgets(L) || !fonts(L) || !sounds(L))
        return false;
    if (L->out) {
        L->out->blob = L->s[S_BLOB].p;
        L->out->nblob = L->s[S_BLOB].size;
        memcpy(L->out->source_hash, L->f.data + 32, 32);
    }
    return true;
}

bool sk_pack_load(const uint8_t *data, size_t n, sk_pack *out, char *err, size_t err_len)
{
    memset(out, 0, sizeof *out);
    if (err && err_len)
        err[0] = 0;
    loader L;
    memset(&L, 0, sizeof L);
    L.f.data = data;
    L.f.n = n;
    L.f.err = err;
    L.f.err_len = err_len;
    L.f.scratch = sk_xmalloc(SK_PACK_SCRATCH);
    bool ok = header(&L) && pass(&L);
    if (ok && L.f.used > SK_PACK_MAX_MEMORY) {
        at_sect(&L, -1);
        ok = sk_packfmt_fail(&L.f, "it would take %llu bytes of memory, more than %u",
                             (unsigned long long)L.f.used, SK_PACK_MAX_MEMORY);
    }
    if (ok) {
        L.f.cap = L.f.used;
        L.f.mem = sk_xmalloc(L.f.cap ? (size_t)L.f.cap : 1);
        L.out = out;
        L.marks = NULL;
        L.inputs = NULL;
        L.stab = NULL;
        L.variants = NULL;
        out->mem_ = L.f.mem;
        if (!pass(&L))
            sk_die("skin pack: a pack its first pass passed fails its second");
    }
    free(L.f.scratch);
    if (!ok)
        memset(out, 0, sizeof *out);
    return ok;
}

void sk_pack_free(sk_pack *p)
{
    free(p->mem_);
    memset(p, 0, sizeof *p);
}
