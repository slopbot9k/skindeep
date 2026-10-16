/* The modes (viz.h), in app/viz/modes.js's order, and the renderers'
 * common part: the picture each keeps, made anew at a new size. */
#include "internal.h"

const sk_viz_mode_info sk_viz_modes[SK_VIZ_MODES] = {
    {"drift", "Drift", SK_VIZ_MILK},
    {"rings", "Rings", SK_VIZ_MILK},
    {"plasma", "Plasma", SK_VIZ_MILK},
    {"stardust", "Star dust", SK_VIZ_MILK},
    {"whirlpool", "Whirlpool", SK_VIZ_MILK},
    {"scope", "Scope", SK_VIZ_CANVAS},
    {"analyzer", "Spectrum analyzer", SK_VIZ_CANVAS},
    {"fire", "Fire analyzer", SK_VIZ_CANVAS},
    {"superscope", "Superscope", SK_VIZ_CANVAS},
    {"dotplane", "Dot plane", SK_VIZ_CANVAS},
    {"fern", "Fern", SK_VIZ_CANVAS},
    {"go", "Go", SK_VIZ_CANVAS},
    {"zoom", "Zoom out", SK_VIZ_CANVAS},
    {"gravity", "Gravity", SK_VIZ_CANVAS},
    {"gravity3d", "Gravity 3D", SK_VIZ_CANVAS},
    {"quantum", "Quantum", SK_VIZ_CANVAS},
    {"carbon", "Carbon", SK_VIZ_CANVAS},
};

/* Each Canvas2D mode's class, by the table's index. */
static const sk_vmode_class *class_of(int mode)
{
    /* The five butterchurn modes before them have none. */
    static const sk_vmode_class *const classes[SK_VIZ_MODES] = {
        [5] = &sk_vmode_scope,
        [6] = &sk_vmode_analyzer,
        [7] = &sk_vmode_fire,
        [8] = &sk_vmode_superscope,
        [9] = &sk_vmode_dotplane,
        [10] = &sk_vmode_fern,
        [11] = &sk_vmode_go,
        [12] = &sk_vmode_zoom,
        [13] = &sk_vmode_gravity,
        [14] = &sk_vmode_gravity3d,
        [15] = &sk_vmode_quantum,
        [16] = &sk_vmode_carbon,
    };
    return mode >= 0 && mode < SK_VIZ_MODES ? classes[mode] : NULL;
}

int sk_viz_mode_find(const char *id)
{
    for (int i = 0; i < SK_VIZ_MODES; i++)
        if (id && strcmp(sk_viz_modes[i].id, id) == 0)
            return i;
    return -1;
}

int sk_viz_scope_mode(void)
{
    return sk_viz_mode_find("scope");
}

sk_vmode *sk_vmode_new(int mode, uint64_t seed)
{
    const sk_vmode_class *cls = class_of(mode);
    if (!cls)
        return NULL;
    sk_vmode *m = sk_xcalloc(1, cls->size);
    m->cls = cls;
    m->index = mode;
    m->scale = 1;
    sk_rng_seed(&m->rng, seed);
    sk_vc_init(&m->vc, &m->canvas, 1);
    if (cls->init)
        cls->init(m);
    return m;
}

void sk_vmode_free(sk_vmode *m)
{
    if (!m)
        return;
    if (m->cls->fini)
        m->cls->fini(m);
    sk_vc_free(&m->vc);
    sk_image_free(&m->canvas);
    sk_vec_free(&m->ev);
    free(m);
}

int sk_vmode_index(const sk_vmode *m)
{
    return m->index;
}

const sk_image *sk_vmode_draw(sk_vmode *m, sk_synth *s, double dt, int w, int h, double scale)
{
    if (!(scale > 0 && scale <= 8))
        scale = 1;
    w = SK_MAX(0, SK_MIN(w, 16384));
    h = SK_MAX(0, SK_MIN(h, 16384));
    if (w != m->w || h != m->h || scale != m->scale || !m->canvas.px) {
        sk_image_free(&m->canvas);
        sk_image_init(&m->canvas, (int)floor(w * scale + 0.5), (int)floor(h * scale + 0.5));
        sk_fill_rect(&m->canvas, sk_image_bounds(&m->canvas), SK_RGB(0, 0, 0), SK_BLEND_SRC, NULL);
        m->w = w;
        m->h = h;
        m->scale = scale;
        m->fresh = true;
    }
    if (dt < 0 || !isfinite(dt))
        dt = 0;
    sk_vc_target(&m->vc, &m->canvas, scale);
    m->cls->draw(m, s, dt);
    m->fresh = false;
    return &m->canvas;
}

bool sk_vmode_pointer(sk_vmode *m, int kind, double x, double y, bool down)
{
    if (!m->cls->pointer)
        return false;
    if (isfinite(x) && isfinite(y))
        m->cls->pointer(m, kind, x, y, down);
    return true;
}

bool sk_vmode_has_pointer(const sk_vmode *m)
{
    return m->cls->pointer != NULL;
}
