/* The visualizer's five MilkDrop modes (decision 0015), drawn in
 * software. Drift, Rings, Plasma, Star dust and Whirlpool are the
 * butterchurn presets this project wrote in app/viz/presets.js; their
 * equations and shaders are translated by hand into the C they compute
 * (milkpresets.c), and the renderer follows what butterchurn 2.6.7 does
 * with them on WebGL2 (milk.c), down to its 8-bit textures.
 *
 * Butterchurn is Copyright (c) 2013-2018 Jordan Berg, MIT licence
 * (third_party/butterchurn/LICENSE). No line of it is vendored: the
 * renderer is ported from its JavaScript and GLSL.
 *
 * A pane owns one sk_milk. A frame depends only on what it is given:
 * the samples, the seconds since the last frame, the seed, and the
 * frames before it. Nothing reads the clock or rand(). */
#ifndef SK_VIZ_MILK_H
#define SK_VIZ_MILK_H

#include "../gfx/image.h"

/* Samples a frame takes: butterchurn's analyser, an fftSize of 1024 at
 * a nominal 44.1 kHz. */
#define SK_MILK_SAMPLES 1024
/* Butterchurn's halves: 512 samples for the waves, 512 spectrum bins. */
#define SK_MILK_WAVE 512

/* In the order of app/viz/modes.js. */
typedef enum {
    SK_MILK_DRIFT,
    SK_MILK_RINGS,
    SK_MILK_PLASMA,
    SK_MILK_STARDUST,
    SK_MILK_WHIRLPOOL,
    SK_MILK_PRESETS
} sk_milk_preset;

/* The preset's id in modes.js, "drift" to "whirlpool"; NULL past the
 * end. */
const char *sk_milk_name(int preset);
/* The preset of that id, or -1. */
int sk_milk_find(const char *id);

/* ---- Audio ----
 *
 * What butterchurn's AudioProcessor and AudioLevels make of a frame's
 * samples. A sample is -1..1 and is first made the byte the synthesiser
 * hands butterchurn, round(128 + 127 s), as synth.js bytes() does. */
typedef struct {
    /* The bands and their attenuated levels, each near 1 for a steady
     * sound: the band's sum of the spectrum over its long average. */
    double bass, mid, treb, bass_att, mid_att, treb_att;
    /* The 1024 samples as signed bytes, -128..127: what the spectrum
     * is taken of. */
    int8_t time[SK_MILK_SAMPLES];
    /* The left and right waves the waveforms draw: each sample averaged
     * with the one before, every second one kept. The two are the same,
     * as the synthesiser sends one channel to both. */
    int8_t wave_l[SK_MILK_WAVE], wave_r[SK_MILK_WAVE];
    /* The equalized magnitude spectrum, bin k at k * 44100 / 1024 Hz. */
    double spectrum[SK_MILK_WAVE];
    /* Their running state. */
    double imm[3], avg[3], long_avg[3];
} sk_milk_audio;

void sk_milk_audio_init(sk_milk_audio *a);
/* One frame: samples is SK_MILK_SAMPLES long. fps is the renderer's
 * frame rate, which sets how fast the averages move; frame counts from
 * 1, and the long average settles faster in the first 50. */
void sk_milk_audio_update(sk_milk_audio *a, const float *samples, double fps, int frame);

/* ---- The renderer ---- */

typedef struct sk_milk sk_milk;

/* A renderer showing preset, its random draws (the hue's phases, Star
 * dust's dots, Whirlpool's scribbles, a blend's pattern) from seed. */
sk_milk *sk_milk_new(int preset, uint64_t seed);
void sk_milk_free(sk_milk *m);
/* Butterchurn's loadPreset(preset, blend): the picture so far stays,
 * and the new preset takes over from the one showing through a pattern
 * over blend seconds, or at once for 0. viz.js blends 1.5 s from one
 * MilkDrop mode to another. */
void sk_milk_load(sk_milk *m, int preset, double blend);
int sk_milk_preset_of(const sk_milk *m);

/* One frame into out, all of it. samples: the newest SK_MILK_SAMPLES,
 * -1..1. dt: the seconds since the frame before, which butterchurn
 * measures and this is told. The picture is rendered at out's size
 * over scale, as butterchurn renders at the pane's CSS pixels, and
 * stretched bilinearly to out as the browser stretches the canvas. A
 * change of size resamples the picture so far. */
void sk_milk_frame(sk_milk *m, const float *samples, double dt, sk_image *out, int scale);

/* The levels of the last frame. */
const sk_milk_audio *sk_milk_levels(const sk_milk *m);

/* The passes over rows run on a pool of up to 8 threads that all
 * renderers share, one pass at a time. This caps the threads a pass
 * uses, the caller's own among them; 0, the default, uses them all.
 * The picture is the same either way. */
void sk_milk_set_threads(int n);

/* A test signal for frame n, SK_MILK_SAMPLES long, frames 1/60 s
 * apart: three tones that drift in pitch and loudness, a bass thump
 * every half second, and bursts of noise, from n alone and made in
 * integers, so it is the same to the bit on every machine. The tools
 * and the tests drive the renderer with it. */
void sk_milk_demo_signal(int n, float *out);

#endif
