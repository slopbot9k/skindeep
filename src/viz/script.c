/* The scripted session (script.h). */
#include "script.h"

#include "viz.h"

#include <math.h>
#include <string.h>

static const struct {
    double t;
    int kind, cls;
} EVENTS[] = {
    {0.0, SK_EV_PROMPT, 0},           {0.6, SK_EV_TOOL, SK_TOOL_READ},  {1.1, SK_EV_OUTPUT, 0},
    {1.5, SK_EV_TOOL, SK_TOOL_READ},  {2.2, SK_EV_TOOL, SK_TOOL_EDIT},  {2.8, SK_EV_OUTPUT, 0},
    {3.1, SK_EV_TOOL, SK_TOOL_SHELL}, {3.9, SK_EV_TOOL, SK_TOOL_WEB},   {4.4, SK_EV_AGENT, 0},
    {5.0, SK_EV_TOOL, SK_TOOL_AGENT}, {5.6, SK_EV_OUTPUT, 0},           {6.0, SK_EV_TOOL, SK_TOOL_OTHER},
    {6.5, SK_EV_TOOL, SK_TOOL_SHELL}, {7.2, SK_EV_TOOL, SK_TOOL_EDIT},  {7.8, SK_EV_OUTPUT, 0},
    {8.4, SK_EV_TOOL, SK_TOOL_READ},  {9.2, SK_EV_OUTPUT, 0},           {10.0, SK_EV_STOP, 0},
    {14.0, SK_EV_PROMPT, 0},          {14.7, SK_EV_TOOL, SK_TOOL_READ}, {15.4, SK_EV_TOOL, SK_TOOL_EDIT},
    {16.0, SK_EV_OUTPUT, 0},          {16.6, SK_EV_COMPACT, 0},         {17.3, SK_EV_TOOL, SK_TOOL_SHELL},
    {18.0, SK_EV_TOOL, SK_TOOL_WEB},  {18.8, SK_EV_OUTPUT, 0},          {19.5, SK_EV_AGENT, 0},
    {20.3, SK_EV_TOOL, SK_TOOL_EDIT}, {21.2, SK_EV_OUTPUT, 0},          {22.0, SK_EV_STOP, 0},
};

/* The two turns: busy from start to stop. */
static const double TURNS[2][2] = {{0, 10}, {14, 22}};

void sk_viz_script(double t0, double t, sk_viz_pane_frame *f, int *buf, size_t cap)
{
    memset(f, 0, sizeof *f);
    f->tab = 1;
    f->place = 1;
    f->active = true;
    double u = fmod(t, SK_VIZ_SCRIPT_LOOP);
    if (u < 0)
        u += SK_VIZ_SCRIPT_LOOP;
    bool busy = false;
    double stopped = -1;
    for (int i = 0; i < 2; i++) {
        if (u >= TURNS[i][0] && u < TURNS[i][1])
            busy = true;
        else if (u >= TURNS[i][1])
            stopped = TURNS[i][1];
    }
    /* Before the first stop of the loop, the last loop's second. */
    if (!busy && stopped < 0)
        stopped = TURNS[1][1] - SK_VIZ_SCRIPT_LOOP;
    double since = busy ? 0 : u - stopped;
    if (busy) {
        f->n[SK_VIZ_OUT_BPS] = SK_MAX(0, 1200 + 900 * sk_vm_sin(1.7 * u) + 500 * sk_vm_sin(5.3 * u));
        f->n[SK_VIZ_OUT_RATE] = 70 + 40 * sk_vm_sin(0.9 * u);
        f->n[SK_VIZ_CPU] = 40 + 30 * sk_vm_sin(0.4 * u);
    } else {
        f->n[SK_VIZ_OUT_RATE] = SK_MAX(0, 70 * (1 - since / 10));
        f->n[SK_VIZ_CPU] = 2;
    }
    f->n[SK_VIZ_IN_KPS] = u >= 13 && u < 14 ? 8 : 0;
    f->n[SK_VIZ_IDLE_S] = since;
    f->n[SK_VIZ_BUSY] = busy ? 1 : 0;
    f->n[SK_VIZ_CTX] = 20 + 50 * u / SK_VIZ_SCRIPT_LOOP;
    /* The events in (t0, t], loop by loop. */
    size_t n = 0;
    double base = floor(t0 / SK_VIZ_SCRIPT_LOOP) * SK_VIZ_SCRIPT_LOOP;
    for (; base <= t; base += SK_VIZ_SCRIPT_LOOP)
        for (size_t i = 0; i < SK_LEN(EVENTS); i++) {
            double at = base + EVENTS[i].t;
            if (at > t0 && at <= t && n + 2 <= cap) {
                buf[n++] = EVENTS[i].kind;
                buf[n++] = EVENTS[i].cls;
            }
        }
    f->events = buf;
    f->n_events = n;
}

/* The seeds of every run: the synthesiser's noise and the mode's
 * random numbers. */
#define SYNTH_SEED 0x5eed5157u
#define MODE_SEED 0x5eedc0deu

bool sk_viz_run_init(sk_viz_run *r, int mode, double start, int w, int h, double scale)
{
    memset(r, 0, sizeof *r);
    r->mode = sk_vmode_new(mode, MODE_SEED);
    if (!r->mode)
        return false;
    sk_synth_init(&r->synth, SYNTH_SEED);
    /* The first frame takes the events at start itself. */
    r->t = start - 1.0 / SK_VIZ_RUN_FPS;
    r->w = w;
    r->h = h;
    r->scale = scale;
    return true;
}

void sk_viz_run_feed(sk_viz_run *r)
{
    double t0 = r->t, t = t0 + 1.0 / SK_VIZ_RUN_FPS;
    sk_viz_pane_frame f;
    sk_viz_script(t0, t, &f, r->buf, SK_LEN(r->buf));
    sk_synth_input(&r->synth, &f);
    sk_synth_step(&r->synth, 1.0 / SK_VIZ_RUN_FPS, SK_VIZ_SRC_SESSION, NULL, 0);
    r->t = t;
}

const sk_image *sk_viz_run_draw(sk_viz_run *r)
{
    r->frame++;
    return sk_vmode_draw(r->mode, &r->synth, 1.0 / SK_VIZ_RUN_FPS, r->w, r->h, r->scale);
}

void sk_viz_run_free(sk_viz_run *r)
{
    sk_vmode_free(r->mode);
    sk_synth_free(&r->synth);
    memset(r, 0, sizeof *r);
}
