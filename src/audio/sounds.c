#include "sounds.h"

#include <limits.h>
#include <math.h>
#include <string.h>
#include <time.h>

/* The longest wait decision 0024 allows, in seconds. */
#define MAX_WAIT 3600.0f

typedef struct {
    sk_pcm *pcm;
    bool loop;
    float volume;    /* half the manifest's */
    uint64_t lo, hi; /* random: the wait, in ms */
    sk_voice voice;
    bool waiting;
    uint64_t due; /* waiting: when it plays; playing: when it should end */
    unsigned plays;
} entry;

struct sk_sounds {
    sk_mixer *m;
    sk_clock_fn clock;
    void *clock_ud;
    uint64_t rng;
    bool muted;
    entry e[SK_SOUNDS_MAX];
    size_t n;
};

static uint64_t monotonic_ms(void *ud)
{
    (void)ud;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

/* splitmix64. */
static uint64_t next_random(sk_sounds *s)
{
    uint64_t z = (s->rng += 0x9e3779b97f4a7c15u);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9u;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebu;
    return z ^ (z >> 31);
}

/* A wait drawn evenly from the interval, to the millisecond. */
static uint64_t draw(sk_sounds *s, const entry *e)
{
    if (e->hi <= e->lo)
        return e->lo;
    return e->lo + next_random(s) % (e->hi - e->lo + 1);
}

/* Seconds as whole milliseconds, within decision 0024's range. */
static uint64_t wait_ms(float seconds)
{
    if (!(seconds > 0.0f))
        return 0;
    return (uint64_t)lroundf(SK_MIN(seconds, MAX_WAIT) * 1000.0f);
}

sk_sounds *sk_sounds_new(sk_mixer *m, sk_clock_fn clock, void *clock_ud, uint64_t seed)
{
    sk_sounds *s = sk_xcalloc(1, sizeof *s);
    s->m = m;
    s->clock = clock ? clock : monotonic_ms;
    s->clock_ud = clock_ud;
    s->rng = seed;
    s->muted = true;
    return s;
}

static void stop(sk_sounds *s)
{
    for (size_t i = 0; i < s->n; i++) {
        entry *e = &s->e[i];
        if (e->voice)
            sk_mixer_stop(s->m, e->voice);
        e->voice = 0;
        e->waiting = false;
    }
}

static void play(sk_sounds *s, entry *e, uint64_t now)
{
    e->voice = sk_mixer_play(s->m, e->pcm, e->volume, e->loop);
    if (e->voice)
        e->plays++;
    e->due = now + sk_pcm_ms(e->pcm);
}

/* Every sound from the start, as a new skin or an unmute has them, its
 * plays counted afresh. */
static long start(sk_sounds *s)
{
    stop(s);
    for (size_t i = 0; i < s->n; i++)
        s->e[i].plays = 0;
    if (s->muted)
        return -1;
    uint64_t now = s->clock(s->clock_ud);
    for (size_t i = 0; i < s->n; i++) {
        entry *e = &s->e[i];
        if (!e->pcm)
            continue;
        if (e->loop) {
            play(s, e, now);
        } else {
            e->waiting = true;
            e->due = now + draw(s, e);
        }
    }
    return sk_sounds_tick(s);
}

void sk_sounds_free(sk_sounds *s)
{
    if (!s)
        return;
    stop(s);
    for (size_t i = 0; i < s->n; i++)
        sk_pcm_unref(s->e[i].pcm);
    free(s);
}

long sk_sounds_set(sk_sounds *s, const sk_sounds_entry *in, size_t n)
{
    stop(s);
    /* The new sounds are held before the old are dropped, which may be
     * the same. */
    entry next[SK_SOUNDS_MAX];
    memset(next, 0, sizeof next);
    n = SK_MIN(n, (size_t)SK_SOUNDS_MAX);
    for (size_t i = 0; i < n; i++) {
        entry *e = &next[i];
        e->pcm = in[i].pcm ? sk_pcm_ref(in[i].pcm) : NULL;
        e->loop = in[i].loop;
        float v = in[i].volume;
        e->volume = SK_SOUNDS_MASTER * (v > 0.0f ? SK_MIN(v, 1.0f) : 0.0f);
        e->lo = wait_ms(in[i].interval[0]);
        e->hi = SK_MAX(e->lo, wait_ms(in[i].interval[1]));
    }
    for (size_t i = 0; i < s->n; i++)
        sk_pcm_unref(s->e[i].pcm);
    memcpy(s->e, next, sizeof next);
    s->n = n;
    return start(s);
}

long sk_sounds_mute(sk_sounds *s, bool muted)
{
    if (muted == s->muted)
        return sk_sounds_tick(s);
    s->muted = muted;
    return start(s);
}

bool sk_sounds_muted(const sk_sounds *s)
{
    return s->muted;
}

long sk_sounds_tick(sk_sounds *s)
{
    if (s->muted)
        return -1;
    uint64_t now = s->clock(s->clock_ud);
    uint64_t next = UINT64_MAX;
    for (size_t i = 0; i < s->n; i++) {
        entry *e = &s->e[i];
        if (!e->pcm)
            continue;
        if (e->loop) {
            /* Every voice was busy when it started: try again. */
            if (!e->voice)
                play(s, e, now);
            if (!e->voice)
                next = SK_MIN(next, now + SK_SOUNDS_POLL_MS);
            continue;
        }
        if (e->waiting && now >= e->due) {
            e->waiting = false;
            play(s, e, now);
        } else if (!e->waiting && !sk_mixer_playing(s->m, e->voice)) {
            e->voice = 0;
        }
        if (!e->waiting && !e->voice) {
            e->waiting = true;
            e->due = now + draw(s, e);
        }
        next = SK_MIN(next, e->due > now ? e->due : now + SK_SOUNDS_POLL_MS);
    }
    if (next == UINT64_MAX)
        return -1;
    return (long)SK_MIN(next - now, (uint64_t)LONG_MAX);
}

size_t sk_sounds_count(const sk_sounds *s)
{
    return s->n;
}

sk_sounds_state sk_sounds_get(sk_sounds *s, size_t i)
{
    const entry *e = &s->e[i];
    return (sk_sounds_state){
        .loop = e->loop,
        .playing = e->voice && sk_mixer_playing(s->m, e->voice),
        .waiting = e->waiting,
        .due = e->due,
        .plays = e->plays,
        .volume = e->volume,
        .voice = e->voice,
    };
}
