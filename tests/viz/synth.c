/* The visualizer's signal (synth.h), against what synth.js computes. */
#include "../../src/viz/fft.h"
#include "../../src/viz/script.h"
#include "../../src/viz/synth.h"
#include "../test.h"

#include <math.h>

static sk_viz_pane_frame frame(double out_bps, double in_kps, double idle, double rate, double busy,
                               const int *ev, size_t n)
{
    sk_viz_pane_frame f = {0};
    f.n[SK_VIZ_OUT_BPS] = out_bps;
    f.n[SK_VIZ_IN_KPS] = in_kps;
    f.n[SK_VIZ_IDLE_S] = idle;
    f.n[SK_VIZ_OUT_RATE] = rate;
    f.n[SK_VIZ_BUSY] = busy;
    f.events = ev;
    f.n_events = n;
    return f;
}

static void test_levels(void)
{
    sk_synth s;
    sk_synth_init(&s, 1);
    sk_viz_pane_frame f = frame(1250, 3, 8, 60, 1, NULL, 0);
    sk_synth_input(&s, &f);
    CHECK(fabs(s.target.bass - 0.6) < 1e-12);
    CHECK(fabs(s.target.treble - 0.5) < 1e-12);
    CHECK(s.target.busy == 1);
    CHECK(fabs(s.target.idle - 0.5) < 1e-12);
    f = frame(1e6, 1e3, 2, 1e4, 0, NULL, 0);
    sk_synth_input(&s, &f);
    CHECK(fabs(s.target.bass - 1.3) < 1e-12 && s.target.treble == 1 && s.target.busy == 0 && s.target.idle == 0);
    /* The levels ease toward the targets, 1 - e^-6 of the way in 1 s. */
    for (int i = 0; i < 60; i++)
        sk_synth_step(&s, 1.0 / 60, SK_VIZ_SRC_SESSION, NULL, 0);
    CHECK(fabs(s.level.treble - (1 - exp(-6))) < 1e-9);
    sk_synth_free(&s);
}

static void test_events(void)
{
    sk_synth s;
    sk_synth_init(&s, 1);
    sk_synth_mark mark = {0};
    sk_viz_events ev = {0};
    CHECK_INT(sk_synth_since(&s, &mark, &ev), 0);
    int e[] = {SK_EV_PROMPT, 0, SK_EV_TOOL, 3, SK_EV_TOOL, 9, 7, 0};
    sk_viz_pane_frame f = frame(0, 0, 0, 0, 0, e, SK_LEN(e));
    sk_synth_input(&s, &f);
    CHECK_INT(s.beats, 4);
    /* A kind with no voice still flashes and beats; a class with no
     * pitch takes 520 Hz. */
    CHECK_INT(s.voices.len, 3);
    CHECK(s.voices.ptr[1].hz == 880 && s.voices.ptr[2].hz == 520);
    CHECK_INT(sk_synth_since(&s, &mark, &ev), 4);
    CHECK(ev.ptr[0].kind == SK_EV_PROMPT && ev.ptr[1].cls == 3 && ev.ptr[3].kind == 7);
    CHECK_INT(sk_synth_since(&s, &mark, &ev), 0);
    /* Flashes older than 1.5 s are gone, so a mark far behind sees only
     * those left. */
    sk_synth_mark old = {0};
    sk_synth_since(&s, &old, &ev);
    old.beats = 0;
    for (int i = 0; i < 100; i++)
        sk_synth_step(&s, 1.0 / 60, SK_VIZ_SRC_SESSION, NULL, 0);
    CHECK_INT(s.flashes.len, 0);
    CHECK_INT(sk_synth_since(&s, &old, &ev), 0);
    /* At most 64 voices, the oldest dropped; at most 16 tones. */
    for (int i = 0; i < 70; i++)
        sk_synth_trigger(&s, SK_EV_OUTPUT, 0);
    CHECK_INT(s.voices.len, 64);
    /* Unstepped, as while no viz draws, the newest 256 flashes. */
    for (int i = 0; i < 1000; i++)
        sk_synth_trigger(&s, SK_EV_OUTPUT, 0);
    CHECK_INT(s.flashes.len, 256);
    for (int i = 0; i < 20; i++)
        sk_synth_touch(&s, 0.5, 0.5);
    CHECK_INT(s.touches.len, 16);
    CHECK(fabs(s.touches.ptr[0].hz - 40 * sqrt(300)) < 1e-9 && fabs(s.touches.ptr[0].amp - 0.575) < 1e-12);
    sk_vec_free(&ev);
    sk_synth_free(&s);
}

/* A tone at a band's frequency lights that band most. */
static void test_bands(void)
{
    sk_synth s;
    sk_synth_init(&s, 1);
    float tone[SK_SYNTH_N];
    for (int b = 6; b < SK_SYNTH_BANDS; b += 7) {
        double hz = sk_synth_band_hz(b);
        for (int n = 0; n < SK_SYNTH_N; n++)
            tone[n] = (float)(0.5 * sin(2 * M_PI * hz * n / SK_SYNTH_RATE));
        sk_synth_step(&s, 1.0 / 60, SK_VIZ_SRC_SOUND, tone, SK_SYNTH_N);
        int top = 0;
        for (int k = 1; k < SK_SYNTH_BANDS; k++)
            if (s.bands[k] > s.bands[top])
                top = k;
        CHECK_INT(top, b);
    }
    CHECK(fabs(sk_synth_band_hz(0) - 40) < 1e-9 && fabs(sk_synth_band_hz(SK_SYNTH_BANDS - 1) - 12000) < 1e-6);
    /* The sound output alone: its loudness rises count as output
     * events, and bytes map -1..1 to 1..255. */
    sk_synth_mark mark = {0};
    sk_viz_events ev = {0};
    float quiet[SK_SYNTH_N] = {0};
    for (int i = 0; i < 120; i++)
        sk_synth_step(&s, 1.0 / 60, SK_VIZ_SRC_SOUND, quiet, SK_SYNTH_N);
    sk_synth_since(&s, &mark, &ev);
    long rises = s.rises;
    for (int n = 0; n < SK_SYNTH_N; n++)
        tone[n] = (float)(0.6 * sin(n * 0.3));
    for (int i = 0; i < 60; i++)
        sk_synth_step(&s, 1.0 / 60, SK_VIZ_SRC_SOUND, tone, SK_SYNTH_N);
    CHECK_INT(s.rises - rises, 1);
    CHECK_INT(sk_synth_since(&s, &mark, &ev), 1);
    CHECK_INT(ev.ptr[0].kind, SK_EV_OUTPUT);
    uint8_t bytes[SK_SYNTH_N];
    s.samples[0] = -1;
    s.samples[1] = 1;
    s.samples[2] = 0;
    sk_synth_bytes(&s, bytes);
    CHECK(bytes[0] == 1 && bytes[1] == 255 && bytes[2] == 128);
    sk_vec_free(&ev);
    sk_synth_free(&s);
}

/* The same script and seed give the same samples, bit for bit. */
static void test_deterministic(void)
{
    sk_synth a, b;
    sk_synth_init(&a, 7);
    sk_synth_init(&b, 7);
    int buf[64];
    double t = -1.0 / 60;
    for (int i = 0; i < 300; i++) {
        sk_viz_pane_frame f;
        sk_viz_script(t, t + 1.0 / 60, &f, buf, SK_LEN(buf));
        t += 1.0 / 60;
        sk_synth_input(&a, &f);
        sk_synth_input(&b, &f);
        sk_synth_step(&a, 1.0 / 60, SK_VIZ_SRC_SESSION, NULL, 0);
        sk_synth_step(&b, 1.0 / 60, SK_VIZ_SRC_SESSION, NULL, 0);
    }
    CHECK(memcmp(a.samples, b.samples, sizeof a.samples) == 0);
    CHECK(memcmp(a.bands, b.bands, sizeof a.bands) == 0);
    /* 5 s of the script: the prompt and 8 events after it. */
    CHECK_INT(a.beats, 9);
    CHECK(a.level.busy > 0.99 && a.level.bass > 0.2);
    sk_synth_free(&a);
    sk_synth_free(&b);
}

static void test_fft(void)
{
    sk_fft_plan p;
    CHECK(!sk_fft_plan_init(&p, 12));
    CHECK(sk_fft_plan_init(&p, 16));
    double re[16], im[16], r0[16];
    for (int i = 0; i < 16; i++) {
        re[i] = r0[i] = sin(i * 1.3) + i;
        im[i] = cos(i * 0.7);
    }
    double sum = 0;
    for (int i = 0; i < 16; i++)
        sum += r0[i];
    sk_fft(re, im, 0, 1, &p, -1);
    CHECK(fabs(re[0] - sum) < 1e-9);
    sk_fft(re, im, 0, 1, &p, 1);
    for (int i = 0; i < 16; i++)
        CHECK(fabs(re[i] / 16 - r0[i]) < 1e-9);
    sk_fft_plan_free(&p);
    float tone[1024];
    double spec[513];
    for (int n = 0; n < 1024; n++)
        tone[n] = (float)sin(2 * M_PI * 37 * n / 1024);
    sk_fft_spectrum(tone, 1024, spec);
    int top = 0;
    for (int k = 1; k <= 512; k++)
        if (spec[k] > spec[top])
            top = k;
    CHECK_INT(top, 37);
    CHECK(fabs(spec[37] - 1) < 0.01);
}

const sk_test sk_suite_viz_synth[] = {T(test_levels), T(test_events), T(test_bands), T(test_deterministic),
                                      T(test_fft), {0}};
