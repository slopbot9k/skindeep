#include "mixer.h"

#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>

struct sk_pcm {
    atomic_int refs;
    uint32_t rate;
    size_t n;
    int16_t *frames;
};

sk_pcm *sk_pcm_adopt(int16_t *frames, size_t n, uint32_t rate)
{
    if (!frames || n == 0 || n > (size_t)1 << 31 || rate < SK_PCM_MIN_RATE || rate > SK_PCM_MAX_RATE) {
        free(frames);
        return NULL;
    }
    sk_pcm *p = sk_xmalloc(sizeof *p);
    atomic_init(&p->refs, 1);
    p->rate = rate;
    p->n = n;
    p->frames = frames;
    return p;
}

sk_pcm *sk_pcm_new(const int16_t *frames, size_t n, uint32_t rate)
{
    if (!frames || n == 0 || n > (size_t)1 << 31)
        return NULL;
    int16_t *copy = sk_xreallocarray(NULL, n, sizeof *copy);
    memcpy(copy, frames, n * sizeof *copy);
    return sk_pcm_adopt(copy, n, rate);
}

sk_pcm *sk_pcm_ref(sk_pcm *p)
{
    atomic_fetch_add_explicit(&p->refs, 1, memory_order_relaxed);
    return p;
}

void sk_pcm_unref(sk_pcm *p)
{
    if (p && atomic_fetch_sub_explicit(&p->refs, 1, memory_order_acq_rel) == 1) {
        free(p->frames);
        free(p);
    }
}

size_t sk_pcm_frames(const sk_pcm *p)
{
    return p->n;
}

uint32_t sk_pcm_rate(const sk_pcm *p)
{
    return p->rate;
}

uint64_t sk_pcm_ms(const sk_pcm *p)
{
    return ((uint64_t)p->n * 1000u + p->rate - 1) / p->rate;
}

enum { SLOT_FREE, SLOT_PLAYING, SLOT_DONE };

/* Frames mixed at a time, into a buffer on the stack. */
#define CHUNK 256
/* The soft clipper's knee, as a fraction of full scale. */
#define KNEE 0.75f

typedef struct {
    atomic_int state;
    atomic_bool stop;
    /* Filled by the control side while the slot is free; the output side
     * reads them while it plays. */
    sk_pcm *pcm;
    const int16_t *s;
    size_t n;
    uint64_t step; /* source frames per output frame, 32.32 */
    float gain;    /* the volume over full scale */
    bool loop;
    /* The output side's while the slot plays. */
    uint64_t pos; /* in source frames, 32.32 */
    float ramp;   /* the fade, 0 to 1 */
    /* The control side's alone. */
    sk_voice id;
} slot;

struct sk_mixer {
    uint32_t rate;
    float fade_in, fade_out; /* the fade's change per frame */
    slot slots[SK_MIXER_VOICES];
    atomic_uint live;
    atomic_flag pulling;
    pthread_mutex_t ctl;
    uint64_t serial;
    void (*wake)(void *ud);
    void *wake_ud;
};

sk_mixer *sk_mixer_new(uint32_t rate)
{
    if (rate < 8000 || rate > 192000)
        sk_die("mixer rate %u", (unsigned)rate);
    sk_mixer *m = sk_xcalloc(1, sizeof *m);
    m->rate = rate;
    m->fade_in = 500.0f / (float)rate;
    m->fade_out = 100.0f / (float)rate;
    for (size_t i = 0; i < SK_MIXER_VOICES; i++) {
        atomic_init(&m->slots[i].state, SLOT_FREE);
        atomic_init(&m->slots[i].stop, false);
    }
    atomic_init(&m->live, 0);
    atomic_flag_clear(&m->pulling);
    pthread_mutex_init(&m->ctl, NULL);
    return m;
}

void sk_mixer_free(sk_mixer *m)
{
    if (!m)
        return;
    for (size_t i = 0; i < SK_MIXER_VOICES; i++)
        if (atomic_load(&m->slots[i].state) != SLOT_FREE)
            sk_pcm_unref(m->slots[i].pcm);
    pthread_mutex_destroy(&m->ctl);
    free(m);
}

uint32_t sk_mixer_rate(const sk_mixer *m)
{
    return m->rate;
}

/* With ctl held: the slots the output side is done with become free,
 * dropping their sounds. */
static void reclaim(sk_mixer *m)
{
    for (size_t i = 0; i < SK_MIXER_VOICES; i++) {
        slot *v = &m->slots[i];
        if (atomic_load_explicit(&v->state, memory_order_acquire) == SLOT_DONE) {
            sk_pcm_unref(v->pcm);
            v->pcm = NULL;
            v->id = 0;
            atomic_store_explicit(&v->state, SLOT_FREE, memory_order_relaxed);
        }
    }
}

/* With ctl held: the slot playing v, or NULL. */
static slot *find(sk_mixer *m, sk_voice v)
{
    size_t i = (size_t)(v & 0xff);
    if (i == 0 || i > SK_MIXER_VOICES)
        return NULL;
    slot *s = &m->slots[i - 1];
    if (s->id != v || atomic_load_explicit(&s->state, memory_order_acquire) != SLOT_PLAYING)
        return NULL;
    return s;
}

sk_voice sk_mixer_play(sk_mixer *m, sk_pcm *p, float volume, bool loop)
{
    if (!p)
        return 0;
    if (!(volume > 0.0f))
        volume = 0.0f;
    if (volume > 1.0f)
        volume = 1.0f;
    pthread_mutex_lock(&m->ctl);
    reclaim(m);
    sk_voice id = 0;
    for (size_t i = 0; i < SK_MIXER_VOICES; i++) {
        slot *v = &m->slots[i];
        if (atomic_load_explicit(&v->state, memory_order_relaxed) != SLOT_FREE)
            continue;
        v->pcm = sk_pcm_ref(p);
        v->s = p->frames;
        v->n = p->n;
        v->step = ((uint64_t)p->rate << 32) / m->rate;
        v->gain = volume / 32768.0f;
        v->loop = loop;
        v->pos = 0;
        v->ramp = 0.0f;
        id = v->id = (++m->serial << 8) | (sk_voice)(i + 1);
        atomic_store_explicit(&v->stop, false, memory_order_relaxed);
        /* Counted before the output can see it, so the count never dips
         * below the voices it holds. */
        atomic_fetch_add_explicit(&m->live, 1, memory_order_relaxed);
        atomic_store_explicit(&v->state, SLOT_PLAYING, memory_order_release);
        break;
    }
    if (id && m->wake)
        m->wake(m->wake_ud);
    pthread_mutex_unlock(&m->ctl);
    return id;
}

void sk_mixer_stop(sk_mixer *m, sk_voice v)
{
    pthread_mutex_lock(&m->ctl);
    slot *s = find(m, v);
    if (s)
        atomic_store_explicit(&s->stop, true, memory_order_relaxed);
    reclaim(m);
    pthread_mutex_unlock(&m->ctl);
}

void sk_mixer_stop_all(sk_mixer *m)
{
    pthread_mutex_lock(&m->ctl);
    reclaim(m);
    for (size_t i = 0; i < SK_MIXER_VOICES; i++)
        if (atomic_load_explicit(&m->slots[i].state, memory_order_relaxed) == SLOT_PLAYING)
            atomic_store_explicit(&m->slots[i].stop, true, memory_order_relaxed);
    pthread_mutex_unlock(&m->ctl);
}

bool sk_mixer_playing(sk_mixer *m, sk_voice v)
{
    pthread_mutex_lock(&m->ctl);
    slot *s = find(m, v);
    bool on = s && !atomic_load_explicit(&s->stop, memory_order_relaxed);
    pthread_mutex_unlock(&m->ctl);
    return on;
}

unsigned sk_mixer_live(const sk_mixer *m)
{
    return atomic_load_explicit(&((sk_mixer *)m)->live, memory_order_acquire);
}

void sk_mixer_on_wake(sk_mixer *m, void (*fn)(void *ud), void *ud)
{
    pthread_mutex_lock(&m->ctl);
    m->wake = fn;
    m->wake_ud = ud;
    pthread_mutex_unlock(&m->ctl);
}

/* ---- The output side ---- */

/* Frame k of the voice, which may lie past either end: a loop wraps
 * round, and a sound played once is silent outside itself. */
static float frame_at(const slot *v, int64_t k)
{
    int64_t n = (int64_t)v->n;
    if (v->loop)
        k = ((k % n) + n) % n;
    else if (k < 0 || k >= n)
        return 0.0f;
    return (float)v->s[k];
}

/* Catmull-Rom between frames k and k + 1, t of the way. */
static float interpolate(const slot *v, size_t k, float t)
{
    float p0, p1, p2, p3;
    if (k >= 1 && k + 2 < v->n) {
        p0 = v->s[k - 1];
        p1 = v->s[k];
        p2 = v->s[k + 1];
        p3 = v->s[k + 2];
    } else {
        p0 = frame_at(v, (int64_t)k - 1);
        p1 = frame_at(v, (int64_t)k);
        p2 = frame_at(v, (int64_t)k + 1);
        p3 = frame_at(v, (int64_t)k + 2);
    }
    return p1 + 0.5f * t * (p2 - p0 + t * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + t * (3.0f * (p1 - p2) + p3 - p0)));
}

/* Hands the slot back. Nothing of it may be touched after. */
static void finish(sk_mixer *m, slot *v)
{
    atomic_store_explicit(&v->state, SLOT_DONE, memory_order_release);
    atomic_fetch_sub_explicit(&m->live, 1, memory_order_release);
}

/* Adds n frames of the voice to acc. */
static void mix_voice(sk_mixer *m, slot *v, float *acc, size_t n)
{
    bool stopping = atomic_load_explicit(&v->stop, memory_order_relaxed);
    uint64_t len = (uint64_t)v->n << 32;
    for (size_t i = 0; i < n; i++) {
        if (v->pos >= len) {
            if (!v->loop) {
                finish(m, v);
                return;
            }
            v->pos %= len;
        }
        if (stopping) {
            v->ramp -= m->fade_out;
            if (v->ramp <= 0.0f) {
                finish(m, v);
                return;
            }
        } else if (v->ramp < 1.0f) {
            v->ramp = SK_MIN(v->ramp + m->fade_in, 1.0f);
        }
        float t = (float)(uint32_t)v->pos * (1.0f / 4294967296.0f);
        acc[i] += interpolate(v, (size_t)(v->pos >> 32), t) * v->gain * v->ramp;
        v->pos += v->step;
    }
}

/* Linear below the knee; above it, a tanh that meets the line with its
 * slope and never reaches full scale. */
static float soft_clip(float x)
{
    float a = fabsf(x);
    if (a <= KNEE)
        return x;
    float y = KNEE + (1.0f - KNEE) * tanhf((a - KNEE) / (1.0f - KNEE));
    return x < 0.0f ? -y : y;
}

bool sk_mixer_pull(sk_mixer *m, int16_t *out, size_t frames)
{
    if (atomic_flag_test_and_set_explicit(&m->pulling, memory_order_acquire)) {
        memset(out, 0, frames * SK_MIXER_CHANNELS * sizeof *out);
        return false;
    }
    bool sounded = false;
    float acc[CHUNK];
    while (frames) {
        size_t n = SK_MIN(frames, (size_t)CHUNK);
        memset(acc, 0, sizeof acc);
        for (size_t i = 0; i < SK_MIXER_VOICES; i++) {
            slot *v = &m->slots[i];
            if (atomic_load_explicit(&v->state, memory_order_acquire) != SLOT_PLAYING)
                continue;
            sounded = true;
            mix_voice(m, v, acc, n);
        }
        for (size_t i = 0; i < n; i++) {
            int16_t s = (int16_t)lrintf(soft_clip(acc[i]) * 32767.0f);
            out[2 * i] = s;
            out[2 * i + 1] = s;
        }
        out += 2 * n;
        frames -= n;
    }
    atomic_flag_clear_explicit(&m->pulling, memory_order_release);
    return sounded;
}
