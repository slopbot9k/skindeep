/* A skin pack as the app holds it (decision 0037): the loader's pack,
 * its faces opened once, shared by reference between the thread that
 * loaded it and the windows that show it; and what the window takes
 * from the manifest without a view: its size and floor (decision 0006,
 * app/skins/scale.js), the terminal's theme and font, the sounds. */
#include "viewint.h"

#include "../audio/adpcm.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

sk_skinpack *sk_skinpack_new(uint8_t *bytes, size_t n, bool own, char *err, size_t err_len)
{
    sk_skinpack *p = sk_xcalloc(1, sizeof *p);
    if (!sk_pack_load(bytes, n, &p->pack, err, err_len)) {
        if (own)
            free(bytes);
        free(p);
        return NULL;
    }
    p->bytes = bytes;
    p->own = own;
    atomic_init(&p->refs, 1);
    p->faces = sk_xcalloc(p->pack.nfonts ? p->pack.nfonts : 1, sizeof *p->faces);
    for (uint32_t i = 0; i < p->pack.nfonts; i++)
        p->faces[i] = sk_font_open(p->pack.fonts[i].data, p->pack.fonts[i].len, 0);
    return p;
}

sk_skinpack *sk_skinpack_ref(sk_skinpack *p)
{
    if (p)
        atomic_fetch_add(&p->refs, 1);
    return p;
}

void sk_skinpack_unref(sk_skinpack *p)
{
    if (!p || atomic_fetch_sub(&p->refs, 1) != 1)
        return;
    for (uint32_t i = 0; i < p->pack.nfonts; i++)
        sk_font_close(p->faces[i]);
    free(p->faces);
    sk_pack_free(&p->pack);
    if (p->own)
        free(p->bytes);
    free(p);
}

const char *sk_skinpack_name(const sk_skinpack *p)
{
    return p->pack.meta.name ? p->pack.meta.name : "";
}

const unsigned char *sk_skinpack_hash(const sk_skinpack *p)
{
    return p->pack.source_hash;
}

/* scale.js: the scaled terminal keeps 40 columns and 10 rows of the
 * skin's font, the window no less than half the skin's minimum. */
#define MIN_SCALE 0.5
#define COLS 40
#define ROWS 10
#define CELL 0.6
#define LINE 1.2

static double key_or0(const sk_pack_rect *r, int key)
{
    return r->has & (1u << key) ? r->v[key] : 0;
}

static double font_size(const sk_pack_meta *m)
{
    return m->terminal_font_size > 0 ? m->terminal_font_size : 14;
}

void sk_skinpack_window(const sk_skinpack *p, sk_skin_window *out)
{
    const sk_pack_meta *m = &p->pack.meta;
    const sk_pack_rect *r = &m->terminal_rect;
    double fs = font_size(m);
    double share = (m->min_width - key_or0(r, 0) - key_or0(r, 1)) / m->min_width;
    double cols = share > 0 ? ceil(COLS * fs * CELL / share) : m->min_width;
    double w = fmin(m->min_width, fmax(ceil(m->min_width * MIN_SCALE), cols));
    double area = m->min_height - SK_VIEW_STRIP;
    double hshare = (area - key_or0(r, 3) - key_or0(r, 4)) / area;
    double rows = hshare > 0 ? ceil(ROWS * fs * LINE / hshare) : area;
    double h = SK_VIEW_STRIP + fmin(area, fmax(ceil(area * MIN_SCALE), rows));
    out->width = (int)lround(m->width);
    out->height = (int)lround(m->height);
    out->min_width = (int)ceil(w);
    out->min_height = (int)ceil(h);
}

void sk_skinpack_terminal(const sk_skinpack *p, sk_skin_terminal *out)
{
    const sk_pack_meta *m = &p->pack.meta;
    memset(out, 0, sizeof *out);
    for (int i = 0; i < SK_THEME_COUNT; i++)
        out->colors[i] = m->theme[i];
    out->has = m->has_theme;
    out->face = m->terminal_font < p->pack.nfonts ? p->faces[m->terminal_font] : NULL;
    out->font_px = (float)font_size(m);
}

float sk_skin_font_px(const sk_skin_terminal *t, double setting)
{
    if (setting > 0)
        return (float)setting;
    return t->font_px > 0 ? t->font_px : 14;
}

size_t sk_skinpack_sounds(const sk_skinpack *p, sk_sounds_entry *out, size_t cap)
{
    size_t n = 0;
    for (uint32_t i = 0; i < p->pack.nsounds && n < cap; i++) {
        const sk_pack_sound *s = &p->pack.sounds[i];
        sk_sounds_entry *e = &out[n++];
        memset(e, 0, sizeof *e);
        e->loop = s->mode == SK_SOUND_LOOP;
        e->volume = s->volume;
        e->interval[0] = s->interval[0];
        e->interval[1] = s->interval[1];
        int16_t *pcm = s->frames ? sk_xreallocarray(NULL, s->frames, sizeof *pcm) : NULL;
        if (pcm && sk_adpcm_decode(s->data, s->len, s->block_bytes, s->frames, pcm))
            e->pcm = sk_pcm_adopt(pcm, s->frames, s->rate);
        else
            free(pcm);
    }
    return n;
}

void sk_skin_sounds_release(sk_sounds_entry *e, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        sk_pcm_unref(e[i].pcm);
        e[i].pcm = NULL;
    }
}

void sk_skin_metrics_of(const sk_bus *b, sk_skin_metrics *out)
{
    size_t n = sk_bus_count(b);
    out->n = n;
    out->items = sk_xcalloc(n ? n : 1, sizeof *out->items);
    for (size_t i = 0; i < n; i++) {
        const char *name;
        const sk_val *v;
        sk_bus_at(b, i, &name, &v);
        out->items[i].name = sk_xstrdup(name);
        out->items[i].value = *v;
        out->items[i].value.text = v->text ? sk_xstrdup(v->text) : NULL;
    }
}

void sk_skin_metrics_free(sk_skin_metrics *m)
{
    for (size_t i = 0; i < m->n; i++) {
        free((char *)m->items[i].name);
        free(m->items[i].value.text);
    }
    free(m->items);
    m->items = NULL;
    m->n = 0;
}
