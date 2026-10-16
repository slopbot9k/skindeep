/* What butterchurn hears in a frame's samples (milk.h): the bytes, the
 * half-rate waves, the spectrum and the three bands' levels. */
#include "../test.h"
#include "milk_shared.h"

#include <math.h>

/* A sine of amp at exactly bin k of the 1024-point spectrum. */
static void bin_tone(float *s, int k, double amp)
{
    for (int i = 0; i < SK_MILK_SAMPLES; i++)
        s[i] = (float)(amp * sin(2 * M_PI * k * i / SK_MILK_SAMPLES));
}

/* The bytes are synth.js's round(128 + 127 s), held to 0..255, less
 * 128; each wave sample is the mean of a byte and the one before,
 * truncated, every second one kept. */
static void test_bytes(void)
{
    sk_milk_audio a;
    sk_milk_audio_init(&a);
    float s[SK_MILK_SAMPLES] = {0};
    s[0] = 1, s[1] = -1, s[2] = 1, s[3] = 0.5f, s[4] = -0.004f, s[5] = -0.012f, s[6] = 2, s[7] = NAN;
    sk_milk_audio_update(&a, s, 60, 1);
    CHECK_INT(a.time[0], 127);
    CHECK_INT(a.time[1], -127);
    CHECK_INT(a.time[3], 64);
    CHECK_INT(a.time[4], -1);
    CHECK_INT(a.time[5], -2);
    CHECK_INT(a.time[6], 127);
    CHECK_INT(a.time[7], -128);
    CHECK_INT(a.time[8], 0);
    CHECK_INT(a.wave_l[0], 127);
    CHECK_INT(a.wave_l[1], 0);
    CHECK_INT(a.wave_l[2], 31);  /* (-1 + 64) / 2 */
    CHECK_INT(a.wave_l[3], 62);  /* (127 - 2) / 2 */
    CHECK_INT(a.wave_l[4], -64); /* (0 - 128) / 2 */
    CHECK_INT(a.wave_r[3], a.wave_l[3]);
}

/* A tone on a bin peaks there; the constant term weighs nothing. */
static void test_spectrum(void)
{
    sk_milk_audio a;
    sk_milk_audio_init(&a);
    float s[SK_MILK_SAMPLES];
    bin_tone(s, 40, 0.8);
    sk_milk_audio_update(&a, s, 60, 1);
    int top = 0;
    for (int k = 1; k < SK_MILK_WAVE; k++)
        if (a.spectrum[k] > a.spectrum[top])
            top = k;
    CHECK_INT(top, 40);
    for (int i = 0; i < SK_MILK_SAMPLES; i++)
        s[i] = 0.5f;
    sk_milk_audio_update(&a, s, 60, 2);
    CHECK(a.spectrum[0] == 0);
}

/* The bands: bins 0-5 bass, 6-63 mid, 64-254 treble. A tone at the edge
 * of a band lights that band's sum, not its neighbour's. */
static void test_bands(void)
{
    /* Each tone against the same a bin across the edge, for what
     * leaks; bin 255 is in no band. */
    const struct {
        int bin, band, other;
    } cases[] = {{5, 0, 6}, {6, 1, 5}, {63, 1, 64}, {64, 2, 63}, {254, 2, 255}};
    for (size_t c = 0; c < SK_LEN(cases); c++) {
        sk_milk_audio a, quiet;
        sk_milk_audio_init(&a);
        sk_milk_audio_init(&quiet);
        float s[SK_MILK_SAMPLES];
        bin_tone(s, cases[c].bin, 0.9);
        sk_milk_audio_update(&a, s, 60, 1);
        bin_tone(s, cases[c].other, 0.9);
        sk_milk_audio_update(&quiet, s, 60, 1);
        int b = cases[c].band;
        if (!(a.imm[b] > 2 * quiet.imm[b]))
            sk_test_fail(__FILE__, __LINE__, "bin %d: band %d %g against %g at bin %d", cases[c].bin, b, a.imm[b],
                         quiet.imm[b], cases[c].other);
    }
}

/* A steady sound settles at 1 in every level; a change of sound shows
 * at once in the level and more slowly in the attenuated one. */
static void test_levels(void)
{
    sk_milk_audio a;
    sk_milk_audio_init(&a);
    float s[SK_MILK_SAMPLES];
    for (int i = 0; i < SK_MILK_SAMPLES; i++)
        s[i] = (float)(0.2 * sin(2 * M_PI * 3 * i / SK_MILK_SAMPLES) + 0.2 * sin(2 * M_PI * 30 * i / SK_MILK_SAMPLES) +
                       0.15 * sin(2 * M_PI * 150 * i / SK_MILK_SAMPLES));
    for (int n = 1; n <= 600; n++)
        sk_milk_audio_update(&a, s, 60, n);
    CHECK(fabs(a.bass - 1) < 0.02 && fabs(a.mid - 1) < 0.02 && fabs(a.treb - 1) < 0.02);
    CHECK(fabs(a.bass_att - 1) < 0.02 && fabs(a.mid_att - 1) < 0.02 && fabs(a.treb_att - 1) < 0.02);
    /* Twice as loud in the bass alone. */
    for (int i = 0; i < SK_MILK_SAMPLES; i++)
        s[i] += (float)(0.2 * sin(2 * M_PI * 3 * i / SK_MILK_SAMPLES));
    sk_milk_audio_update(&a, s, 60, 601);
    CHECK(a.bass > 1.9 && a.bass < 2.1);
    CHECK(a.bass_att > 1.1 && a.bass_att < a.bass);
    CHECK(fabs(a.mid - 1) < 0.05);
}

/* The averages move by a rate set for 30 fps: at 15 fps a frame moves
 * them further than at 60. */
static void test_rate(void)
{
    float s[SK_MILK_SAMPLES];
    bin_tone(s, 3, 0.5);
    sk_milk_audio slow, fast;
    sk_milk_audio_init(&slow);
    sk_milk_audio_init(&fast);
    sk_milk_audio_update(&slow, s, 15, 60);
    sk_milk_audio_update(&fast, s, 60, 60);
    CHECK(fabs(slow.avg[0] - 1) > fabs(fast.avg[0] - 1));
    CHECK(fabs(slow.long_avg[0] - 1) > fabs(fast.long_avg[0] - 1));
    /* Out of 15..144, or not a number, the rate holds at the limit. */
    sk_milk_audio_init(&fast);
    sk_milk_audio_update(&fast, s, NAN, 60);
    CHECK(slow.avg[0] == fast.avg[0]);
}

/* Silence: every level 0 until the long average falls under 0.001,
 * then 1; the attenuated levels fall from 1. */
static void test_silence(void)
{
    sk_milk_audio a;
    sk_milk_audio_init(&a);
    float s[SK_MILK_SAMPLES] = {0};
    sk_milk_audio_update(&a, s, 60, 1);
    CHECK(a.bass == 0 && a.mid == 0 && a.treb == 0);
    CHECK(a.bass_att > 0.5 && a.bass_att < 1);
    for (int n = 2; n <= 3000; n++)
        sk_milk_audio_update(&a, s, 60, n);
    CHECK(a.bass == 1 && a.mid == 1 && a.treb == 1 && a.bass_att == 1);
}

/* The test signal: the same samples for the same frame, all in -1..1,
 * loud enough to see. */
static void test_signal(void)
{
    float a[SK_MILK_SAMPLES], b[SK_MILK_SAMPLES];
    double peak = 0;
    for (int n = 0; n < 120; n++) {
        sk_milk_demo_signal(n, a);
        sk_milk_demo_signal(n, b);
        CHECK(!memcmp(a, b, sizeof a));
        for (int i = 0; i < SK_MILK_SAMPLES; i++) {
            CHECK(a[i] >= -1 && a[i] <= 1);
            peak = fmax(peak, fabs(a[i]));
        }
    }
    CHECK(peak > 0.4);
}

const sk_test sk_suite_milk_audio[] = {
    T(test_bytes),
    T(test_spectrum),
    T(test_bands),
    T(test_levels),
    T(test_rate),
    T(test_silence),
    T(test_signal),
    {0},
};
