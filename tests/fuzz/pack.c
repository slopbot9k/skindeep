/* The pack loader of decision 0037, which reads what may be any file in
 * a cache directory: whatever it passes, the runtime may follow every
 * index, count, string and list in. So after a load everything is
 * walked as the runtime would: each element's state table through
 * sk_pack_style_of in every state, each style's lists to their ends,
 * its animations and transitions as sk_style_anim and
 * sk_style_transition pair them, every string to its NUL, each image
 * variant and layer decoded to its size, each sound's blocks decoded.
 * Then the pack is taken in as the runtime takes it, laid out at its
 * size for its layers' checks. The input is copied to a buffer of its
 * own size, so a read past the pack is the sanitizer's to see. A pack
 * that fails says why and is left empty. */
#include "../../src/audio/adpcm.h"
#include "../../src/gfx/image.h"
#include "../../src/skin/pack.h"
#include "../../src/skin/view.h"
#include "fuzz.h"

#include <stdlib.h>
#include <string.h>

const char *const sk_fuzz_dict[] = {
    "SKINPACK", "STRS", "META", "SPLT", "SPAN", "RECT", "WIDG", "MARK", "ELEM", "SVGG",
    "INPT",     "STAB", "STYL", "KEYF", "IMGS", "VARI", "LAYR", "FONT", "SNDS", "BLOB",
    "qoif",     "\x01\x00", "\x00\x00\x80\x3f", "\x00\x00\x80\x7f", "\xff\xff\xff\xff", "\x00\x01\x00\x00",
    "\x00\x00\x00\x00\x00\x00\x00\x01", "\x00\x00\x00\x00", NULL,
};

static volatile size_t sink;

static void str(const char *s)
{
    if (s)
        sink += strlen(s);
}

static void bytes(const void *p, size_t n)
{
    const unsigned char *b = p;
    for (size_t i = 0; i < n; i++)
        sink += b[i];
}

#define LIST(ptr, n, per) bytes((ptr), (size_t)(n) * (per) * sizeof *(ptr))

static void image(const sk_css_image *im)
{
    bytes(im, sizeof *im);
    if (im->kind == SK_IMAGE_GRADIENT) {
        bytes(im->gradient, sizeof *im->gradient);
        LIST(im->gradient->stops, im->gradient->nstops, 1);
    }
}

static void style(const sk_css_style *s)
{
    bytes(s, sizeof *s);
    image(&s->bi_source);
    LIST(s->grid_columns, s->ngrid_columns, 1);
    LIST(s->grid_rows, s->ngrid_rows, 1);
    LIST(s->transform, s->ntransform, 1);
    LIST(s->filter, s->nfilter, 1);
    LIST(s->clip, s->nclip, 2);
    for (uint16_t i = 0; i < s->nbg_image; i++) {
        image(&s->bg_image[i]);
        bytes(&s->bg_position[i % s->nbg_position], sizeof *s->bg_position);
        bytes(&s->bg_size[i % s->nbg_size], sizeof *s->bg_size);
        bytes(&s->bg_repeat[2 * (i % s->nbg_repeat)], 2);
        bytes(&s->bg_blend[i % s->nbg_blend], 1);
        bytes(&s->bg_origin[i % s->nbg_origin], 1);
        bytes(&s->bg_clip[i % s->nbg_clip], 1);
    }
    LIST(s->box_shadow, s->nbox_shadow, 1);
    for (uint16_t i = 0; i < s->nfont_family; i++)
        str(s->font_family[i]);
    LIST(s->text_shadow, s->ntext_shadow, 1);
    for (uint16_t i = 0; i < s->ncontent; i++)
        str(s->content[i].text);
    LIST(s->dasharray, s->ndasharray, 1);
    for (uint16_t i = 0; i < s->nanim_name; i++) {
        sk_css_anim a = sk_style_anim(s, i);
        bytes(&a, sizeof a);
    }
    for (uint16_t i = 0; i < s->ntr_property; i++) {
        sk_css_transition t = sk_style_transition(s, i);
        bytes(&t, sizeof t);
    }
}

/* A QOI image of BLOB, which must decode to w x h. */
static void picture(const sk_pack *p, uint32_t blob, uint32_t len, uint32_t w, uint32_t h)
{
    if ((uint64_t)blob + len > p->nblob)
        abort();
    sk_image im;
    if (!sk_qoi_decode(p->blob + blob, len, SK_PACK_MAX_PIXELS, &im) || (uint32_t)im.w != w || (uint32_t)im.h != h)
        abort();
    sk_image_free(&im);
}

static void walk(const sk_pack *p)
{
    const sk_pack_meta *m = &p->meta;
    str(m->name);
    str(m->version);
    str(m->author);
    str(m->description);
    str(m->license);
    for (uint32_t i = 0; i < p->nsplits; i++)
        str(p->splits[i].id);
    for (uint32_t i = 0; i < p->nspans; i++)
        str(p->spans[i]);
    for (uint32_t i = 0; i < p->nwidgets; i++) {
        const sk_pack_widget *w = &p->widgets[i];
        const char *s[9] = {w->id,     w->bind,          w->label,          w->prefix,         w->suffix,
                            w->resets, w->fallback_bind, w->fallback_label, w->fallback_resets};
        for (int k = 0; k < 9; k++)
            str(s[k]);
        for (uint16_t k = 0; k < w->nmarks; k++) {
            str(w->marks[k].bind);
            str(w->marks[k].klass);
        }
        if (w->el >= p->nels)
            abort();
    }
    for (uint32_t i = 0; i < p->nels; i++) {
        const sk_pack_el *e = &p->els[i];
        str(e->text);
        if (e->rect != SK_PACK_NONE)
            bytes(&p->rects[e->rect], sizeof *p->rects);
        if (e->svg != SK_PACK_NONE)
            bytes(&p->svgs[e->svg], sizeof *p->svgs);
        if (e->ninputs > SK_PACK_MAX_INPUTS)
            abort();
        /* Every state. */
        uint8_t values[SK_PACK_MAX_INPUTS];
        memset(values, 0, sizeof values);
        for (;;) {
            style(sk_pack_style_of(p, e, values));
            uint8_t k = 0;
            while (k < e->ninputs && ++values[k] == e->inputs[k].radix)
                values[k++] = 0;
            if (k == e->ninputs)
                break;
        }
        for (uint8_t k = 0; k < e->ninputs; k++)
            if (e->inputs[k].owner >= p->nels)
                abort();
    }
    for (uint32_t i = 0; i < p->nstyles; i++)
        style(&p->styles[i]);
    for (uint32_t i = 0; i < p->nkeyframes; i++) {
        str(p->keyframes[i].name);
        for (uint16_t k = 0; k < p->keyframes[i].nkeys; k++)
            LIST(p->keyframes[i].keys[k].transform, p->keyframes[i].keys[k].ntransform, 1);
    }
    for (uint32_t i = 0; i < p->nimages; i++) {
        str(p->images[i].path);
        for (uint16_t k = 0; k < p->images[i].nvariants; k++) {
            const sk_pack_variant *v = &p->images[i].variants[k];
            picture(p, v->blob, v->len, v->w, v->h);
        }
    }
    for (uint32_t i = 0; i < p->nlayers; i++)
        picture(p, p->layers[i].blob, p->layers[i].len, p->layers[i].w, p->layers[i].h);
    for (uint32_t i = 0; i < p->nfonts; i++) {
        str(p->fonts[i].family);
        bytes(p->fonts[i].data, p->fonts[i].len);
    }
    for (uint32_t i = 0; i < p->nsounds; i++) {
        const sk_pack_sound *s = &p->sounds[i];
        str(s->path);
        int16_t *pcm = malloc((size_t)s->frames * sizeof *pcm);
        if (!pcm || !sk_adpcm_decode(s->data, s->len, s->block_bytes, s->frames, pcm))
            abort();
        free(pcm);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint8_t *copy = malloc(size ? size : 1);
    if (!copy)
        abort();
    if (size)
        memcpy(copy, data, size);
    sk_pack p;
    char err[300];
    if (!sk_pack_load(copy, size, &p, err, sizeof err)) {
        if (!err[0] || p.mem_ || p.els || p.styles)
            abort();
        free(copy);
        return 0;
    }
    walk(&p);
    sk_pack_free(&p);
    /* Taken in as the runtime takes it: laid out, its layers checked
     * against the boxes. */
    sk_skinpack *sp = sk_skinpack_new(copy, size, false, err, sizeof err);
    if (!sp && !err[0])
        abort();
    sk_skinpack_unref(sp);
    free(copy);
    return 0;
}
