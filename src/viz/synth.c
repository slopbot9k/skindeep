/* The visualizer's signal (synth.h), line for line app/viz/synth.js,
 * with the generator of vmath.h for its noise. */
#include "synth.h"

#include <math.h>
#include <pthread.h>
#include <string.h>

enum { VOICE_THUMP, VOICE_TOOL, VOICE_CHIRP, VOICE_WOBBLE };

/* A tool call's pitch by its class: read, edit, shell, web, agent,
 * other. */
static const double TOOL_HZ[] = {520, 330, 660, 880, 1320, 990};

/* The band analysis: each band's frequency, and per band the Hann
 * window times the cosine and the sine at every even sample, which
 * synth.js multiplies in each frame. */
static double band_hz[SK_SYNTH_BANDS];
static float hann[SK_SYNTH_N];
static double (*twiddle)[SK_SYNTH_N / 2][2];
static pthread_once_t tables_once = PTHREAD_ONCE_INIT;

static void make_tables(void)
{
    for (int b = 0; b < SK_SYNTH_BANDS; b++)
        band_hz[b] = 40 * sk_vm_pow(12000.0 / 40, (double)b / (SK_SYNTH_BANDS - 1));
    for (int n = 0; n < SK_SYNTH_N; n++)
        hann[n] = (float)(0.5 - 0.5 * sk_vm_cos(SK_TAU * n / (SK_SYNTH_N - 1)));
    twiddle = sk_xmalloc(SK_SYNTH_BANDS * sizeof *twiddle);
    for (int b = 0; b < SK_SYNTH_BANDS; b++) {
        double w = SK_TAU * band_hz[b] / SK_SYNTH_RATE;
        for (int n = 0; n < SK_SYNTH_N; n += 2) {
            double s, c;
            sk_vm_sincos(w * n, &s, &c);
            twiddle[b][n / 2][0] = (double)hann[n] * c;
            twiddle[b][n / 2][1] = (double)hann[n] * s;
        }
    }
}

double sk_synth_band_hz(int b)
{
    pthread_once(&tables_once, make_tables);
    return band_hz[b];
}

void sk_synth_init(sk_synth *s, uint64_t seed)
{
    pthread_once(&tables_once, make_tables);
    memset(s, 0, sizeof *s);
    s->armed = true;
    sk_rng_seed(&s->rng, seed);
}

void sk_synth_free(sk_synth *s)
{
    sk_vec_free(&s->flashes);
    sk_vec_free(&s->touches);
    sk_vec_free(&s->voices);
}

void sk_synth_input(sk_synth *s, const sk_viz_pane_frame *f)
{
    double out_bps = f->n[SK_VIZ_OUT_BPS], in_kps = f->n[SK_VIZ_IN_KPS], idle = f->n[SK_VIZ_IDLE_S];
    s->target.bass = SK_MIN(1, out_bps / 2500) + SK_MIN(0.3, in_kps / 30);
    s->target.treble = SK_MIN(1, f->n[SK_VIZ_OUT_RATE] / 120);
    s->target.busy = f->n[SK_VIZ_BUSY] != 0 ? 1 : 0;
    s->target.idle = idle > 3 ? SK_MIN(1, (idle - 3) / 10) : 0;
    for (size_t i = 0; i + 1 < f->n_events; i += 2)
        sk_synth_trigger(s, f->events[i], f->events[i + 1]);
}

static void voice(sk_synth *s, int type, double hz, double from, double to, double amp, double life)
{
    sk_synth_voice v = {type, s->t, hz, from, to, amp, life};
    sk_vec_push(&s->voices, v);
}

void sk_synth_trigger(sk_synth *s, int kind, int cls)
{
    switch (kind) {
    case SK_EV_OUTPUT:
        voice(s, VOICE_THUMP, 70, 0, 0, 0.5, 0.25);
        break;
    case SK_EV_TOOL:
        voice(s, VOICE_TOOL, cls >= 0 && cls < (int)SK_LEN(TOOL_HZ) ? TOOL_HZ[cls] : 520, 0, 0, 0.6, 0.45);
        break;
    case SK_EV_PROMPT:
        voice(s, VOICE_CHIRP, 0, 1800, 6500, 0.45, 0.7);
        break;
    case SK_EV_STOP:
        voice(s, VOICE_CHIRP, 0, 6000, 1200, 0.4, 0.7);
        break;
    case SK_EV_AGENT:
        voice(s, VOICE_WOBBLE, 220, 0, 0, 0.5, 1.4);
        break;
    case SK_EV_COMPACT:
        voice(s, VOICE_CHIRP, 0, 8000, 90, 0.7, 1.6);
        break;
    default:
        break;
    }
    sk_synth_flash fl = {kind, cls, s->t};
    sk_vec_push(&s->flashes, fl);
    s->beats++;
    if (s->voices.len > 64) {
        size_t drop = s->voices.len - 64;
        memmove(s->voices.ptr, s->voices.ptr + drop, 64 * sizeof *s->voices.ptr);
        s->voices.len = 64;
    }
}

void sk_synth_touch(sk_synth *s, double u, double v)
{
    double hz = 40 * sk_vm_pow(12000.0 / 40, sk_vm_clamp(u, 0, 1));
    sk_synth_tone t = {s->t, hz, 0.35 + 0.45 * (1 - sk_vm_clamp(v, 0, 1)), 0.5};
    sk_vec_push(&s->touches, t);
    if (s->touches.len > 16) {
        size_t drop = s->touches.len - 16;
        memmove(s->touches.ptr, s->touches.ptr + drop, 16 * sizeof *s->touches.ptr);
        s->touches.len = 16;
    }
}

/* Levels from the sound output alone: its loudness for the bass, its
 * sample-to-sample change for the treble. */
static void hear(sk_synth *s, const float *a)
{
    double sq = 0, d = 0;
    for (int n = 0; n < SK_SYNTH_N; n++) {
        sq += (double)a[n] * a[n];
        if (n)
            d += fabs((double)a[n] - a[n - 1]);
    }
    s->target.bass = SK_MIN(1, 4 * sqrt(sq / SK_SYNTH_N));
    s->target.treble = SK_MIN(1, 12 * d / SK_SYNTH_N);
    s->target.busy = 0;
    s->target.idle = 0;
}

/* The elements of a vector that meet cond, kept in order: JavaScript's
 * filter in place. */
#define KEEP(vec, cond)                                                      \
    do {                                                                     \
        size_t k_ = 0;                                                       \
        for (size_t i_ = 0; i_ < (vec)->len; i_++)                           \
            if (cond((vec)->ptr[i_]))                                        \
                (vec)->ptr[k_++] = (vec)->ptr[i_];                           \
        (vec)->len = k_;                                                     \
    } while (0)

void sk_synth_step(sk_synth *s, double dt, int src, const float *sound, size_t n_sound)
{
    const float *a = src > 0 && sound && n_sound == SK_SYNTH_N ? sound : NULL;
    bool only = a && src == SK_VIZ_SRC_SOUND;
    if (only)
        hear(s, a);
    s->t += dt;
    double k = 1 - sk_vm_exp(-dt * 6);
    sk_synth_levels *L = &s->level;
    L->bass += (s->target.bass - L->bass) * k;
    L->treble += (s->target.treble - L->treble) * k;
    L->busy += (s->target.busy - L->busy) * k;
    L->idle += (s->target.idle - L->idle) * k;
    double t0 = s->t;
    if (only) {
        if (s->armed && L->bass > s->floor + 0.15) {
            s->rises++;
            s->armed = false;
        } else if (L->bass < s->floor + 0.05) {
            s->armed = true;
        }
        s->floor += (L->bass - s->floor) * (1 - sk_vm_exp(-dt * 1.5));
    }
#define ALIVE(v) (t0 - (v).t < (v).life)
#define FRESH(f) (t0 - (f).t < 1.5)
    KEEP(&s->voices, ALIVE);
    KEEP(&s->flashes, FRESH);
    KEEP(&s->touches, ALIVE);
#undef ALIVE
#undef FRESH
    double bass_hz = 55 + 50 * SK_MIN(1, L->bass);
    double bass_amp = 0.06 + 0.8 * SK_MIN(1, L->bass) + 0.1 * L->busy;
    for (int n = 0; n < SK_SYNTH_N; n++) {
        double t = t0 + (double)n / SK_SYNTH_RATE, y = 0;
        for (size_t i = 0; i < s->touches.len; i++) {
            const sk_synth_tone *v = &s->touches.ptr[i];
            double age = t - v->t;
            if (age >= 0 && age <= v->life)
                y += v->amp * sk_vm_exp(-age / (v->life / 3)) * sk_vm_sin(SK_TAU * v->hz * age);
        }
        if (only) {
            s->samples[n] = (float)sk_vm_tanh(1.5 * a[n] + y);
            continue;
        }
        double x = y + (a ? 1.5 * a[n] : 0) + bass_amp * sk_vm_sin(SK_TAU * bass_hz * t);
        /* 0 times the drone is 0: its sines are skipped at work. */
        if (L->idle != 0)
            x += 0.07 * L->idle * sk_vm_sin(SK_TAU * 38 * t) * (0.6 + 0.4 * sk_vm_sin(SK_TAU * 0.3 * t));
        x += 0.22 * L->treble * (sk_rng_next(&s->rng) * 2 - 1) * (n % 2 ? 1 : -1);
        for (size_t i = 0; i < s->voices.len; i++) {
            const sk_synth_voice *v = &s->voices.ptr[i];
            double age = t - v->t;
            if (age < 0 || age > v->life)
                continue;
            double env = v->amp * sk_vm_exp(-age / (v->life / 3));
            if (v->type == VOICE_THUMP) {
                x += env * sk_vm_sin(SK_TAU * v->hz * age);
            } else if (v->type == VOICE_TOOL) {
                x += env * (0.7 * sk_vm_sin(SK_TAU * v->hz * t) + 0.3 * (sk_rng_next(&s->rng) * 2 - 1));
            } else if (v->type == VOICE_WOBBLE) {
                x += env * sk_vm_sin(SK_TAU * v->hz * t + 3 * sk_vm_sin(SK_TAU * 6 * age));
            } else {
                double f = v->from + (v->to - v->from) * SK_MIN(1, age / v->life);
                x += env * sk_vm_sin(SK_TAU * f * age);
            }
        }
        s->samples[n] = (float)sk_vm_tanh(x);
    }
    for (int b = 0; b < SK_SYNTH_BANDS; b++) {
        double re = 0, im = 0;
        for (int n = 0; n < SK_SYNTH_N; n += 2) {
            double v = s->samples[n];
            re += v * twiddle[b][n / 2][0];
            im -= v * twiddle[b][n / 2][1];
        }
        s->bands[b] = sqrt(re * re + im * im) / (SK_SYNTH_N / 4);
    }
}

size_t sk_synth_since(const sk_synth *s, sk_synth_mark *mark, sk_viz_events *out)
{
    out->len = 0;
    if (!mark->set) {
        mark->set = true;
        mark->beats = s->beats;
        mark->rises = s->rises;
    }
    size_t n = (size_t)SK_MIN((long)s->flashes.len, s->beats - mark->beats);
    for (size_t i = s->flashes.len - n; i < s->flashes.len; i++) {
        sk_viz_event e = {s->flashes.ptr[i].kind, s->flashes.ptr[i].cls};
        sk_vec_push(out, e);
    }
    for (long k = mark->rises; k < s->rises; k++) {
        sk_viz_event e = {SK_EV_OUTPUT, 0};
        sk_vec_push(out, e);
    }
    mark->beats = s->beats;
    mark->rises = s->rises;
    return out->len;
}

void sk_synth_bytes(const sk_synth *s, uint8_t out[SK_SYNTH_N])
{
    for (int n = 0; n < SK_SYNTH_N; n++)
        out[n] = (uint8_t)sk_vm_clamp(sk_vm_round(128 + 127 * (double)s->samples[n]), 0, 255);
}
