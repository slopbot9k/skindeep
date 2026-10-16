/* The visualizer's signal (decision 0015), as app/viz/synth.js builds
 * it: a synthesiser per pane, fed each frame by its tab's numbers and
 * event codes, or by the sound output's samples, or both added, that
 * yields 1024 samples at a nominal 44.1 kHz and 48 band magnitudes.
 * Every mode draws from it. No audio device is touched.
 *
 * The frame a pane receives is app/viz.js's feed(): one entry per pane,
 * numbers only. */
#ifndef SK_VIZ_SYNTH_H
#define SK_VIZ_SYNTH_H

#include "../core/bus.h"
#include "vmath.h"

#define SK_SYNTH_N 1024
#define SK_SYNTH_RATE 44100
#define SK_SYNTH_BANDS 48

/* The numbers of a pane's frame, in feed()'s order: the tab's
 * pty.out_bps, pty.in_kps, pty.idle_s, tokens.out_rate, state.busy,
 * proc.cpu_pct and context.used_pct, 0 for a metric with no number. */
enum {
    SK_VIZ_OUT_BPS,
    SK_VIZ_IN_KPS,
    SK_VIZ_IDLE_S,
    SK_VIZ_OUT_RATE,
    SK_VIZ_BUSY,
    SK_VIZ_CPU,
    SK_VIZ_CTX,
    SK_VIZ_NUMS
};

/* The tool classes of an SK_EV_TOOL event (bus.h's kinds). */
enum { SK_TOOL_READ, SK_TOOL_EDIT, SK_TOOL_SHELL, SK_TOOL_WEB, SK_TOOL_AGENT, SK_TOOL_OTHER };

/* One pane of the frame: k, i, a, m and o of feed(), then n and e. */
typedef struct {
    int tab;     /* the tab's id */
    int place;   /* its place in the strip, from 1 */
    bool active; /* the active tab's pane */
    int mode;    /* the index of its mode in sk_viz_modes */
    bool open;   /* its mode drop-down is open */
    double n[SK_VIZ_NUMS];
    const int *events; /* (kind, class) pairs, oldest first, as the bus drains them */
    size_t n_events;   /* numbers in events, twice the pairs */
} sk_viz_pane_frame;

/* The input, the menu's Visualization input. */
enum { SK_VIZ_SRC_SESSION, SK_VIZ_SRC_SOUND, SK_VIZ_SRC_BOTH };

/* An event as a mode reads it. */
typedef struct {
    int kind, cls;
} sk_viz_event;
typedef SK_VEC(sk_viz_event) sk_viz_events;

typedef struct {
    double bass, treble, busy, idle;
} sk_synth_levels;

/* A sound an event makes: kinds of synth.js, a thump, a tool's tone, a
 * chirp from one pitch to another, or a wobble. */
typedef struct {
    int type;
    double t, hz, from, to, amp, life;
} sk_synth_voice;
typedef struct {
    int kind, cls;
    double t;
} sk_synth_flash;
typedef struct {
    double t, hz, amp, life;
} sk_synth_tone;

typedef struct {
    double t; /* seconds of signal so far */
    sk_synth_levels level, target;
    float samples[SK_SYNTH_N]; /* -1..1 */
    double bands[SK_SYNTH_BANDS];
    /* The events of the last 1.5 s, oldest first. */
    SK_VEC(sk_synth_flash) flashes;
    /* The pointer's tones, at most 16, neither events nor beats. */
    SK_VEC(sk_synth_tone) touches;
    SK_VEC(sk_synth_voice) voices;
    /* Events so far: a mode that keeps the count sees a beat. */
    long beats;
    /* Rises of the sound output's loudness so far, while it drives the
     * signal alone, and the slow level a rise is measured from. */
    long rises;
    double floor;
    bool armed;
    sk_rng rng;
} sk_synth;

/* What a mode keeps to read the events since its last frame. */
typedef struct {
    bool set;
    long beats, rises;
} sk_synth_mark;

void sk_synth_init(sk_synth *s, uint64_t seed);
void sk_synth_free(sk_synth *s);
/* The pane's numbers and events of a frame: the levels' targets, and a
 * voice per event. */
void sk_synth_input(sk_synth *s, const sk_viz_pane_frame *f);
/* One event of kind and class, as a frame's would be. */
void sk_synth_trigger(sk_synth *s, int kind, int cls);
/* A tone for the pointer at u across the pane, 0 the left edge and 1
 * the right: its pitch on the bands' scale, its loudness from v, 0 the
 * top. */
void sk_synth_touch(sk_synth *s, double u, double v);
/* Advances dt seconds and fills samples and bands. src is the input;
 * sound, n of them, the sound output's newest samples, taken when n is
 * SK_SYNTH_N and src is not the session alone. */
void sk_synth_step(sk_synth *s, double dt, int src, const float *sound, size_t n);
/* The events since the mark, oldest first, into *out (emptied first);
 * the sound output alone brings none, so there each rise of its
 * loudness counts as an output event. Returns how many. */
size_t sk_synth_since(const sk_synth *s, sk_synth_mark *mark, sk_viz_events *out);
/* The samples as butterchurn's bytes, 128 + 127 x rounded. */
void sk_synth_bytes(const sk_synth *s, uint8_t out[SK_SYNTH_N]);
/* The centre frequency of band b, Hz: 40 to 12000, log spaced. */
double sk_synth_band_hz(int b);

#endif
