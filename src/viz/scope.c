/* Scope mode (decision 0015), app/viz/scope.js: an oscilloscope over
 * spectrum bars, and a ring for each event, coloured by its tool class
 * or its kind. Each frame fades the last. */
#include "internal.h"

/* An event's hue by its kind: output, prompt, stop, subagent; the
 * compaction's 0 is falsy in JavaScript, so it falls to 160 as an
 * unknown kind does. */
static double kind_hue(int kind)
{
    switch (kind) {
    case SK_EV_PROMPT:
        return 60;
    case SK_EV_STOP:
        return 190;
    case SK_EV_AGENT:
        return 300;
    default:
        return 160;
    }
}

static void draw(sk_vmode *m, sk_synth *s, double dt)
{
    (void)dt;
    sk_vc *g = &m->vc;
    double w = m->w, h = m->h;
    const sk_synth_levels *L = &s->level;
    sk_vc_paint p = sk_vc_solid(sk_vc_rgba(0, 0, 0, 0.22));
    sk_vc_fill_rect(g, 0, 0, w, h, &p);

    /* Spectrum along the bottom. */
    double bw = w / SK_SYNTH_BANDS;
    for (int b = 0; b < SK_SYNTH_BANDS; b++) {
        double v = SK_MIN(1, sk_vm_log10(1 + 30 * s->bands[b]) / 1.2);
        double bh = v * h * 0.38;
        p = sk_vc_solid(sk_vc_hsla(140 - b * 2.6 + 60 * L->treble, 90, 35 + 30 * v, 0.85));
        sk_vc_fill_rect(g, b * bw + 1, h - bh, bw - 2, bh, &p);
    }

    /* Event rings, coloured by tool class or event kind. */
    sk_vc_line_width(g, 3);
    for (size_t i = 0; i < s->flashes.len; i++) {
        const sk_synth_flash *f = &s->flashes.ptr[i];
        double age = s->t - f->t;
        double hue = f->kind == SK_EV_TOOL ? sk_viz_class_hue(f->cls) : kind_hue(f->kind);
        double cx = w * (f->kind == SK_EV_TOOL ? 0.15 + 0.14 * f->cls : 0.5), cy = h * 0.42;
        p = sk_vc_solid(sk_vc_hsla(hue, 95, 60, SK_MAX(0, 1 - age / 1.5)));
        sk_vc_begin(g);
        sk_vc_arc(g, cx, cy, 10 + age * w * 0.25, 0, SK_TAU, false);
        sk_vc_stroke(g, &p);
    }

    /* The waveform, glowing. */
    sk_color c = sk_vc_hsl(150 - 110 * L->treble + 40 * L->busy, 100, 55 + 15 * L->bass);
    sk_vc_line_width(g, 2);
    sk_vc_begin(g);
    for (int i = 0; i < SK_SYNTH_N; i++) {
        double x = (double)i / (SK_SYNTH_N - 1) * w, y = h * 0.42 - s->samples[i] * h * 0.32;
        if (i)
            sk_vc_line(g, x, y);
        else
            sk_vc_move(g, x, y);
    }
    p = sk_vc_solid(c);
    sk_vc_stroke_glow(g, &p, c, 12);
}

const sk_vmode_class sk_vmode_scope = {sizeof(sk_vmode), NULL, draw, NULL, NULL};
