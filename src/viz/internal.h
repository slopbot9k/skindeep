/* What the visualizer's modes share and viz.h does not declare: a
 * renderer's common part, each mode's class, and the small helpers the
 * JavaScript modes repeat. */
#ifndef SK_VIZ_INTERNAL_H
#define SK_VIZ_INTERNAL_H

#include "canvas.h"
#include "viz.h"

#include <math.h>
#include <string.h>

/* A mode's methods. size: its struct's, sk_vmode first. */
typedef struct {
    size_t size;
    void (*init)(sk_vmode *m);
    /* One frame into m->vc, the pane m->w by m->h points. */
    void (*draw)(sk_vmode *m, sk_synth *s, double dt);
    /* NULL: no answer of its own. */
    void (*pointer)(sk_vmode *m, int kind, double x, double y, bool down);
    void (*fini)(sk_vmode *m);
} sk_vmode_class;

struct sk_vmode {
    const sk_vmode_class *cls;
    int index;
    sk_image canvas; /* the picture, in pixels */
    sk_vc vc;        /* over canvas */
    int w, h;        /* points */
    double scale;
    bool fresh;      /* the canvas was made anew for this frame */
    sk_rng rng;
    sk_synth_mark mark;
    sk_viz_events ev;
};

extern const sk_vmode_class sk_vmode_scope, sk_vmode_analyzer, sk_vmode_fire, sk_vmode_superscope, sk_vmode_dotplane,
    sk_vmode_go, sk_vmode_fern, sk_vmode_zoom, sk_vmode_gravity;

/* The events since the mode's last frame, oldest first, in m->ev. */
static inline size_t sk_vmode_events(sk_vmode *m, const sk_synth *s)
{
    return sk_synth_since(s, &m->mark, &m->ev);
}

/* Math.random's place. */
static inline double sk_vmode_rand(sk_vmode *m)
{
    return sk_rng_next(&m->rng);
}

/* A tool call's hue by its class, read, edit, shell, web, agent, other,
 * and 200 for a class that is none of them (JavaScript's ?? 200). */
static inline double sk_viz_class_hue(int cls)
{
    static const double hue[] = {200, 120, 40, 0, 280, 320};
    return cls >= 0 && cls < 6 ? hue[cls] : 200;
}

/* smoothstep of x clamped to 0..1. */
static inline double sk_viz_smooth(double x)
{
    double t = sk_vm_clamp(x, 0, 1);
    return t * t * (3 - 2 * t);
}

/* JavaScript's array filter in place: the elements of a vector that
 * meet cond, kept in order. */
#define SK_VIZ_KEEP(vec, cond)                                               \
    do {                                                                     \
        size_t k_ = 0;                                                       \
        for (size_t i_ = 0; i_ < (vec)->len; i_++)                           \
            if (cond((vec)->ptr[i_]))                                        \
                (vec)->ptr[k_++] = (vec)->ptr[i_];                           \
        (vec)->len = k_;                                                     \
    } while (0)

/* Drops the first n elements of a vector, as splice(0, n). */
#define SK_VIZ_SHIFT(vec, n)                                                 \
    do {                                                                     \
        size_t n_ = SK_MIN((size_t)(n), (vec)->len);                         \
        if (n_ && n_ < (vec)->len)                                           \
            memmove((vec)->ptr, (vec)->ptr + n_, ((vec)->len - n_) * sizeof *(vec)->ptr); \
        (vec)->len -= n_;                                                    \
    } while (0)

#endif
