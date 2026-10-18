/* The front end's run: the manifest checked, the three sheets parsed
 * and their declarations checked, the keyframes gathered, the tree
 * built and the cascade run; and the summary skinc prints. */
#include "front.h"

#include "../src/app/js.h"
#include "../src/base/map.h"
#include "../src/base/paths.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

/* A skin's stylesheet, at most: the largest of the skins here is 13 KB,
 * and the cascade's time grows with the rules it holds. */
#define MAX_CSS_BYTES ((size_t)1 << 20)

/* path's bytes, up to one past cap. */
static char *slurp(const char *path, size_t cap, size_t *n)
{
    /* path is real: a link there, or a fifo, was swapped in since. */
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    struct stat st;
    if (fd >= 0 && (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode))) {
        close(fd);
        return NULL;
    }
    if (fd < 0)
        return NULL;
    sk_buf b = {0};
    char chunk[65536];
    ssize_t r = 0;
    while (b.len <= cap && (r = read(fd, chunk, sizeof chunk)) > 0)
        sk_buf_append(&b, chunk, (size_t)r);
    close(fd);
    if (r < 0) {
        sk_buf_free(&b);
        return NULL;
    }
    *n = b.len;
    if (!b.ptr)
        sk_buf_reserve(&b, 0);
    return b.ptr;
}

/* validate.js's checks of the stylesheet, made where it makes them:
 * every @import, then every url(). */
static void check_css(void *data, const char *path, const char *shown, sk_diags *d)
{
    sk_front *f = data;
    size_t n = 0;
    char *text = NULL;
    if (f->css_text) {
        n = f->css_len;
        text = sk_xmalloc(n + 1);
        if (n)
            memcpy(text, f->css_text, n);
        text[n] = 0;
    } else {
        text = slurp(path, MAX_CSS_BYTES, &n);
    }
    if (!text)
        return;
    if (n > MAX_CSS_BYTES) {
        sk_diag(d, shown, "(file)", "is larger than %zu KiB, the most a skin's stylesheet may be",
                MAX_CSS_BYTES >> 10);
        free(text);
        return;
    }
    f->css_shown = sk_xstrdup(shown);
    f->css_path = sk_xstrdup(path);
    sk_buf dir = {0};
    sk_str dn = sk_js_dirname(sk_str_c(path));
    sk_buf_append_str(&dir, dn);
    f->css_dir = dir.ptr ? dir.ptr : sk_xstrdup("/");
    sk_css_parse(&f->css, f->css_shown, text, n, &f->css_diags);
    f->has_css = true;
    free(text);
    for (size_t i = 0; i < f->css.imports.len; i++)
        sk_diag_line(d, shown, f->css.imports.ptr[i], "@import is not allowed");
    for (size_t i = 0; i < f->css.urls.len; i++) {
        sk_str v = sk_js_trim(f->css.urls.ptr[i].value);
        if (v.len >= 11 && strncasecmp(v.ptr, "data:image/", 11) == 0)
            continue;
        char *target = sk_xstrndup(v.ptr ? v.ptr : "", v.len);
        sk_buf field = {0};
        sk_buf_printf(&field, "line %u: url(%s)", (unsigned)f->css.urls.ptr[i].line, target);
        free(sk_skin_file(&f->skin, target, sk_buf_cstr(&field), sk_ext_image_font, f->css_dir, shown, d));
        sk_buf_free(&field);
        free(target);
    }
}

/* Every class name a selector list names that starts with span-. */
static void spans_in(sk_front *f, const sk_selector_list *l)
{
    for (size_t i = 0; l && i < l->n; i++)
        for (size_t k = 0; k < l->items[i].n; k++) {
            const sk_compound *c = &l->items[i].compounds[k];
            for (size_t j = 0; j < c->n; j++) {
                const sk_simple *x = &c->simples[j];
                if (x->list)
                    spans_in(f, x->list);
                if (x->kind != SK_SEL_CLASS || !sk_str_prefix(x->name, SK_STR("span-")))
                    continue;
                bool seen = false;
                for (size_t s = 0; s < f->spans.len && !seen; s++)
                    seen = sk_str_eq_c(x->name, f->spans.ptr[s]);
                if (!seen && f->spans.len < 29)
                    sk_vec_push(&f->spans, sk_arena_strndup(&f->arena, x->name.ptr, x->name.len));
            }
        }
}

static const sk_css_style *root_parent(void)
{
    static sk_css_style s;
    static bool ready;
    if (!ready) {
        sk_style_initial(&s, (sk_css_color){0, 0, 0, 1});
        ready = true;
    }
    return &s;
}

/* A declaration checked once, in no element's context: its property
 * known and, where its value holds no var(), the value valid. */
static void check_decl(sk_front *f, sk_xdecl *x, bool keyframe)
{
    const sk_css_decl *d = x->d;
    if (x->custom)
        return;
    if (!x->prop && !x->sh) {
        sk_diag_line(f->d, x->sheet->file, d->line, "the property \"%.*s\" is not supported", (int)d->name.len,
                     d->name.ptr);
        x->reported = true;
        return;
    }
    if (keyframe && x->prop != SK_PROP_TRANSFORM && x->prop != SK_PROP_OPACITY &&
        x->prop != SK_PROP_ANIMATION_TIMING_FUNCTION) {
        sk_diag_line(f->d, x->sheet->file, d->line, "a keyframe may set transform and opacity only, not \"%.*s\"",
                     (int)d->name.len, d->name.ptr);
        x->reported = true;
        return;
    }
    if (x->wide || x->var)
        return;
    sk_vctx c;
    sk_vctx_init(f, &c, root_parent());
    c.internal = x->internal;
    f->url_base = x->internal || !f->css_dir ? f->skin.real : f->css_dir;
    sk_css_style s;
    sk_style_initial(&s, c.color);
    bool ok = x->prop ? sk_prop_parse(x->prop, &c, d->value, d->n, &s)
                      : sk_shorthand_parse(x->sh, &c, d->value, d->n, &s);
    if (ok)
        return;
    x->reported = true;
    /* A url the file checks refused is said once, there. */
    if (c.why && !strcmp(c.why, "the url names no image of the skin"))
        return;
    sk_buf v = {0};
    sk_cv_print(&v, d->value, d->n);
    if (v.len > 80) {
        v.len = 77;
        sk_buf_append_c(&v, "...");
    }
    sk_diag_line(f->d, x->sheet->file, d->line, "\"%.*s\": \"%s\" is not a valid value%s%s", (int)d->name.len,
                 d->name.ptr, sk_buf_cstr(&v), c.why ? ": " : "", c.why ? c.why : "");
    sk_buf_free(&v);
}

/* A sheet's rules as cascade entries, each declaration checked. */
static void entries(sk_front *f, sk_css_sheet *sheet, uint8_t layer, uint32_t *order)
{
    for (size_t i = 0; i < sheet->rules.len; i++) {
        sk_css_rule *r = &sheet->rules.ptr[i];
        sk_xdecl *x = r->ndecls ? sk_arena_array(&f->arena, r->ndecls, sizeof *x) : NULL;
        for (size_t k = 0; k < r->ndecls; k++) {
            sk_xdecl_init(&x[k], &r->decls[k], sheet, false);
            check_decl(f, &x[k], false);
        }
        for (size_t s = 0; s < r->sel->n; s++) {
            sk_entry e;
            memset(&e, 0, sizeof e);
            e.sel = &r->sel->items[s];
            e.decls = x;
            e.ndecls = r->ndecls;
            e.spec = e.sel->spec;
            e.order = (*order)++;
            e.layer = layer;
            e.rule = r;
            e.sheet = sheet;
            sk_vec_push(&f->entries, e);
        }
    }
}

static int by_offset(const void *a, const void *b)
{
    float x = ((const sk_pack_key *)a)->offset, y = ((const sk_pack_key *)b)->offset;
    return x < y ? -1 : x > y ? 1 : 0;
}

/* The @keyframes rules, the last of a name winning. Each key holds the
 * transform and opacity of the frames at its offset, later ones
 * winning, computed in the root's context. */
static void keyframes(sk_front *f, sk_css_sheet *sheet)
{
    /* The names first, so animation-name can find a later rule; each
     * name's track by a map, as a skin may hold thousands. */
    sk_map index;
    sk_map_init(&index);
    for (size_t j = 0; j < f->keyframes.len; j++)
        *sk_map_put(&index, sk_str_c(f->keyframes.ptr[j].name)) = (void *)(uintptr_t)(j + 1);
    for (size_t i = 0; i < sheet->keyframes.len; i++) {
        sk_css_keyframes *k = &sheet->keyframes.ptr[i];
        void **slot = sk_map_put(&index, k->name);
        if (*slot)
            continue;
        sk_pack_keyframes kf = {sk_arena_strndup(&f->arena, k->name.ptr, k->name.len), NULL, 0};
        sk_vec_push(&f->keyframes, kf);
        *slot = (void *)(uintptr_t)f->keyframes.len;
    }
    for (size_t i = 0; i < sheet->keyframes.len; i++) {
        sk_css_keyframes *k = &sheet->keyframes.ptr[i];
        SK_VEC(sk_pack_key) keys = {0};
        bool too_many = false;
        for (size_t fr = 0; fr < k->nframes; fr++) {
            sk_css_frame *frame = &k->frames[fr];
            sk_xdecl *x = frame->ndecls ? sk_arena_array(&f->arena, frame->ndecls, sizeof *x) : NULL;
            for (size_t o = 0; o < frame->noffsets; o++) {
                sk_pack_key key;
                memset(&key, 0, sizeof key);
                key.offset = frame->offsets[o];
                for (size_t d = 0; d < frame->ndecls; d++) {
                    if (o == 0) {
                        sk_xdecl_init(&x[d], &frame->decls[d], sheet, false);
                        check_decl(f, &x[d], true);
                    }
                    if (x[d].reported || x[d].wide || x[d].var || x[d].custom)
                        continue;
                    sk_vctx c;
                    sk_vctx_init(f, &c, root_parent());
                    sk_css_style s;
                    sk_style_initial(&s, c.color);
                    if (!sk_prop_parse(x[d].prop, &c, frame->decls[d].value, frame->decls[d].n, &s))
                        continue;
                    if (x[d].prop == SK_PROP_TRANSFORM) {
                        key.has_transform = true;
                        key.transform = s.transform;
                        key.ntransform = s.ntransform;
                    } else if (x[d].prop == SK_PROP_OPACITY) {
                        key.has_opacity = true;
                        key.opacity = s.opacity;
                    } else {
                        key.has_timing = true;
                        key.timing = s.anim_timing[0];
                    }
                }
                /* A frame at an offset another frame took merges into it. */
                size_t same = keys.len;
                for (size_t q = 0; q < keys.len; q++)
                    if (keys.ptr[q].offset == key.offset)
                        same = q;
                if (same == keys.len && keys.len >= SK_PACK_MAX_LIST) {
                    too_many = true;
                } else if (same == keys.len) {
                    sk_vec_push(&keys, key);
                } else {
                    sk_pack_key *old = &keys.ptr[same];
                    if (key.has_transform) {
                        old->has_transform = true;
                        old->transform = key.transform;
                        old->ntransform = key.ntransform;
                    }
                    if (key.has_opacity) {
                        old->has_opacity = true;
                        old->opacity = key.opacity;
                    }
                    if (key.has_timing) {
                        old->has_timing = true;
                        old->timing = key.timing;
                    }
                }
            }
        }
        if (keys.len > 1)
            qsort(keys.ptr, keys.len, sizeof *keys.ptr, by_offset);
        if (too_many)
            sk_diag_line(f->d, f->css_shown ? f->css_shown : "skin.css", k->line,
                         "@keyframes %.*s has more than %u keyframes, the most a pack holds", (int)k->name.len,
                         k->name.ptr, SK_PACK_MAX_LIST);
        sk_pack_keyframes *t = &f->keyframes.ptr[(uintptr_t)sk_map_get(&index, k->name) - 1];
        sk_pack_key *kept = keys.len ? sk_arena_array(&f->arena, keys.len, sizeof *kept) : NULL;
        if (keys.len)
            memcpy(kept, keys.ptr, keys.len * sizeof *kept);
        t->keys = kept;
        t->nkeys = (uint16_t)keys.len;
        sk_vec_free(&keys);
    }
    sk_map_free(&index);
}

static void internal_sheet(sk_css_sheet *sheet, const char *name, const char *text)
{
    sk_diags d = {0};
    sk_css_parse(sheet, name, text, strlen(text), &d);
    if (d.items.len)
        sk_die("skinc: %s does not parse: %s", name, d.items.ptr[0]);
    sk_diags_free(&d);
}

static bool run(sk_front *f, const char *dir, sk_diags *d)
{
    sk_arena_init(&f->arena, 0);
    f->d = d;
    size_t before = d->items.len;
    internal_sheet(&f->ua, "(user agent)", sk_ua_css);
    internal_sheet(&f->app, "app/renderer/widgets.css", sk_widgets_css());
    f->skin_ok = sk_skin_load(&f->skin, dir, d, check_css, f);
    for (size_t i = 0; i < f->css_diags.items.len; i++) {
        sk_vec_push(&d->items, f->css_diags.items.ptr[i]);
        f->css_diags.items.ptr[i] = NULL;
    }
    sk_vec_free(&f->css_diags.items);
    if (!f->skin.real)
        return false;
    keyframes(f, &f->app);
    if (f->has_css)
        keyframes(f, &f->css);
    uint32_t order = 0;
    entries(f, &f->ua, SK_LAYER_UA, &order);
    entries(f, &f->app, SK_LAYER_AUTHOR, &order);
    if (f->has_css)
        entries(f, &f->css, SK_LAYER_AUTHOR, &order);
    if (!f->skin_ok || d->items.len != before)
        return false;
    for (size_t i = 0; i < f->entries.len; i++) {
        sk_selector_list one = {(sk_selector *)f->entries.ptr[i].sel, 1};
        spans_in(f, &one);
    }
    sk_tree_build(f);
    sk_cascade_run(f);
    return d->items.len == before;
}

bool sk_front_run(sk_front *f, const char *dir, sk_diags *d)
{
    memset(f, 0, sizeof *f);
    return run(f, dir, d);
}

bool sk_front_run_text(sk_front *f, const char *dir, const char *css, size_t n, size_t max_states, sk_diags *d)
{
    memset(f, 0, sizeof *f);
    f->css_text = css ? css : "";
    f->css_len = css ? n : 0;
    f->max_states = max_states;
    return run(f, dir, d);
}

void sk_front_free(sk_front *f)
{
    sk_skin_free(&f->skin);
    sk_css_sheet_free(&f->ua);
    sk_css_sheet_free(&f->app);
    if (f->has_css)
        sk_css_sheet_free(&f->css);
    sk_css_sheet_free(&f->inline_sheet);
    sk_css_sheet_free(&f->hint_sheet);
    for (size_t i = 0; i < f->images.len; i++)
        free(f->images.ptr[i].real);
    sk_vec_free(&f->images);
    sk_vec_free(&f->entries);
    sk_vec_free(&f->els);
    sk_vec_free(&f->styles);
    sk_vec_free(&f->spans);
    sk_vec_free(&f->keyframes);
    sk_diags_free(&f->css_diags);
    if (f->dedupe) {
        sk_map_free(f->dedupe);
        free(f->dedupe);
    }
    free(f->css_path);
    free(f->css_shown);
    free(f->css_dir);
    sk_arena_free(&f->arena);
    memset(f, 0, sizeof *f);
}

/* ---- Lookups ---- */

const sk_el *sk_front_find(const sk_front *f, const char *widget, uint8_t role, int nth)
{
    for (size_t i = 0; i < f->els.len; i++) {
        const sk_el *e = f->els.ptr[i];
        if (e->widget < 0 || e->role != role || strcmp(f->skin.widgets[e->widget].w.id, widget) != 0)
            continue;
        if (nth-- == 0)
            return e;
    }
    return NULL;
}

const sk_el *sk_front_panel(const sk_front *f, const char *klass)
{
    for (size_t i = 0; i < f->els.len; i++) {
        const sk_el *e = f->els.ptr[i];
        if (e->role != SK_ROLE_PANEL)
            continue;
        for (uint16_t k = 0; k < e->nclasses; k++)
            if (!strcmp(e->classes[k], klass))
                return e;
    }
    return NULL;
}

const sk_css_style *sk_front_style(const sk_front *f, const sk_el *el, const sk_assign *a, size_t n)
{
    if (!el || !el->ntable)
        return NULL;
    size_t idx = 0, stride = 1;
    for (uint8_t i = 0; i < el->ninputs; i++) {
        uint8_t v = 0;
        for (size_t k = 0; k < n; k++)
            if (a[k].owner == el->inputs[i].owner && a[k].var == el->inputs[i].var)
                v = a[k].value;
        if (v >= el->inputs[i].radix)
            v = 0;
        idx += v * stride;
        stride *= el->inputs[i].radix;
    }
    return f->styles.ptr[el->table[idx]];
}

/* ---- The summary ---- */

static void el_name(sk_buf *b, const sk_front *f, const sk_el *e)
{
    static const char *const KIND[] = {":host", "link", "div", "span", "svg", "g", "path", "line", "circle", "text",
                                       "image", "::before", "::after"};
    sk_buf_append_c(b, KIND[e->kind]);
    for (uint16_t i = 0; i < e->nclasses; i++)
        sk_buf_printf(b, ".%s", e->classes[i]);
    if (e->widget >= 0)
        sk_buf_printf(b, " [%s]", f->skin.widgets[e->widget].w.id);
    if (e->group)
        sk_buf_printf(b, " meter %u of %u", (unsigned)e->meter, (unsigned)e->group);
}

void sk_front_summary(const sk_front *f, FILE *out, bool verbose)
{
    const sk_skin *s = &f->skin;
    size_t pseudo = 0, dynamic = 0, max_states = 0;
    for (size_t i = 0; i < f->els.len; i++) {
        const sk_el *e = f->els.ptr[i];
        pseudo += e->kind == SK_EL_BEFORE || e->kind == SK_EL_AFTER;
        dynamic += e->ntable > 1;
        max_states = SK_MAX(max_states, e->ntable);
    }
    size_t matched = 0, author = 0;
    for (size_t i = 0; i < f->entries.len; i++) {
        if (f->entries.ptr[i].layer != SK_LAYER_AUTHOR)
            continue;
        author++;
        matched += f->entries.ptr[i].matched > 0;
    }
    fprintf(out, "skin %s %s (%s)\n", s->name, s->version ? s->version : "", s->dir);
    fprintf(out, "manifest: %zu panels, %zu widgets, %zu splits, %zu fonts, %zu sounds\n", s->npanels, s->nwidgets,
            s->nsplits, s->nfonts, s->nsounds);
    fprintf(out, "css: %s: %zu rules, %zu keyframes, %zu images named\n", f->css_shown ? f->css_shown : "(none)",
            f->has_css ? f->css.rules.len : (size_t)0, f->keyframes.len, f->images.len);
    fprintf(out, "tree: %zu elements (%zu pseudo-elements), %zu change with state, at most %zu states\n", f->els.len,
            pseudo, dynamic, max_states);
    fprintf(out, "states: %zu in all, %zu distinct styles\n", f->states, f->styles.len);
    fprintf(out, "rules: %zu of %zu author selectors match some element\n", matched, author);
    for (size_t i = 0; i < f->entries.len; i++) {
        const sk_entry *e = &f->entries.ptr[i];
        if (e->layer == SK_LAYER_AUTHOR && !e->matched && e->sheet == &f->css) {
            sk_buf b = {0};
            sk_selector_print(&b, e->sel);
            fprintf(out, "  unmatched: %s:%u: %s\n", e->sheet->file, (unsigned)e->rule->line, sk_buf_cstr(&b));
            sk_buf_free(&b);
        }
    }
    if (!verbose)
        return;
    sk_css_style init;
    sk_style_initial(&init, (sk_css_color){0, 0, 0, 1});
    for (size_t i = 0; i < f->els.len; i++) {
        const sk_el *e = f->els.ptr[i];
        sk_buf b = {0};
        el_name(&b, f, e);
        fprintf(out, "\n#%zu %s\n", i, sk_buf_cstr(&b));
        sk_buf_free(&b);
        if (e->ninputs) {
            fprintf(out, "  inputs:");
            for (uint8_t k = 0; k < e->ninputs; k++)
                fprintf(out, " %s@%u", sk_var_name(e->inputs[k].var), (unsigned)e->inputs[k].owner->index);
            fprintf(out, " (%zu states)\n", e->ntable);
        }
        /* Each distinct style of the element once. */
        for (size_t t = 0; t < e->ntable; t++) {
            bool seen = false;
            for (size_t u = 0; u < t && !seen; u++)
                seen = e->table[u] == e->table[t];
            if (seen)
                continue;
            fprintf(out, "  style %u:", (unsigned)e->table[t]);
            const sk_css_style *st = f->styles.ptr[e->table[t]];
            for (uint16_t id = 1; id < SK_PROP_COUNT; id++) {
                const sk_propdef *d = sk_prop_def(id);
                if (!d || sk_prop_equal(id, st, &init))
                    continue;
                sk_buf v = {0};
                sk_prop_print(&v, id, st);
                fprintf(out, " %s: %s;", d->name, sk_buf_cstr(&v));
                sk_buf_free(&v);
            }
            fputc('\n', out);
        }
    }
}
