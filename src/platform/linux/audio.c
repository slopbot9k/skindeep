/* platform/audio.h on Linux (decision 0036): the writer thread pipes the
 * mixer's frames, raw, to a player child, the first of pw-play, paplay
 * and aplay on PATH that works. The child starts when the sounds start
 * and its stdin closes after a silence. A player that exits within
 * EARLY_MS of its start, as an old pw-play without --raw does, or one
 * that cannot be run, is not tried again, and the next one starts in its
 * place; a player that exits later, or takes nothing for STALL_MS, ends
 * the sound until the sounds next start. Each failure is one log line;
 * none blocks the app. */
#include "../audio.h"

#include "../../base/log.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

#define EARLY_MS 2000u
#define STALL_MS 5000u
/* How long a player whose stdin closed has to exit before a SIGTERM, and
 * then a SIGKILL. */
#define EXIT_MS 1000u

typedef struct {
    sk_audio *a;
    uint32_t rate;
    size_t n;
    sk_audio_player kinds[SK_AUDIO_PLAYERS];
    sk_strv paths;
    bool bad[SK_AUDIO_PLAYERS];
    size_t cur; /* the player running */
    pid_t pid;  /* -1: none */
    int fd;     /* its stdin */
    uint64_t started;
    bool told; /* that no player is left */
} pipe_dev;

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static bool big_endian(void)
{
    const uint16_t one = 1;
    return *(const uint8_t *)&one == 0;
}

/* A descriptor moved above stdio, closed on exec. */
static int high_cloexec(int fd)
{
    int r = fcntl(fd, F_DUPFD_CLOEXEC, 3);
    close(fd);
    return r;
}

static bool spawn(pipe_dev *d, size_t i)
{
    const char *name = sk_audio_player_name(d->kinds[i]);
    int p[2];
    if (pipe(p) != 0) {
        sk_log("skin sounds: no pipe for %s: %s", name, strerror(errno));
        return false;
    }
    int r = high_cloexec(p[0]), w = high_cloexec(p[1]);
    if (r < 0 || w < 0) {
        if (r >= 0)
            close(r);
        if (w >= 0)
            close(w);
        sk_log("skin sounds: no pipe for %s", name);
        return false;
    }
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, r, 0);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    /* The writer's mask blocks SIGPIPE and the app ignores it: the
     * player gets the defaults. */
    posix_spawnattr_t at;
    posix_spawnattr_init(&at);
    sigset_t none, pipe_sig;
    sigemptyset(&none);
    sigemptyset(&pipe_sig);
    sigaddset(&pipe_sig, SIGPIPE);
    posix_spawnattr_setsigmask(&at, &none);
    posix_spawnattr_setsigdefault(&at, &pipe_sig);
    posix_spawnattr_setflags(&at, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
    sk_strv argv = {0};
    sk_audio_player_argv(&argv, d->kinds[i], d->paths.ptr[i], d->rate, SK_MIXER_CHANNELS, big_endian());
    pid_t pid;
    int err = posix_spawn(&pid, d->paths.ptr[i], &fa, &at, sk_strv_argv(&argv), environ);
    sk_strv_free(&argv);
    posix_spawnattr_destroy(&at);
    posix_spawn_file_actions_destroy(&fa);
    close(r);
    if (err) {
        close(w);
        sk_log("skin sounds: cannot run %s: %s", d->paths.ptr[i], strerror(err));
        return false;
    }
    int fl = fcntl(w, F_GETFL);
    fcntl(w, F_SETFL, (fl < 0 ? 0 : fl) | O_NONBLOCK);
    d->cur = i;
    d->pid = pid;
    d->fd = w;
    d->started = now_ms();
    return true;
}

/* How the player ended, for the log. */
static void describe(int ws, char *out, size_t cap)
{
    if (WIFEXITED(ws))
        snprintf(out, cap, "status %d", WEXITSTATUS(ws));
    else if (WIFSIGNALED(ws))
        snprintf(out, cap, "signal %d", WTERMSIG(ws));
    else
        snprintf(out, cap, "wait status %d", ws);
}

/* Closes the player's stdin and waits for it to exit: at once when the
 * output is closing or it hung, else after it plays what it holds. */
static int reap(pipe_dev *d, bool now)
{
    close(d->fd);
    d->fd = -1;
    int ws = 0, sig = now ? SIGTERM : 0;
    if (sig)
        kill(d->pid, sig);
    uint64_t since = now_ms();
    for (;;) {
        pid_t r = waitpid(d->pid, &ws, WNOHANG);
        if (r == d->pid || (r < 0 && errno != EINTR))
            break;
        if (now_ms() - since >= EXIT_MS && sig != SIGKILL) {
            sig = sig ? SIGKILL : SIGTERM;
            kill(d->pid, sig);
            since = now_ms();
        }
        struct timespec ts = {0, 10 * 1000000};
        nanosleep(&ts, NULL);
    }
    d->pid = -1;
    return ws;
}

static bool pipe_start(void *dev)
{
    pipe_dev *d = dev;
    for (size_t i = 0; i < d->n; i++) {
        if (d->bad[i])
            continue;
        if (spawn(d, i))
            return true;
        d->bad[i] = true;
    }
    if (!d->told)
        sk_log("skin sounds: no player works: they are silent");
    d->told = true;
    return false;
}

enum { WROTE, CLOSING, DIED, STALLED };

static int write_all(pipe_dev *d, const char *p, size_t len)
{
    uint64_t last = now_ms();
    while (len) {
        ssize_t w = write(d->fd, p, len);
        if (w > 0) {
            p += w;
            len -= (size_t)w;
            last = now_ms();
            continue;
        }
        if (w < 0 && errno == EINTR)
            continue;
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (sk_audio_closing(d->a))
                return CLOSING;
            if (now_ms() - last >= STALL_MS)
                return STALLED;
            struct pollfd pfd = {.fd = d->fd, .events = POLLOUT};
            poll(&pfd, 1, 100);
            continue;
        }
        return DIED;
    }
    return WROTE;
}

static bool pipe_write(void *dev, const int16_t *frames, size_t n)
{
    pipe_dev *d = dev;
    for (;;) {
        int r = write_all(d, (const char *)frames, n * SK_MIXER_CHANNELS * sizeof *frames);
        if (r == WROTE)
            return true;
        if (r == CLOSING)
            return false;
        const char *name = sk_audio_player_name(d->kinds[d->cur]);
        bool early = r == DIED && now_ms() - d->started < EARLY_MS;
        char how[32];
        describe(reap(d, r == STALLED), how, sizeof how);
        if (r == STALLED) {
            sk_log("skin sounds: %s took nothing for %u s and was stopped: silent until the sounds next start",
                   name, STALL_MS / 1000u);
            return false;
        }
        if (!early) {
            sk_log("skin sounds: %s exited (%s): silent until the sounds next start", name, how);
            return false;
        }
        sk_log("skin sounds: %s exited as it started (%s): not trying it again", name, how);
        d->bad[d->cur] = true;
        if (!pipe_start(d))
            return false;
    }
}

static void pipe_stop(void *dev)
{
    pipe_dev *d = dev;
    if (d->pid > 0)
        reap(d, sk_audio_closing(d->a));
}

static void pipe_free(void *dev)
{
    pipe_dev *d = dev;
    if (d->pid > 0)
        reap(d, true);
    sk_strv_free(&d->paths);
    free(d);
}

bool sk_audio_device_open(sk_audio *a, uint32_t rate, sk_audio_device *out)
{
    pipe_dev *d = sk_xcalloc(1, sizeof *d);
    const char *path = getenv("PATH");
    d->n = sk_audio_player_find(path ? path : "/usr/bin:/bin", d->kinds, &d->paths);
    if (!d->n) {
        sk_log("skin sounds: no pw-play, paplay or aplay on PATH: they are silent");
        sk_strv_free(&d->paths);
        free(d);
        return false;
    }
    d->a = a;
    d->rate = rate;
    d->pid = -1;
    d->fd = -1;
    *out = (sk_audio_device){
        .name = sk_audio_player_name(d->kinds[0]),
        .dev = d,
        .start = pipe_start,
        .write = pipe_write,
        .stop = pipe_stop,
        .free = pipe_free,
    };
    return true;
}
