/* audio/sounds.h, decision 0024's player, on a clock the tests move by
 * hand and a mixer whose frames they pull, as an output would. */
#include "../../src/audio/sounds.h"
#include "../test.h"

#define RATE SK_MIXER_RATE

static uint64_t fake_now;

static uint64_t fake_clock(void *ud)
{
    (void)ud;
    return fake_now;
}

static sk_pcm *constant(int16_t v, size_t ms)
{
    size_t n = ms * RATE / 1000;
    int16_t *f = sk_xreallocarray(NULL, n, sizeof *f);
    for (size_t i = 0; i < n; i++)
        f[i] = v;
    return sk_pcm_adopt(f, n, RATE);
}

/* ms of output; the last frame's left sample. */
static int pull_ms(sk_mixer *m, size_t ms)
{
    size_t n = ms * RATE / 1000;
    int16_t *out = sk_xreallocarray(NULL, n * 2, sizeof *out);
    sk_mixer_pull(m, out, n);
    int last = out[2 * (n - 1)];
    free(out);
    return last;
}

typedef struct {
    sk_mixer *m;
    sk_sounds *s;
    sk_pcm *loop, *once;
} rig;

/* A player at time 0 over a loop at 20000 and a sound of 500 ms at
 * 10000, both at 48 kHz. */
static rig rig_new(uint64_t seed)
{
    fake_now = 0;
    rig r;
    r.m = sk_mixer_new(RATE);
    r.s = sk_sounds_new(r.m, fake_clock, NULL, seed);
    r.loop = constant(20000, 100);
    r.once = constant(10000, 500);
    return r;
}

static void rig_free(rig *r)
{
    sk_sounds_free(r->s);
    sk_mixer_free(r->m);
    sk_pcm_unref(r->loop);
    sk_pcm_unref(r->once);
}

static sk_sounds_entry loop_entry(sk_pcm *p, float volume)
{
    return (sk_sounds_entry){.pcm = p, .loop = true, .volume = volume};
}

static sk_sounds_entry random_entry(sk_pcm *p, float volume, float lo, float hi)
{
    return (sk_sounds_entry){.pcm = p, .volume = volume, .interval = {lo, hi}};
}

static void test_starts_muted(void)
{
    rig r = rig_new(1);
    sk_sounds_entry e[] = {loop_entry(r.loop, 0.5f), random_entry(r.once, 0.3f, 1, 2)};
    CHECK(sk_sounds_muted(r.s));
    CHECK_INT(sk_sounds_set(r.s, e, 2), -1);
    CHECK_INT(sk_mixer_live(r.m), 0);
    sk_sounds_state st = sk_sounds_get(r.s, 1);
    CHECK(!st.playing && !st.waiting);
    fake_now = 10000;
    CHECK_INT(sk_sounds_tick(r.s), -1);
    CHECK_INT(sk_mixer_live(r.m), 0);
    rig_free(&r);
}

static void test_loop_plays_at_once_at_half_volume(void)
{
    rig r = rig_new(1);
    sk_sounds_mute(r.s, false);
    sk_sounds_entry e[] = {loop_entry(r.loop, 0.6f)};
    CHECK_INT(sk_sounds_set(r.s, e, 1), -1); /* a loop waits on nothing */
    sk_sounds_state st = sk_sounds_get(r.s, 0);
    CHECK(st.loop && st.playing && !st.waiting);
    CHECK_INT(st.plays, 1);
    CHECK(st.volume > 0.2999f && st.volume < 0.3001f);
    CHECK(abs(pull_ms(r.m, 10) - 6000) <= 1);
    /* It plays without end, the one play. */
    pull_ms(r.m, 1000);
    fake_now = 60000;
    CHECK_INT(sk_sounds_tick(r.s), -1);
    st = sk_sounds_get(r.s, 0);
    CHECK(st.playing);
    CHECK_INT(st.plays, 1);
    CHECK_INT(sk_mixer_live(r.m), 1);
    rig_free(&r);
}

static void test_volume_is_half_and_capped(void)
{
    rig r = rig_new(1);
    sk_sounds_mute(r.s, false);
    sk_sounds_entry e[] = {loop_entry(r.loop, 1.0f), loop_entry(r.loop, 7.0f), loop_entry(r.loop, -1.0f)};
    sk_sounds_set(r.s, e, 3);
    CHECK(sk_sounds_get(r.s, 0).volume == 0.5f);
    CHECK(sk_sounds_get(r.s, 1).volume == 0.5f);
    CHECK(sk_sounds_get(r.s, 2).volume == 0.0f);
    /* Two loops at 20000 and half volume, and a silent one. */
    CHECK(abs(pull_ms(r.m, 10) - 20000) <= 1);
    rig_free(&r);
}

/* A random sound's first play waits its interval too. */
static void test_random_first_play_waits(void)
{
    rig r = rig_new(7);
    sk_sounds_mute(r.s, false);
    sk_sounds_entry e[] = {random_entry(r.once, 0.4f, 2, 5)};
    long next = sk_sounds_set(r.s, e, 1);
    sk_sounds_state st = sk_sounds_get(r.s, 0);
    CHECK(st.waiting && !st.playing);
    CHECK(st.due >= 2000 && st.due <= 5000);
    CHECK_INT(next, (long)st.due);
    CHECK_INT(sk_mixer_live(r.m), 0);
    fake_now = st.due - 1;
    CHECK_INT(sk_sounds_tick(r.s), 1);
    CHECK_INT(sk_mixer_live(r.m), 0);
    fake_now = st.due;
    /* It plays, and the player looks again when it should have ended. */
    CHECK_INT(sk_sounds_tick(r.s), 500);
    st = sk_sounds_get(r.s, 0);
    CHECK(st.playing && !st.waiting);
    CHECK_INT(st.plays, 1);
    CHECK(abs(pull_ms(r.m, 100) - 2000) <= 1);
    rig_free(&r);
}

/* Once it has ended it waits again, from when it ended. */
static void test_random_waits_again_after_it_ends(void)
{
    rig r = rig_new(7);
    sk_sounds_mute(r.s, false);
    sk_sounds_entry e[] = {random_entry(r.once, 0.4f, 2, 5)};
    sk_sounds_set(r.s, e, 1);
    fake_now = sk_sounds_get(r.s, 0).due;
    sk_sounds_tick(r.s);
    uint64_t played = fake_now;
    /* The clock is at its end, but the output has not played it all:
     * the player asks again shortly. */
    pull_ms(r.m, 300);
    fake_now = played + 500;
    CHECK_INT(sk_sounds_tick(r.s), SK_SOUNDS_POLL_MS);
    sk_sounds_state st = sk_sounds_get(r.s, 0);
    CHECK(st.playing && !st.waiting);
    pull_ms(r.m, 300);
    CHECK_INT(sk_mixer_live(r.m), 0);
    fake_now = played + 550;
    long next = sk_sounds_tick(r.s);
    st = sk_sounds_get(r.s, 0);
    CHECK(st.waiting && !st.playing);
    CHECK(st.due >= fake_now + 2000 && st.due <= fake_now + 5000);
    CHECK_INT(next, (long)(st.due - fake_now));
    CHECK_INT(st.plays, 1);
    fake_now = st.due;
    sk_sounds_tick(r.s);
    CHECK_INT(sk_sounds_get(r.s, 0).plays, 2);
    rig_free(&r);
}

/* Plays a short random sound n times, keeping each wait. */
static void waits_of(uint64_t seed, float lo, float hi, uint64_t *waits, size_t n)
{
    rig r = rig_new(seed);
    sk_pcm *blip = constant(1000, 1);
    sk_sounds_mute(r.s, false);
    sk_sounds_entry e[] = {random_entry(blip, 1.0f, lo, hi)};
    sk_sounds_set(r.s, e, 1);
    for (size_t i = 0; i < n; i++) {
        sk_sounds_state st = sk_sounds_get(r.s, 0);
        waits[i] = st.due - fake_now;
        fake_now = st.due;
        sk_sounds_tick(r.s);
        pull_ms(r.m, 2);
        fake_now += 1;
        sk_sounds_tick(r.s);
    }
    CHECK_INT(sk_sounds_get(r.s, 0).plays, n);
    sk_pcm_unref(blip);
    rig_free(&r);
}

static void test_random_waits_drawn_evenly(void)
{
    enum { N = 400 };
    uint64_t w[N], again[N], other[N];
    waits_of(42, 1, 3, w, N);
    uint64_t lo = UINT64_MAX, hi = 0, sum = 0;
    size_t low_half = 0;
    for (size_t i = 0; i < N; i++) {
        if (w[i] < 1000 || w[i] > 3000)
            sk_test_fail(__FILE__, __LINE__, "wait %zu: %llu ms", i, (unsigned long long)w[i]);
        lo = SK_MIN(lo, w[i]);
        hi = SK_MAX(hi, w[i]);
        sum += w[i];
        low_half += w[i] < 2000;
    }
    CHECK(lo < 1100 && hi > 2900);
    CHECK(sum / N > 1850 && sum / N < 2150);
    CHECK(low_half > N * 2 / 5 && low_half < N * 3 / 5);
    /* The same seed draws the same waits; another, others. */
    waits_of(42, 1, 3, again, N);
    CHECK(memcmp(w, again, sizeof w) == 0);
    waits_of(43, 1, 3, other, N);
    CHECK(memcmp(w, other, sizeof w) != 0);
    /* An interval of one value waits exactly that. */
    waits_of(5, 4, 4, other, 10);
    for (size_t i = 0; i < 10; i++)
        CHECK_INT(other[i], 4000);
}

/* Another skin stops every sound and every wait of the last one. */
static void test_new_skin_stops_the_last(void)
{
    rig r = rig_new(3);
    sk_pcm *other = constant(-5000, 100);
    sk_sounds_mute(r.s, false);
    sk_sounds_entry a[] = {loop_entry(r.loop, 1.0f), random_entry(r.once, 1.0f, 1, 1)};
    sk_sounds_set(r.s, a, 2);
    sk_voice old = sk_sounds_get(r.s, 0).voice;
    pull_ms(r.m, 10);
    fake_now = 500;
    sk_sounds_entry b[] = {loop_entry(other, 1.0f)};
    CHECK_INT(sk_sounds_set(r.s, b, 1), -1);
    CHECK_INT(sk_sounds_count(r.s), 1);
    CHECK(!sk_mixer_playing(r.m, old));
    /* The old loop fades out under the new one. */
    pull_ms(r.m, 20);
    CHECK_INT(sk_mixer_live(r.m), 1);
    CHECK(abs(pull_ms(r.m, 10) - (-2500)) <= 1);
    /* The old random sound's wait went with it. */
    fake_now = 5000;
    CHECK_INT(sk_sounds_tick(r.s), -1);
    CHECK_INT(sk_mixer_live(r.m), 1);
    /* No sounds at all is a skin too. */
    CHECK_INT(sk_sounds_set(r.s, NULL, 0), -1);
    pull_ms(r.m, 20);
    CHECK_INT(sk_mixer_live(r.m), 0);
    sk_pcm_unref(other);
    rig_free(&r);
}

/* The same skin again, a reload, starts its sounds over. */
static void test_same_skin_restarts(void)
{
    rig r = rig_new(3);
    sk_sounds_mute(r.s, false);
    sk_sounds_entry a[] = {loop_entry(r.loop, 1.0f), random_entry(r.once, 1.0f, 10, 20)};
    sk_sounds_set(r.s, a, 2);
    sk_voice old = sk_sounds_get(r.s, 0).voice;
    fake_now = 9000;
    sk_sounds_set(r.s, a, 2);
    sk_sounds_state loop = sk_sounds_get(r.s, 0), rnd = sk_sounds_get(r.s, 1);
    CHECK(loop.playing && loop.voice != old);
    CHECK_INT(loop.plays, 1);
    CHECK(!sk_mixer_playing(r.m, old));
    CHECK(rnd.waiting);
    CHECK(rnd.due >= 19000 && rnd.due <= 29000);
    pull_ms(r.m, 20);
    CHECK_INT(sk_mixer_live(r.m), 1);
    rig_free(&r);
}

/* Muted, nothing plays and nothing waits; unmuted, it all starts over. */
static void test_mute_and_unmute(void)
{
    rig r = rig_new(9);
    sk_sounds_mute(r.s, false);
    sk_sounds_entry a[] = {loop_entry(r.loop, 1.0f), random_entry(r.once, 1.0f, 1, 2)};
    sk_sounds_set(r.s, a, 2);
    uint64_t due = sk_sounds_get(r.s, 1).due;
    fake_now = 100;
    CHECK_INT(sk_sounds_mute(r.s, true), -1);
    CHECK(sk_sounds_muted(r.s));
    sk_sounds_state loop = sk_sounds_get(r.s, 0), rnd = sk_sounds_get(r.s, 1);
    CHECK(!loop.playing && !rnd.waiting && !rnd.playing);
    pull_ms(r.m, 20);
    CHECK_INT(sk_mixer_live(r.m), 0);
    fake_now = due + 10000;
    CHECK_INT(sk_sounds_tick(r.s), -1);
    CHECK_INT(sk_mixer_live(r.m), 0);
    CHECK_INT(sk_sounds_get(r.s, 1).plays, 0);
    /* Muting again changes nothing. */
    CHECK_INT(sk_sounds_mute(r.s, true), -1);
    long next = sk_sounds_mute(r.s, false);
    loop = sk_sounds_get(r.s, 0);
    rnd = sk_sounds_get(r.s, 1);
    CHECK(loop.playing);
    CHECK_INT(loop.plays, 1);
    CHECK(rnd.waiting);
    CHECK(rnd.due >= fake_now + 1000 && rnd.due <= fake_now + 2000);
    CHECK_INT(next, (long)(rnd.due - fake_now));
    /* A skin set while muted starts at the unmute. */
    sk_sounds_mute(r.s, true);
    sk_sounds_set(r.s, a, 1);
    pull_ms(r.m, 20);
    CHECK_INT(sk_mixer_live(r.m), 0);
    sk_sounds_mute(r.s, false);
    CHECK_INT(sk_mixer_live(r.m), 1);
    rig_free(&r);
}

/* The soonest of several waits. */
static void test_tick_says_when_next(void)
{
    rig r = rig_new(11);
    sk_sounds_mute(r.s, false);
    sk_sounds_entry a[] = {random_entry(r.once, 1.0f, 30, 30), random_entry(r.once, 1.0f, 10, 10),
                           loop_entry(r.loop, 1.0f), random_entry(r.once, 1.0f, 20, 20)};
    CHECK_INT(sk_sounds_set(r.s, a, 4), 10000);
    fake_now = 4000;
    CHECK_INT(sk_sounds_tick(r.s), 6000);
    fake_now = 10000;
    CHECK_INT(sk_sounds_tick(r.s), 500); /* the second plays */
    rig_free(&r);
}

/* Windows share the mixer: muting one leaves the other's sounds. */
static void test_players_share_a_mixer(void)
{
    rig r = rig_new(1);
    sk_sounds *other = sk_sounds_new(r.m, fake_clock, NULL, 2);
    sk_sounds_mute(r.s, false);
    sk_sounds_mute(other, false);
    sk_sounds_entry a[] = {loop_entry(r.loop, 1.0f)};
    sk_sounds_set(r.s, a, 1);
    sk_sounds_set(other, a, 1);
    CHECK_INT(sk_mixer_live(r.m), 2);
    sk_sounds_mute(r.s, true);
    pull_ms(r.m, 20);
    CHECK_INT(sk_mixer_live(r.m), 1);
    CHECK(sk_sounds_get(other, 0).playing);
    sk_sounds_free(other);
    pull_ms(r.m, 20);
    CHECK_INT(sk_mixer_live(r.m), 0);
    rig_free(&r);
}

/* An entry with no sound keeps its place and stays silent; a skin's
 * sounds beyond 16 are dropped. */
static void test_odd_entries(void)
{
    rig r = rig_new(1);
    sk_sounds_mute(r.s, false);
    sk_sounds_entry a[20];
    for (size_t i = 0; i < 20; i++)
        a[i] = loop_entry(r.loop, 0.01f);
    a[0] = random_entry(NULL, 1.0f, 1, 1);
    sk_sounds_set(r.s, a, 20);
    CHECK_INT(sk_sounds_count(r.s), SK_SOUNDS_MAX);
    sk_sounds_state st = sk_sounds_get(r.s, 0);
    CHECK(!st.playing && !st.waiting);
    CHECK_INT(sk_mixer_live(r.m), SK_SOUNDS_MAX - 1);
    rig_free(&r);
}

/* A loop that found every voice busy plays once one is free. */
static void test_loop_waits_for_a_voice(void)
{
    rig r = rig_new(1);
    sk_voice v[SK_MIXER_VOICES];
    for (size_t i = 0; i < SK_MIXER_VOICES; i++)
        v[i] = sk_mixer_play(r.m, r.loop, 0.0f, true);
    sk_sounds_mute(r.s, false);
    sk_sounds_entry a[] = {loop_entry(r.loop, 1.0f)};
    CHECK_INT(sk_sounds_set(r.s, a, 1), SK_SOUNDS_POLL_MS);
    CHECK(!sk_sounds_get(r.s, 0).playing);
    sk_mixer_stop(r.m, v[0]);
    pull_ms(r.m, 20);
    CHECK_INT(sk_sounds_tick(r.s), -1);
    CHECK(sk_sounds_get(r.s, 0).playing);
    rig_free(&r);
}

const sk_test sk_suite_audio_sounds[] = {
    T(test_starts_muted),
    T(test_loop_plays_at_once_at_half_volume),
    T(test_volume_is_half_and_capped),
    T(test_random_first_play_waits),
    T(test_random_waits_again_after_it_ends),
    T(test_random_waits_drawn_evenly),
    T(test_new_skin_stops_the_last),
    T(test_same_skin_restarts),
    T(test_mute_and_unmute),
    T(test_tick_says_when_next),
    T(test_players_share_a_mixer),
    T(test_odd_entries),
    T(test_loop_waits_for_a_voice),
    {0},
};
