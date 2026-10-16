/* The front end's tree for the layout: one node per element, a
 * ::before first among its element's children and an ::after last, as
 * a pack keeps them; a limits widget's meters of the count shown, a
 * list's row when the state shows one. */
#include "render.h"

#include "../src/base/paths.h"
#include "splits.h"

#include <math.h>
#include <string.h>

static uint8_t value_of(const sk_render_state *st, const sk_el *owner, uint8_t var)
{
    for (size_t i = 0; st && i < st->nassign; i++)
        if (st->assign[i].owner == owner && st->assign[i].var == var)
            return st->assign[i].value;
    return 0;
}

/* Whether child c is in the tree drawn. */
static bool shown(const sk_render_state *st, const sk_el *c)
{
    if (c->group && c->limits && c->group != value_of(st, c->limits, SK_VAR_COUNT))
        return false;
    if (c->repeated && !(st && st->rows))
        return false;
    return true;
}

static uint32_t add_image(sk_render_tree *t, const char *dir, const char *file)
{
    char *p = sk_path_join(dir, file);
    const char *kept = sk_arena_strndup(&t->arena, p, strlen(p));
    free(p);
    sk_vec_push(&t->images, kept);
    return (uint32_t)(t->images.len - 1);
}

void sk_render_tree_build(sk_render_tree *t, const sk_front *f, const sk_render_state *st, float w, float h)
{
    memset(t, 0, sizeof *t);
    sk_arena_init(&t->arena, 0);
    t->n = (uint32_t)f->els.len;
    t->width = w;
    t->height = h;
    t->root = SK_PACK_NONE;
    t->nodes = sk_xcalloc(t->n ? t->n : 1, sizeof *t->nodes);
    t->rects = sk_xcalloc(t->n ? t->n : 1, sizeof *t->rects);
    t->svgs = sk_xcalloc(t->n ? t->n : 1, sizeof *t->svgs);
    for (size_t i = 0; i < f->images.len; i++)
        sk_vec_push(&t->images, (const char *)f->images.ptr[i].real);
    /* The widgets' own images, after the stylesheet's. */
    size_t nw = f->skin.nwidgets;
    uint32_t *face = sk_xcalloc(nw ? nw : 1, sizeof *face), *knob = sk_xcalloc(nw ? nw : 1, sizeof *knob);
    for (size_t i = 0; i < nw; i++) {
        const sk_skin_widget *sw = &f->skin.widgets[i];
        face[i] = sw->face ? add_image(t, f->skin.real, sw->face) : SK_PACK_NONE;
        knob[i] = sw->image ? add_image(t, f->skin.real, sw->image) : SK_PACK_NONE;
    }
    /* The splits at their sizes in this area. */
    double at[64];
    size_t ns = SK_MIN(f->skin.nsplits, SK_LEN(at));
    double saved[64];
    for (size_t i = 0; i < ns; i++)
        saved[i] = st && st->saved ? st->saved[i] : NAN;
    sk_split_sizes(&f->skin, saved, w, h, at);
    for (uint32_t i = 0; i < t->n; i++) {
        const sk_el *e = f->els.ptr[i];
        sk_lnode *n = &t->nodes[i];
        n->parent = n->first = n->next = SK_PACK_NONE;
        n->kind = e->kind;
        n->role = e->role;
        n->style = sk_front_style(f, e, st ? st->assign : NULL, st ? st->nassign : 0);
        n->text = e->kind == SK_EL_BEFORE || e->kind == SK_EL_AFTER ? NULL : e->text;
        t->svgs[i] = e->svg;
        if (e->svg.kind == SK_SVG_IMAGE && e->widget >= 0 && (size_t)e->widget < nw)
            t->svgs[i].image = e->role == SK_ROLE_FACE ? face[e->widget] : knob[e->widget];
        n->svg = e->svg.kind != SK_SVG_NONE ? &t->svgs[i] : NULL;
        if (e->svg.kind == SK_SVG_TURN && e->widget >= 0 && (size_t)e->widget < nw)
            n->turn = f->skin.widgets[e->widget].w.start_angle;
        const sk_pack_rect *r = NULL;
        if (e->rect >= 1000000 && (size_t)(e->rect - 1000000) < nw)
            r = &f->skin.widgets[e->rect - 1000000].toggle_rect;
        else if (e->rect > 0 && (size_t)(e->rect - 1) < f->skin.npanels)
            r = &f->skin.panels[e->rect - 1].rect;
        else if (e->rect < 0 && (size_t)(-e->rect - 1) < nw)
            r = &f->skin.widgets[-e->rect - 1].rect;
        if (r) {
            t->rects[i] = sk_split_rect(&f->skin, r, at);
            n->rect = &t->rects[i];
        }
        if (e->role == SK_ROLE_STAGE && t->root == SK_PACK_NONE)
            t->root = i;
    }
    /* The children: a ::before, the elements, an ::after. */
    for (uint32_t i = 0; i < t->n; i++) {
        const sk_el *e = f->els.ptr[i];
        if (e->kind == SK_EL_BEFORE || e->kind == SK_EL_AFTER)
            continue;
        uint32_t last = SK_PACK_NONE;
        const sk_el *kids[3] = {e->before, e->first, e->after};
        for (int k = 0; k < 3; k++) {
            for (const sk_el *c = kids[k]; c; c = k == 1 ? c->next : NULL) {
                if (!shown(st, c))
                    continue;
                t->nodes[c->index].parent = i;
                if (last == SK_PACK_NONE)
                    t->nodes[i].first = c->index;
                else
                    t->nodes[last].next = c->index;
                last = c->index;
            }
        }
    }
    free(face);
    free(knob);
}

void sk_render_tree_free(sk_render_tree *t)
{
    free(t->nodes);
    free(t->rects);
    free(t->svgs);
    sk_vec_free(&t->images);
    sk_arena_free(&t->arena);
    memset(t, 0, sizeof *t);
}

void sk_render_fonts_open(sk_render_fonts *rf, const sk_front *f)
{
    rf->n = f->skin.nfonts;
    rf->faces = sk_xcalloc(rf->n ? rf->n : 1, sizeof *rf->faces);
    for (size_t i = 0; i < rf->n; i++) {
        char *p = sk_path_join(f->skin.real, f->skin.fonts[i].file);
        rf->faces[i] = sk_font_open_file(p, 0);
        free(p);
    }
}

void sk_render_fonts_close(sk_render_fonts *rf)
{
    for (size_t i = 0; i < rf->n; i++)
        sk_font_close(rf->faces[i]);
    free(rf->faces);
    memset(rf, 0, sizeof *rf);
}

const sk_font *sk_render_face(void *data, uint16_t index)
{
    const sk_render_fonts *rf = data;
    if (index < SK_FONT_EMBEDDED_COUNT)
        return sk_font_embedded((sk_embedded_font)index);
    size_t k = (size_t)index - SK_FONT_EMBEDDED_COUNT;
    return rf && k < rf->n ? rf->faces[k] : NULL;
}
