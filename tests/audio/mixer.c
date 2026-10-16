/* audio/mixer.h with no output: each test pulls the frames itself, as
 * an output's thread would. */
#include "../../src/audio/mixer.h"
#include "../test.h"

#include <math.h>

#define RATE SK_MIXER_RATE
/* The fade-in, 2 ms. */
#define FADE_IN (RATE / 500)
/* The fade-out, 10 ms. */
#define FADE_OUT (RATE / 100)

static sk_pcm *constant(int16_t v, size_t n, uint32_t rate)
{
    int16_t *f = sk_xreallocarray(NULL, n, sizeof *f);
    for (size_t i = 0; i < n; i++)
        f[i] = v;
    return sk_pcm_adopt(f, n, rate);
}

static sk_pcm *sine(double hz, double amp, size_t n, uint32_t rate)
{
    int16_t *f = sk_xreallocarray(NULL, n, sizeof *f);
    for (size_t i = 0; i < n; i++)
        f[i] = (int16_t)lrint(amp * sin(2 * M_PI * hz * (double)i / rate));
    return sk_pcm_adopt(f, n, rate);
}

/* frames of output, which the caller frees. */
static int16_t *pull(sk_mixer *m, size_t frames, bool *sounded)
{
    int16_t *out = sk_xreallocarray(NULL, frames * 2, sizeof *out);
    bool s = sk_mixer_pull(m, out, frames);
    if (sounded)
        *sounded = s;
    return out;
}

static void test_pcm_checks_its_input(void)
{
    int16_t f[4] = {1, 2, 3, 4};
    CHECK(!sk_pcm_new(f, 4, 7999));
    CHECK(!sk_pcm_new(f, 4, 48001));
    CHECK(!sk_pcm_new(f, 0, 22050));
    CHECK(!sk_pcm_new(NULL, 4, 22050));
    sk_pcm *p = sk_pcm_new(f, 4, 8000);
    CHECK(p);
    CHECK_INT(sk_pcm_frames(p), 4);
    CHECK_INT(sk_pcm_rate(p), 8000);
    CHECK_INT(sk_pcm_ms(p), 1); /* half a millisecond, rounded up */
    sk_pcm_unref(p);
    p = constant(0, 22050, 22050);
    CHECK_INT(sk_pcm_ms(p), 1000);
    sk_pcm_unref(p);
    sk_pcm_unref(NULL);
}

static void test_silence_without_voices(void)
{
    sk_mixer *m = sk_mixer_new(RATE);
    bool sounded = true;
    int16_t *out = pull(m, 1000, &sounded);
    CHECK(!sounded);
    bool zero = true;
    for (size_t i = 0; i < 2000; i++)
        zero = zero && out[i] == 0;
    CHECK(zero);
    CHECK_INT(sk_mixer_live(m), 0);
    free(out);
    sk_mixer_free(m);
}

/* At the output's own rate the samples come through as they are, on
 * both channels, and the voice ends with its last frame. */
static void test_same_rate_passes_through(void)
{
    size_t n = 3000;
    int16_t *f = sk_xreallocarray(NULL, n, sizeof *f);
    for (size_t i = 0; i < n; i++)
        f[i] = (int16_t)((int)(i * 7919 % 50000) - 25000);
    sk_pcm *p = sk_pcm_new(f, n, RATE);
    sk_mixer *m = sk_mixer_new(RATE);
    sk_voice v = sk_mixer_play(m, p, 1.0f, false);
    CHECK(v != 0);
    CHECK(sk_mixer_playing(m, v));
    CHECK_INT(sk_mixer_live(m), 1);
    int16_t *out = pull(m, n + 100, NULL);
    int worst = 0;
    bool stereo = true, tail = true;
    for (size_t i = 0; i < n + 100; i++) {
        stereo = stereo && out[2 * i] == out[2 * i + 1];
        if (i >= n)
            tail = tail && out[2 * i] == 0;
        else if (i >= FADE_IN)
            worst = SK_MAX(worst, abs(out[2 * i] - f[i]));
    }
    CHECK(stereo);
    CHECK(tail);
    CHECK(worst <= 1);
    CHECK(!sk_mixer_playing(m, v));
    CHECK_INT(sk_mixer_live(m), 0);
    free(out);
    free(f);
    sk_mixer_free(m);
    sk_pcm_unref(p);
}

/* A sine at 22050 Hz, resampled to 48000, against the sine itself:
 * within 0.1% of its amplitude at 1 kHz and 0.025% at 440 Hz. */
static void check_sine(double hz, uint32_t rate, double max_err, double rms_err)
{
    const double amp = 16384.0;
    sk_pcm *p = sine(hz, amp, rate, rate); /* one second */
    sk_mixer *m = sk_mixer_new(RATE);
    sk_mixer_play(m, p, 1.0f, false);
    int16_t *out = pull(m, RATE, NULL);
    double worst = 0, sum = 0;
    size_t count = 0;
    for (size_t j = FADE_IN; j < RATE - 100; j++) {
        double want = amp * sin(2 * M_PI * hz * (double)j / RATE);
        double e = fabs(out[2 * j] - want);
        worst = fmax(worst, e);
        sum += e * e;
        count++;
    }
    double rms = sqrt(sum / (double)count);
    if (worst > max_err || rms > rms_err)
        sk_test_fail(__FILE__, __LINE__, "%.0f Hz from %u: max error %.2f, rms %.2f", hz, (unsigned)rate,
                     worst, rms);
    /* One second at either rate. */
    free(out);
    out = pull(m, 2, NULL);
    CHECK_INT(sk_mixer_live(m), 0);
    free(out);
    sk_mixer_free(m);
    sk_pcm_unref(p);
}

static void test_resampled_sine_matches(void)
{
    check_sine(1000, 22050, 16.4, 6.0);
    check_sine(440, 22050, 4.1, 1.5);
    check_sine(1000, 44100, 4.1, 1.5);
}

/* 22050 frames last 48000 output frames, give or take the last. */
static void test_resampled_length(void)
{
    sk_pcm *p = constant(1000, 22050, 22050);
    sk_mixer *m = sk_mixer_new(RATE);
    sk_mixer_play(m, p, 1.0f, false);
    free(pull(m, RATE - 1, NULL));
    CHECK_INT(sk_mixer_live(m), 1);
    free(pull(m, 3, NULL));
    CHECK_INT(sk_mixer_live(m), 0);
    sk_mixer_free(m);
    sk_pcm_unref(p);
}

static void test_volume_scales(void)
{
    sk_pcm *p = constant(20000, 4800, RATE);
    sk_mixer *m = sk_mixer_new(RATE);
    sk_mixer_play(m, p, 0.25f, false);
    int16_t *out = pull(m, 1000, NULL);
    CHECK(abs(out[2 * 500] - 5000) <= 1);
    /* The fade-in rises from nothing. */
    CHECK(out[0] > 0 && out[0] < 200);
    CHECK(out[2 * (FADE_IN / 2)] > 2000 && out[2 * (FADE_IN / 2)] < 3000);
    free(out);
    sk_mixer_free(m);
    sk_pcm_unref(p);
}

static void test_voices_sum(void)
{
    sk_pcm *a = constant(8000, 4800, RATE), *b = constant(-3000, 4800, 22050), *c = constant(10000, 4800, 8000);
    sk_mixer *m = sk_mixer_new(RATE);
    sk_mixer_play(m, a, 1.0f, false);
    sk_mixer_play(m, b, 1.0f, false);
    int16_t *out = pull(m, 1000, NULL);
    CHECK(abs(out[2 * 500] - 5000) <= 1);
    free(out);
    sk_mixer_play(m, c, 1.0f, false);
    out = pull(m, 1000, NULL);
    CHECK(abs(out[2 * 500] - 15000) <= 1);
    free(out);
    sk_mixer_free(m);
    sk_pcm_unref(a);
    sk_pcm_unref(b);
    sk_pcm_unref(c);
}

/* Every voice at full scale sums to 32 times full scale: the output
 * stays just under it, with its sign. */
static void test_many_voices_never_wrap(void)
{
    sk_pcm *hi = constant(32767, 4800, RATE), *lo = constant(-32768, 4800, 22050);
    sk_mixer *m = sk_mixer_new(RATE);
    for (size_t i = 0; i < SK_MIXER_VOICES; i++)
        CHECK(sk_mixer_play(m, hi, 1.0f, true) != 0);
    int16_t *out = pull(m, 2000, NULL);
    for (size_t i = 0; i < 4000; i++)
        if (out[i] < 0 || (i >= 2 * 200 && out[i] < 32000)) {
            sk_test_fail(__FILE__, __LINE__, "frame %zu: %d", i / 2, out[i]);
            break;
        }
    free(out);
    sk_mixer_stop_all(m);
    free(pull(m, 1000, NULL));
    CHECK_INT(sk_mixer_live(m), 0);
    for (size_t i = 0; i < SK_MIXER_VOICES; i++)
        sk_mixer_play(m, lo, 1.0f, true);
    out = pull(m, 2000, NULL);
    for (size_t i = 0; i < 4000; i++)
        if (out[i] > 0 || out[i] < -32767 || (i >= 2 * 200 && out[i] > -32000)) {
            sk_test_fail(__FILE__, __LINE__, "frame %zu: %d", i / 2, out[i]);
            break;
        }
    free(out);
    sk_mixer_free(m);
    sk_pcm_unref(hi);
    sk_pcm_unref(lo);
}

/* A sum rising from half to one and a half times full scale: the output
 * follows it to the knee, then bends, rising all the way, never over. */
static void test_soft_clip_bends(void)
{
    size_t n = 30000;
    int16_t *f = sk_xreallocarray(NULL, n, sizeof *f);
    for (size_t i = 0; i < n; i++)
        f[i] = (int16_t)(i * 32767 / n);
    sk_pcm *ramp = sk_pcm_adopt(f, n, RATE), *half = constant(16384, n, RATE);
    sk_mixer *m = sk_mixer_new(RATE);
    sk_mixer_play(m, ramp, 1.0f, false);
    sk_mixer_play(m, half, 1.0f, false);
    int16_t *out = pull(m, n, NULL);
    bool rising = true, linear = true;
    int top = 0;
    for (size_t i = FADE_IN + 1; i < n; i++) {
        rising = rising && out[2 * i] >= out[2 * (i - 1)];
        double in = (16384.0 + (double)f[i]) / 32768.0;
        if (in < 0.74)
            linear = linear && fabs(out[2 * i] - in * 32767.0) <= 2.0;
        top = SK_MAX(top, out[2 * i]);
    }
    CHECK(rising);
    CHECK(linear);
    CHECK(top < 32767 && top > 32000);
    free(out);
    sk_mixer_free(m);
    sk_pcm_unref(ramp);
    sk_pcm_unref(half);
}

static void test_loop_wraps(void)
{
    int16_t f[100];
    for (int i = 0; i < 100; i++)
        f[i] = (int16_t)(i * 100);
    sk_pcm *p = sk_pcm_new(f, 100, RATE);
    sk_mixer *m = sk_mixer_new(RATE);
    sk_voice v = sk_mixer_play(m, p, 1.0f, true);
    int16_t *out = pull(m, 1000, NULL);
    int worst = 0;
    for (size_t i = FADE_IN; i < 1000; i++)
        worst = SK_MAX(worst, abs(out[2 * i] - f[i % 100]));
    CHECK(worst <= 1);
    CHECK(sk_mixer_playing(m, v));
    CHECK_INT(sk_mixer_live(m), 1);
    free(out);
    /* A loop shorter than a step of the resampler wraps too. */
    sk_pcm *one = constant(1000, 1, 8000);
    sk_mixer_stop(m, v);
    sk_mixer_play(m, one, 1.0f, true);
    out = pull(m, 1000, NULL);
    CHECK(abs(out[2 * 999] - 1000) <= 1);
    free(out);
    sk_mixer_free(m);
    sk_pcm_unref(p);
    sk_pcm_unref(one);
}

static void test_stop_fades_out(void)
{
    sk_pcm *p = constant(16000, 100, RATE);
    sk_mixer *m = sk_mixer_new(RATE);
    sk_voice v = sk_mixer_play(m, p, 1.0f, true);
    free(pull(m, 500, NULL));
    sk_mixer_stop(m, v);
    CHECK(!sk_mixer_playing(m, v));
    CHECK_INT(sk_mixer_live(m), 1); /* until the fade is out */
    int16_t *out = pull(m, FADE_OUT + 10, NULL);
    bool falling = true;
    for (size_t i = 1; i < FADE_OUT + 10; i++)
        falling = falling && out[2 * i] <= out[2 * (i - 1)];
    CHECK(falling);
    CHECK(out[0] > 15000);
    CHECK(out[2 * (FADE_OUT / 2)] > 7000 && out[2 * (FADE_OUT / 2)] < 9000);
    CHECK_INT(out[2 * (FADE_OUT + 9)], 0);
    CHECK_INT(sk_mixer_live(m), 0);
    /* Stopping it again, or a voice never played, does nothing. */
    sk_mixer_stop(m, v);
    sk_mixer_stop(m, 0);
    sk_mixer_stop(m, 12345);
    CHECK(!sk_mixer_playing(m, 0));
    free(out);
    sk_mixer_free(m);
    sk_pcm_unref(p);
}

static void test_slots_run_out_and_come_back(void)
{
    sk_pcm *p = constant(100, 480, RATE);
    sk_mixer *m = sk_mixer_new(RATE);
    sk_voice v[SK_MIXER_VOICES];
    for (size_t i = 0; i < SK_MIXER_VOICES; i++) {
        v[i] = sk_mixer_play(m, p, 0.5f, true);
        CHECK(v[i] != 0);
    }
    CHECK_INT(sk_mixer_play(m, p, 0.5f, true), 0);
    sk_mixer_stop(m, v[3]);
    /* The slot is the output's until its fade is out. */
    CHECK_INT(sk_mixer_play(m, p, 0.5f, true), 0);
    free(pull(m, 1000, NULL));
    sk_voice again = sk_mixer_play(m, p, 0.5f, true);
    CHECK(again != 0 && again != v[3]);
    /* The old voice's name does not reach the new one. */
    CHECK(!sk_mixer_playing(m, v[3]));
    CHECK(sk_mixer_playing(m, again));
    sk_mixer_stop_all(m);
    free(pull(m, 1000, NULL));
    CHECK_INT(sk_mixer_live(m), 0);
    sk_mixer_free(m);
    sk_pcm_unref(p);
}

/* A voice keeps its sound: dropping the caller's reference while it
 * plays frees nothing the output reads (AddressSanitizer would say). */
static void test_sound_outlives_its_owner(void)
{
    sk_mixer *m = sk_mixer_new(RATE);
    sk_pcm *p = constant(4000, 2000, 22050);
    sk_voice v = sk_mixer_play(m, p, 1.0f, false);
    sk_pcm_unref(p);
    int16_t *out = pull(m, 5000, NULL);
    CHECK(abs(out[2 * 1000] - 4000) <= 1);
    CHECK(!sk_mixer_playing(m, v));
    free(out);
    /* And a mixer freed with voices still playing drops their sounds. */
    p = constant(1, 10, RATE);
    sk_mixer_play(m, p, 1.0f, true);
    sk_pcm_unref(p);
    sk_mixer_free(m);
}

static int wakes;

static void count_wake(void *ud)
{
    (void)ud;
    wakes++;
}

static void test_play_wakes_the_output(void)
{
    sk_mixer *m = sk_mixer_new(RATE);
    sk_pcm *p = constant(1, 10, RATE);
    wakes = 0;
    sk_mixer_on_wake(m, count_wake, NULL);
    sk_mixer_play(m, p, 1.0f, false);
    sk_mixer_play(m, p, 1.0f, false);
    CHECK_INT(wakes, 2);
    sk_mixer_play(m, NULL, 1.0f, false);
    CHECK_INT(wakes, 2);
    sk_mixer_on_wake(m, NULL, NULL);
    sk_mixer_play(m, p, 1.0f, false);
    CHECK_INT(wakes, 2);
    sk_mixer_free(m);
    sk_pcm_unref(p);
}

const sk_test sk_suite_audio_mixer[] = {
    T(test_pcm_checks_its_input),
    T(test_silence_without_voices),
    T(test_same_rate_passes_through),
    T(test_resampled_sine_matches),
    T(test_resampled_length),
    T(test_volume_scales),
    T(test_voices_sum),
    T(test_many_voices_never_wrap),
    T(test_soft_clip_bends),
    T(test_loop_wraps),
    T(test_stop_fades_out),
    T(test_slots_run_out_and_come_back),
    T(test_sound_outlives_its_owner),
    T(test_play_wakes_the_output),
    {0},
};
