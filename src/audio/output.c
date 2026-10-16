/* The output's thread (platform/audio.h), the same on every system: the
 * device is what differs, and the null sink has none. */
#include "../platform/audio.h"

#include "../base/log.h"

#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <time.h>

/* A writer that falls this far behind the clock starts its count again
 * rather than catch up in a burst. */
#define BEHIND_NS 500000000u

struct sk_audio {
    sk_mixer *m;
    uint32_t rate;
    sk_audio_device dev;
    bool has_dev;
    sk_audio_null null;
    unsigned idle_ms;
    const char *name;
    bool threaded;
    pthread_t thread;
    /* Taken by the control side to wake the thread and by the thread to
     * read wake and quit, never across a pull or a write. */
    pthread_mutex_t lock;
    pthread_cond_t cond;
    bool wake, quit;
    atomic_bool closing;
    atomic_bool running;
    atomic_uint_least64_t pulled;
};

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

static void sleep_ns(uint64_t ns)
{
    struct timespec ts = {(time_t)(ns / 1000000000u), (long)(ns % 1000000000u)};
    while (nanosleep(&ts, &ts) != 0)
        ;
}

bool sk_audio_pull(sk_audio *a, int16_t *frames, size_t n)
{
    bool sounded = sk_mixer_pull(a->m, frames, n);
    atomic_fetch_add_explicit(&a->pulled, n, memory_order_relaxed);
    return sounded;
}

bool sk_audio_closing(const sk_audio *a)
{
    return atomic_load(&((sk_audio *)a)->closing);
}

const char *sk_audio_name(const sk_audio *a)
{
    return a->name;
}

bool sk_audio_running(const sk_audio *a)
{
    return atomic_load(&((sk_audio *)a)->running);
}

uint64_t sk_audio_pulled(const sk_audio *a)
{
    return atomic_load(&((sk_audio *)a)->pulled);
}

/* The mixer's hook, on the control side. */
static void on_wake(void *ud)
{
    sk_audio *a = ud;
    pthread_mutex_lock(&a->lock);
    a->wake = true;
    pthread_cond_signal(&a->cond);
    pthread_mutex_unlock(&a->lock);
}

/* Takes a wake, if one came; false once the output is closing. */
static bool take_wake(sk_audio *a, bool *woke)
{
    pthread_mutex_lock(&a->lock);
    bool go = !a->quit;
    *woke = a->wake;
    a->wake = false;
    pthread_mutex_unlock(&a->lock);
    return go;
}

/* Waits up to ms for a wake or the close. */
static void doze(sk_audio *a, unsigned ms)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t ns = (uint64_t)ts.tv_nsec + (uint64_t)ms * 1000000u;
    ts.tv_sec += (time_t)(ns / 1000000000u);
    ts.tv_nsec = (long)(ns % 1000000000u);
    pthread_mutex_lock(&a->lock);
    if (!a->wake && !a->quit)
        pthread_cond_timedwait(&a->cond, &a->lock, &ts);
    pthread_mutex_unlock(&a->lock);
}

/* A device that pulls on its own thread: the output only watches for
 * the silence that stops it. */
static void watch(sk_audio *a)
{
    uint64_t quiet_since = now_ns();
    bool woke;
    while (take_wake(a, &woke)) {
        uint64_t now = now_ns();
        if (woke || sk_mixer_live(a->m))
            quiet_since = now;
        else if (now - quiet_since >= (uint64_t)a->idle_ms * 1000000u)
            break;
        doze(a, 100);
    }
}

/* One stretch of sound, from a wake until the mixer has been silent for
 * the idle time or the output closes. Without a working device the
 * frames are dropped, in real time unless the null sink says not. */
static void run(sk_audio *a, int16_t *buf)
{
    bool device = a->has_dev && a->dev.start(a->dev.dev);
    atomic_store(&a->running, true);
    if (device && !a->dev.write) {
        watch(a);
    } else {
        const uint64_t idle = (uint64_t)a->idle_ms * a->rate / 1000u;
        const uint64_t lead = (uint64_t)SK_AUDIO_LEAD_MS * 1000000u;
        uint64_t t0 = now_ns(), sent = 0, silent = 0;
        bool woke;
        while (take_wake(a, &woke)) {
            bool sounded = sk_audio_pull(a, buf, SK_AUDIO_PERIOD);
            silent = (sounded || woke) ? 0 : silent + SK_AUDIO_PERIOD;
            if (device) {
                if (!a->dev.write(a->dev.dev, buf, SK_AUDIO_PERIOD)) {
                    a->dev.stop(a->dev.dev);
                    device = false;
                }
            } else if (a->null.record) {
                a->null.record(a->null.ud, buf, SK_AUDIO_PERIOD);
            }
            if (silent >= idle && !sk_mixer_live(a->m))
                break;
            if (!device && a->null.unpaced)
                continue;
            /* Paced: no more than the lead ahead of the clock. */
            sent += SK_AUDIO_PERIOD;
            uint64_t ahead = sent * 1000000000u / a->rate, elapsed = now_ns() - t0;
            if (elapsed > ahead + BEHIND_NS) {
                t0 = now_ns();
                sent = 0;
            } else if (ahead > elapsed + lead) {
                sleep_ns(ahead - elapsed - lead);
            }
        }
    }
    if (device)
        a->dev.stop(a->dev.dev);
    atomic_store(&a->running, false);
}

static void *output_main(void *ud)
{
    sk_audio *a = ud;
    /* Linux sends a pipe's SIGPIPE to the thread that wrote, so a player
     * that went away is an error of the write even where the process
     * does not ignore it, as the app does. */
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
    int16_t *buf = sk_xreallocarray(NULL, SK_AUDIO_PERIOD * SK_MIXER_CHANNELS, sizeof *buf);
    pthread_mutex_lock(&a->lock);
    while (!a->quit) {
        if (!a->wake && !sk_mixer_live(a->m)) {
            pthread_cond_wait(&a->cond, &a->lock);
            continue;
        }
        a->wake = false;
        pthread_mutex_unlock(&a->lock);
        run(a, buf);
        pthread_mutex_lock(&a->lock);
    }
    pthread_mutex_unlock(&a->lock);
    free(buf);
    return NULL;
}

static sk_audio *open_output(sk_mixer *m, bool system, const sk_audio_null *opts)
{
    sk_audio *a = sk_xcalloc(1, sizeof *a);
    a->m = m;
    a->rate = sk_mixer_rate(m);
    if (opts)
        a->null = *opts;
    a->idle_ms = a->null.idle_ms ? a->null.idle_ms : SK_AUDIO_IDLE_MS;
    pthread_mutex_init(&a->lock, NULL);
    pthread_cond_init(&a->cond, NULL);
    atomic_init(&a->closing, false);
    atomic_init(&a->running, false);
    atomic_init(&a->pulled, 0);
    a->name = "null";
    if (system) {
        a->has_dev = sk_audio_device_open(a, a->rate, &a->dev);
        a->name = a->has_dev ? a->dev.name : "silent";
    }
    a->threaded = pthread_create(&a->thread, NULL, output_main, a) == 0;
    if (a->threaded)
        sk_mixer_on_wake(m, on_wake, a);
    else
        sk_log("skin sounds: cannot start the output's thread: they are silent");
    return a;
}

sk_audio *sk_audio_open(sk_mixer *m)
{
    return open_output(m, true, NULL);
}

sk_audio *sk_audio_open_null(sk_mixer *m, const sk_audio_null *opts)
{
    return open_output(m, false, opts);
}

void sk_audio_close(sk_audio *a)
{
    if (!a)
        return;
    sk_mixer_on_wake(a->m, NULL, NULL);
    atomic_store(&a->closing, true);
    pthread_mutex_lock(&a->lock);
    a->quit = true;
    pthread_cond_signal(&a->cond);
    pthread_mutex_unlock(&a->lock);
    if (a->threaded)
        pthread_join(a->thread, NULL);
    if (a->has_dev)
        a->dev.free(a->dev.dev);
    pthread_cond_destroy(&a->cond);
    pthread_mutex_destroy(&a->lock);
    free(a);
}
