/* A scripted session for the visualizer's tools and tests: what one
 * tab's frames look like through a loop of 40 s, as app/viz.js would
 * feed them. Two turns of work, a prompt, tool calls of every class,
 * output, a subagent, a compaction and a stop, then a long idle spell.
 *
 *   0 s       a prompt; the agent works: output, tool calls, a subagent
 *   10 s      the stop; idle, the prompt typed again at 13 s
 *   14 s      a prompt; tool calls, a compaction at 16.6 s, a subagent
 *   22 s      the stop; idle to the loop's end */
#ifndef SK_VIZ_SCRIPT_H
#define SK_VIZ_SCRIPT_H

#include "viz.h"

#define SK_VIZ_SCRIPT_LOOP 40.0

/* The pane's frame at time t, its events those in (t0, t]: up to cap
 * numbers of events into buf, which f->events then points to. */
void sk_viz_script(double t0, double t, sk_viz_pane_frame *f, int *buf, size_t cap);

/* A mode run on the script at 60 frames a second, as vizshot and the
 * golden tests run it: the synthesiser and the renderer seeded alike
 * every time. */
#define SK_VIZ_RUN_FPS 60

typedef struct {
    sk_synth synth;
    sk_vmode *mode;
    double t; /* the script's time of the last frame */
    int frame;
    int w, h;
    double scale;
    int buf[1024];
} sk_viz_run;

/* False when mode has no renderer here. start: the script's time of
 * the first frame. */
bool sk_viz_run_init(sk_viz_run *r, int mode, double start, int w, int h, double scale);
/* The next frame's input: the script fed to the synthesiser, stepped. */
void sk_viz_run_feed(sk_viz_run *r);
/* The frame drawn. */
const sk_image *sk_viz_run_draw(sk_viz_run *r);
void sk_viz_run_free(sk_viz_run *r);

#endif
