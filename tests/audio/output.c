/* platform/audio.h: the output's thread over the null sink, the control
 * side and the output side at once under stress, and Linux's players:
 * their command lines and the search of PATH everywhere, and on Linux
 * the writer piping to fake players, sh scripts that keep what they
 * read. A real device opens only with SK_AUDIO_TESTS=1, which nothing
 * sets, so no test makes a sound. SK_TEST_PIPE_OUTPUT builds the Linux
 * tests elsewhere, against platform/linux/audio.c. */
#include "../../src/platform/audio.h"
#include "../../src/audio/sounds.h"
#include "../../src/base/log.h"
#include "../test.h"

#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define RATE SK_MIXER_RATE

static sk_pcm *constant(int16_t v, size_t n, uint32_t rate)
{
    int16_t *f = sk_xreallocarray(NULL, n, sizeof *f);
    for (size_t i = 0; i < n; i++)
        f[i] = v;
    return sk_pcm_adopt(f, n, rate);
}

static uint64_t mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

/* What the null sink was handed: frames, frames not silent, and frames
 * no mix of these tests' sounds makes, the channels apart or below zero
 * (they are all positive). */
typedef struct {
    atomic_ulong frames, loud, wrong;
} tally;

static void record(void *ud, const int16_t *f, size_t n)
{
    tally *t = ud;
    unsigned long loud = 0, wrong = 0;
    for (size_t i = 0; i < n; i++) {
        loud += f[2 * i] != 0;
        wrong += (f[2 * i] != f[2 * i + 1] || f[2 * i] < 0);
    }
    atomic_fetch_add(&t->frames, n);
    atomic_fetch_add(&t->loud, loud);
    atomic_fetch_add(&t->wrong, wrong);
}

/* Until the voices have ended and the output has stopped. */
static bool settles(sk_audio *a, sk_mixer *m, unsigned ms)
{
    uint64_t end = mono_ms() + ms;
    while (sk_mixer_live(m) || (a && sk_audio_running(a))) {
        if (mono_ms() > end)
            return false;
        sk_test_sleep_ms(2);
    }
    return true;
}

static void test_null_sink_runs_while_sounds_play(void)
{
    sk_mixer *m = sk_mixer_new(RATE);
    tally t = {0};
    sk_audio_null o = {.record = record, .ud = &t, .unpaced = true, .idle_ms = 20};
    sk_audio *a = sk_audio_open_null(m, &o);
    CHECK_STR(sk_audio_name(a), "null");
    sk_test_sleep_ms(20);
    CHECK(!sk_audio_running(a));
    CHECK_INT(sk_audio_pulled(a), 0);
    sk_pcm *p = constant(1000, 4800, RATE);
    sk_mixer_play(m, p, 1.0f, false);
    CHECK(settles(a, m, 2000));
    CHECK_INT(atomic_load(&t.loud), 4800);
    CHECK_INT(atomic_load(&t.wrong), 0);
    uint64_t pulled = sk_audio_pulled(a);
    CHECK_INT(pulled, atomic_load(&t.frames));
    CHECK(pulled >= 4800 + 960);
    /* Stopped, it pulls nothing. */
    sk_test_sleep_ms(30);
    CHECK_INT(sk_audio_pulled(a), pulled);
    /* The next sound starts it again. */
    sk_mixer_play(m, p, 1.0f, false);
    CHECK(settles(a, m, 2000));
    CHECK_INT(atomic_load(&t.loud), 9600);
    sk_audio_close(a);
    sk_mixer_free(m);
    sk_pcm_unref(p);
}

/* A voice playing before the output opens starts it. */
static void test_null_sink_finds_a_voice_playing(void)
{
    sk_mixer *m = sk_mixer_new(RATE);
    sk_pcm *p = constant(1000, 480, RATE);
    sk_mixer_play(m, p, 1.0f, false);
    tally t = {0};
    sk_audio_null o = {.record = record, .ud = &t, .unpaced = true, .idle_ms = 1};
    sk_audio *a = sk_audio_open_null(m, &o);
    CHECK(settles(a, m, 2000));
    CHECK_INT(atomic_load(&t.loud), 480);
    sk_audio_close(a);
    sk_mixer_free(m);
    sk_pcm_unref(p);
}

/* In real time, no more than the lead ahead of the clock; closed while
 * it runs, it stops at once. */
static void test_null_sink_keeps_time(void)
{
    sk_mixer *m = sk_mixer_new(RATE);
    sk_audio *a = sk_audio_open_null(m, NULL);
    sk_pcm *p = constant(1000, 2 * RATE, RATE);
    uint64_t start = mono_ms();
    sk_mixer_play(m, p, 1.0f, false);
    for (int i = 0; i < 500 && !sk_audio_pulled(a); i++)
        sk_test_sleep_ms(2);
    sk_test_sleep_ms(150);
    uint64_t pulled = sk_audio_pulled(a), elapsed = mono_ms() - start;
    CHECK(pulled > 0);
    CHECK(pulled <= (elapsed + SK_AUDIO_LEAD_MS) * RATE / 1000 + SK_AUDIO_PERIOD);
    CHECK(sk_audio_running(a));
    CHECK_INT(sk_mixer_live(m), 1);
    uint64_t t0 = mono_ms();
    sk_audio_close(a);
    CHECK(mono_ms() - t0 < 500);
    sk_mixer_free(m);
    sk_pcm_unref(p);
}

/* ---- Both sides at once ---- */

static sk_pcm *pool[8];

typedef struct {
    sk_mixer *m;
    uint64_t seed;
    unsigned ops;
} worker;

static uint64_t lcg(uint64_t *r)
{
    *r = *r * 6364136223846793005u + 1442695040888963407u;
    return *r >> 16;
}

/* A control thread: plays, stops and asks about voices at random, and
 * drops sounds while they play. */
static void *control(void *ud)
{
    worker *w = ud;
    static const uint32_t rates[] = {8000, 22050, 44100, 48000};
    sk_voice mine[64] = {0};
    uint64_t r = w->seed;
    for (unsigned i = 0; i < w->ops; i++) {
        uint64_t x = lcg(&r);
        size_t k = (size_t)(x >> 8) % 64;
        float volume = (float)(x % 1000) / 1000.0f;
        switch (x % 10) {
        case 0:
        case 1:
        case 2:
            mine[k] = sk_mixer_play(w->m, pool[(x >> 20) % SK_LEN(pool)], volume, (x >> 30) % 4 == 0);
            break;
        case 3: {
            sk_pcm *p = constant((int16_t)(1 + (x >> 12) % 1000), 1 + (x >> 24) % 3000, rates[(x >> 40) % 4]);
            mine[k] = sk_mixer_play(w->m, p, volume, false);
            sk_pcm_unref(p);
            break;
        }
        case 4:
        case 5:
            sk_mixer_stop(w->m, mine[k]);
            break;
        case 6:
        case 7:
        case 8:
            (void)sk_mixer_playing(w->m, mine[k]);
            break;
        default:
            if ((x >> 32) % 64 == 0)
                sk_mixer_stop_all(w->m);
            break;
        }
        if (i % 128 == 0)
            sched_yield();
    }
    return NULL;
}

static uint64_t stress_now;

static uint64_t stress_clock(void *ud)
{
    (void)ud;
    return stress_now;
}

/* The output's thread pulls as fast as it can while two control threads
 * start and stop voices and a player changes skins and mutes on this
 * one. The voice slots pass between the sides by their atomic states,
 * so AddressSanitizer would see a sound freed under a voice, and the
 * frames would show a slot read half-filled. */
static void test_stress_both_sides(void)
{
    for (size_t i = 0; i < SK_LEN(pool); i++)
        pool[i] = constant((int16_t)(100 + 100 * i), 200 + 700 * i, i % 2 ? 22050 : RATE);
    sk_mixer *m = sk_mixer_new(RATE);
    tally t = {0};
    sk_audio_null o = {.record = record, .ud = &t, .unpaced = true, .idle_ms = 5};
    sk_audio *a = sk_audio_open_null(m, &o);
    worker w[2] = {{m, 1, 20000}, {m, 2, 20000}};
    pthread_t th[2];
    for (size_t i = 0; i < 2; i++)
        CHECK(pthread_create(&th[i], NULL, control, &w[i]) == 0);
    sk_sounds *s = sk_sounds_new(m, stress_clock, NULL, 3);
    sk_sounds_entry skins[2][3] = {
        {{pool[0], true, 0.5f, {0, 0}}, {pool[1], false, 1.0f, {0.001f, 0.01f}}, {pool[2], false, 0.3f, {0, 0}}},
        {{pool[3], false, 1.0f, {0, 0.005f}}, {pool[4], true, 0.2f, {0, 0}}, {NULL, false, 1.0f, {1, 1}}},
    };
    uint64_t r = 99;
    for (unsigned i = 0; i < 4000; i++) {
        uint64_t x = lcg(&r);
        stress_now += x % 7;
        switch (x % 20) {
        case 0:
            sk_sounds_set(s, skins[(x >> 8) % 2], 3);
            break;
        case 1:
            sk_sounds_mute(s, (x >> 8) % 2);
            break;
        default:
            sk_sounds_tick(s);
            break;
        }
        if (i % 64 == 0)
            sched_yield();
    }
    for (size_t i = 0; i < 2; i++)
        pthread_join(th[i], NULL);
    sk_sounds_free(s);
    sk_mixer_stop_all(m);
    CHECK(settles(a, m, 5000));
    CHECK(atomic_load(&t.frames) > 0);
    CHECK(atomic_load(&t.loud) > 0);
    CHECK_INT(atomic_load(&t.wrong), 0);
    sk_audio_close(a);
    sk_mixer_free(m);
    for (size_t i = 0; i < SK_LEN(pool); i++)
        sk_pcm_unref(pool[i]);
}

/* ---- Linux's players ---- */

static void check_argv(sk_audio_player p, bool big_endian, const char *want)
{
    sk_strv v = {0};
    sk_audio_player_argv(&v, p, "/usr/bin/x", 48000, 2, big_endian);
    sk_buf b = {0};
    for (size_t i = 0; i < v.len; i++)
        sk_buf_printf(&b, "%s%s", i ? " " : "", v.ptr[i]);
    CHECK_STR(b.ptr, want);
    CHECK(v.ptr[v.len] == NULL);
    sk_buf_free(&b);
    sk_strv_free(&v);
}

static void test_player_command_lines(void)
{
    CHECK_STR(sk_audio_player_name(SK_AUDIO_PW_PLAY), "pw-play");
    CHECK_STR(sk_audio_player_name(SK_AUDIO_PAPLAY), "paplay");
    CHECK_STR(sk_audio_player_name(SK_AUDIO_APLAY), "aplay");
    /* pw-play's s16 is the host's own order. */
    check_argv(SK_AUDIO_PW_PLAY, false, "/usr/bin/x --raw --format=s16 --rate=48000 --channels=2 -");
    check_argv(SK_AUDIO_PW_PLAY, true, "/usr/bin/x --raw --format=s16 --rate=48000 --channels=2 -");
    check_argv(SK_AUDIO_PAPLAY, false,
               "/usr/bin/x --raw --format=s16le --rate=48000 --channels=2 --latency-msec=100 --client-name=skinned");
    check_argv(SK_AUDIO_PAPLAY, true,
               "/usr/bin/x --raw --format=s16be --rate=48000 --channels=2 --latency-msec=100 --client-name=skinned");
    check_argv(SK_AUDIO_APLAY, false, "/usr/bin/x -q -t raw -f S16_LE -r 48000 -c 2 -");
    check_argv(SK_AUDIO_APLAY, true, "/usr/bin/x -q -t raw -f S16_BE -r 48000 -c 2 -");
    sk_strv v = {0};
    sk_audio_player_argv(&v, SK_AUDIO_APLAY, "aplay", 22050, 1, false);
    CHECK_STR(v.ptr[7], "22050");
    CHECK_STR(v.ptr[9], "1");
    sk_strv_free(&v);
}

static void put_file(const char *path, const char *text, mode_t mode)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    CHECK(fd >= 0);
    if (fd < 0)
        return;
    CHECK(write(fd, text, strlen(text)) == (ssize_t)strlen(text));
    close(fd);
    CHECK(chmod(path, mode) == 0);
}

static char *path_in(const char *dir, const char *name)
{
    sk_buf b = {0};
    sk_buf_printf(&b, "%s/%s", dir, name);
    return b.ptr;
}

static void put_in(const char *dir, const char *name, const char *text, mode_t mode)
{
    char *f = path_in(dir, name);
    put_file(f, text, mode);
    free(f);
}

static void test_players_found_on_path(void)
{
    const char *tmp = sk_test_tmpdir();
    char *a = path_in(tmp, "a"), *b = path_in(tmp, "b"), *c = path_in(tmp, "c");
    mkdir(a, 0700);
    mkdir(b, 0700);
    mkdir(c, 0700);
    put_in(a, "paplay", "", 0755);
    put_in(b, "paplay", "", 0755);
    put_in(b, "pw-play", "", 0755);
    put_in(c, "aplay", "", 0644); /* not executable */
    char *dir = path_in(a, "aplay");
    mkdir(dir, 0700); /* not a file */
    free(dir);
    sk_buf path = {0};
    sk_buf_printf(&path, "a::/nonexistent:%s:%s:%s", a, b, c);
    sk_audio_player found[SK_AUDIO_PLAYERS];
    sk_strv paths = {0};
    CHECK_INT(sk_audio_player_find(path.ptr, found, &paths), 2);
    CHECK_INT(found[0], SK_AUDIO_PW_PLAY);
    CHECK_INT(found[1], SK_AUDIO_PAPLAY);
    char *want0 = path_in(b, "pw-play"), *want1 = path_in(a, "paplay");
    CHECK_STR(paths.ptr[0], want0);
    CHECK_STR(paths.ptr[1], want1);
    free(want0);
    free(want1);
    sk_strv_free(&paths);
    /* A relative entry is never searched, even where it names one. */
    char cwd[4096];
    CHECK(getcwd(cwd, sizeof cwd) != NULL);
    CHECK(chdir(tmp) == 0);
    CHECK_INT(sk_audio_player_find("a:b:.:", found, &paths), 0);
    CHECK(chdir(cwd) == 0);
    CHECK_INT(sk_audio_player_find(NULL, found, &paths), 0);
    CHECK_INT(sk_audio_player_find("", found, &paths), 0);
    sk_strv_free(&paths);
    sk_buf_free(&path);
    free(a);
    free(b);
    free(c);
}

#if defined(__linux__) || defined(SK_TEST_PIPE_OUTPUT)

/* The system's output with PATH as given while it looks for players. */
static sk_audio *open_with_path(sk_mixer *m, const char *path)
{
    const char *old = getenv("PATH");
    char *keep = old ? sk_xstrdup(old) : NULL;
    setenv("PATH", path, 1);
    sk_audio *a = sk_audio_open(m);
    if (keep)
        setenv("PATH", keep, 1);
    else
        unsetenv("PATH");
    free(keep);
    return a;
}

/* A fake player that keeps what it reads in out. */
static void keeper(const char *bin, const char *name, const char *out)
{
    sk_buf text = {0};
    sk_buf_printf(&text, "#!/bin/sh\nexec /bin/cat > '%s'\n", out);
    put_in(bin, name, text.ptr, 0755);
    sk_buf_free(&text);
}

/* The frames in out: how many are not silent; false if any is odd. */
static bool kept(const char *out, size_t *loud)
{
    size_t len = 0;
    char *data = sk_test_read(out, &len);
    *loud = 0;
    if (!data)
        return false;
    bool ok = len % 4 == 0;
    const int16_t *f = (const int16_t *)(const void *)data;
    for (size_t i = 0; i < len / 4; i++) {
        ok = ok && f[2 * i] == f[2 * i + 1] && f[2 * i] >= 0;
        *loud += f[2 * i] != 0;
    }
    free(data);
    return ok;
}

static void quiet_log(bool on)
{
    static int fd = -1;
    if (on) {
        fd = open("/dev/null", O_WRONLY);
        sk_log_set_fd(fd);
    } else {
        sk_log_set_fd(2);
        close(fd);
    }
}

static void test_pipe_feeds_the_player(void)
{
    const char *tmp = sk_test_tmpdir();
    char *bin = path_in(tmp, "bin"), *out = path_in(tmp, "out.raw");
    mkdir(bin, 0700);
    keeper(bin, "paplay", out);
    sk_mixer *m = sk_mixer_new(RATE);
    sk_audio *a = open_with_path(m, bin);
    CHECK_STR(sk_audio_name(a), "paplay");
    sk_pcm *p = constant(10000, 2400, RATE);
    sk_mixer_play(m, p, 1.0f, false);
    CHECK(settles(NULL, m, 2000));
    CHECK(sk_audio_running(a));
    sk_test_sleep_ms(300);
    sk_audio_close(a);
    size_t loud;
    CHECK(kept(out, &loud));
    CHECK_INT(loud, 2400);
    sk_mixer_free(m);
    sk_pcm_unref(p);
    free(bin);
    free(out);
}

/* A player that exits as it starts, as a pw-play without --raw does,
 * gives way to the next. */
static void test_pipe_falls_back(void)
{
    const char *tmp = sk_test_tmpdir();
    char *bin = path_in(tmp, "bin"), *out = path_in(tmp, "out.raw");
    mkdir(bin, 0700);
    put_in(bin, "pw-play", "#!/bin/sh\nexit 1\n", 0755);
    keeper(bin, "paplay", out);
    quiet_log(true);
    sk_mixer *m = sk_mixer_new(RATE);
    sk_audio *a = open_with_path(m, bin);
    CHECK_STR(sk_audio_name(a), "pw-play");
    sk_pcm *p = constant(10000, RATE, RATE);
    sk_mixer_play(m, p, 1.0f, false);
    CHECK(settles(NULL, m, 3000));
    sk_test_sleep_ms(300);
    sk_audio_close(a);
    quiet_log(false);
    size_t loud;
    CHECK(kept(out, &loud));
    /* What went to the first before it died is lost. */
    CHECK(loud >= RATE / 2 && loud <= RATE);
    sk_mixer_free(m);
    sk_pcm_unref(p);
    free(bin);
    free(out);
}

/* A player that reads nothing fills the pipe; closing the output then
 * still returns at once. */
static void test_pipe_close_with_a_hung_player(void)
{
    const char *tmp = sk_test_tmpdir();
    char *bin = path_in(tmp, "bin");
    mkdir(bin, 0700);
    put_in(bin, "paplay", "#!/bin/sh\nexec /bin/sleep 30\n", 0755);
    sk_mixer *m = sk_mixer_new(RATE);
    sk_pcm *p = constant(10000, 10 * RATE, RATE);
    sk_audio *a = open_with_path(m, bin);
    sk_mixer_play(m, p, 1.0f, false);
    /* 64 KiB of pipe is a third of a second. */
    sk_test_sleep_ms(800);
    uint64_t pulled = sk_audio_pulled(a);
    sk_test_sleep_ms(200);
    CHECK_INT(sk_audio_pulled(a), pulled);
    uint64_t t0 = mono_ms();
    sk_audio_close(a);
    CHECK(mono_ms() - t0 < 1000);
    sk_mixer_free(m);
    sk_pcm_unref(p);
    free(bin);
}

/* No player, or none that works: silence, the voices still end, and the
 * app goes on. */
static void test_pipe_without_a_player(void)
{
    const char *tmp = sk_test_tmpdir();
    char *bin = path_in(tmp, "bin");
    mkdir(bin, 0700);
    quiet_log(true);
    sk_mixer *m = sk_mixer_new(RATE);
    sk_pcm *p = constant(10000, RATE / 10, RATE);
    sk_audio *a = open_with_path(m, bin);
    CHECK_STR(sk_audio_name(a), "silent");
    sk_mixer_play(m, p, 1.0f, false);
    CHECK(settles(NULL, m, 2000));
    sk_audio_close(a);
    put_in(bin, "aplay", "#!/bin/sh\nexit 3\n", 0755);
    a = open_with_path(m, bin);
    CHECK_STR(sk_audio_name(a), "aplay");
    sk_mixer_play(m, p, 1.0f, false);
    CHECK(settles(NULL, m, 2000));
    sk_mixer_play(m, p, 1.0f, false);
    CHECK(settles(NULL, m, 2000));
    sk_audio_close(a);
    quiet_log(false);
    sk_mixer_free(m);
    sk_pcm_unref(p);
    free(bin);
}

#endif

/* The system's device itself, only when asked for: a quiet 440 Hz for
 * a third of a second. */
static void test_device_smoke(void)
{
    const char *on = getenv("SK_AUDIO_TESTS");
    if (!on || strcmp(on, "1") != 0)
        return;
    size_t n = RATE / 3;
    int16_t *f = sk_xreallocarray(NULL, n, sizeof *f);
    for (size_t i = 0; i < n; i++)
        f[i] = (int16_t)lrint(3000.0 * sin(2 * M_PI * 440.0 * (double)i / RATE));
    sk_pcm *p = sk_pcm_adopt(f, n, RATE);
    sk_mixer *m = sk_mixer_new(RATE);
    sk_audio *a = sk_audio_open(m);
    fprintf(stderr, "    sound output: %s\n", sk_audio_name(a));
    sk_mixer_play(m, p, 0.5f, false);
    CHECK(settles(NULL, m, 3000));
    CHECK(sk_audio_pulled(a) >= n);
    sk_audio_close(a);
    sk_mixer_free(m);
    sk_pcm_unref(p);
}

const sk_test sk_suite_audio_output[] = {
    T(test_null_sink_runs_while_sounds_play),
    T(test_null_sink_finds_a_voice_playing),
    T(test_null_sink_keeps_time),
    T(test_stress_both_sides),
    T(test_player_command_lines),
    T(test_players_found_on_path),
#if defined(__linux__) || defined(SK_TEST_PIPE_OUTPUT)
    T(test_pipe_feeds_the_player),
    T(test_pipe_falls_back),
    T(test_pipe_close_with_a_hung_player),
    T(test_pipe_without_a_player),
#endif
    T(test_device_smoke),
    {0},
};
