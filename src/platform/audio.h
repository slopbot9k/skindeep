/* The sound output (decision 0036). On macOS the mixer's frames go to an
 * AudioToolbox AudioQueue, whose callback pulls them on one of the
 * queue's own threads. On Linux a writer thread pipes them, raw, to one
 * player child, the first of pw-play, paplay and aplay on PATH.
 *
 * An output has a thread of its own. It sleeps until a voice starts,
 * then starts the device and runs until the mixer has been silent for
 * SK_AUDIO_IDLE_MS, when the AudioQueue pauses or the player's stdin
 * closes. On Linux the thread is the writer; it pulls a period at a time
 * and writes at most SK_AUDIO_LEAD_MS ahead of the clock. A device that
 * cannot start, or a player that dies, leaves the sounds silent with a
 * log line and the app goes on: the thread then drops the mixer's frames
 * in real time, so voices still end, and tries the device again when the
 * sounds next start after a silence. Starting a voice wakes the thread
 * through a mutex the thread holds only to read two flags, so the
 * control side never waits on the output.
 *
 * The null sink is an output without a device: its thread pulls the
 * mixer as a device would and hands each block to a callback, or drops
 * it. The tests use it, so nothing they play reaches a speaker. */
#ifndef SK_PLATFORM_AUDIO_H
#define SK_PLATFORM_AUDIO_H

#include "../audio/mixer.h"
#include "../base/strv.h"

/* Frames a pull: about 21 ms at 48 kHz. */
#define SK_AUDIO_PERIOD 1024u
/* The most a writer gets ahead of the clock. */
#define SK_AUDIO_LEAD_MS 100u
/* The silence after which an output stops. */
#define SK_AUDIO_IDLE_MS 3000u

typedef struct sk_audio sk_audio;

typedef struct {
    /* Each block pulled, on the output's thread; NULL drops them. */
    void (*record)(void *ud, const int16_t *frames, size_t n);
    void *ud;
    /* Pulls as fast as record takes the blocks, not in real time. */
    bool unpaced;
    /* The silence before it stops; 0 for SK_AUDIO_IDLE_MS. */
    unsigned idle_ms;
} sk_audio_null;

/* The system's output for m, at m's rate, interleaved 16-bit stereo.
 * Never NULL: without a device the sounds are silent. m outlives it. */
sk_audio *sk_audio_open(sk_mixer *m);
/* The null sink for m; opts NULL drops every block, in real time. */
sk_audio *sk_audio_open_null(sk_mixer *m, const sk_audio_null *opts);
/* Stops the output at once, cutting off what it holds. */
void sk_audio_close(sk_audio *a);
/* "AudioQueue", the first player found, "null" or "silent". */
const char *sk_audio_name(const sk_audio *a);
/* True from a start until the stop after a silence. */
bool sk_audio_running(const sk_audio *a);
/* The frames pulled from the mixer so far. */
uint64_t sk_audio_pulled(const sk_audio *a);

/* ---- What each system's audio.c gives the output ---- */

typedef struct {
    const char *name;
    void *dev;
    /* Starts the device; false when it cannot. */
    bool (*start)(void *dev);
    /* Hands it n frames, waiting as a device does; false when it died
     * or the output is closing. NULL for a device that pulls with
     * sk_audio_pull on a thread of its own. */
    bool (*write)(void *dev, const int16_t *frames, size_t n);
    /* Stops it after a silence, or at once when the output is closing. */
    void (*stop)(void *dev);
    void (*free)(void *dev);
} sk_audio_device;

/* The system's device for a, at rate; false, with a log line, when there
 * is none. sk_audio_open calls it once. */
bool sk_audio_device_open(sk_audio *a, uint32_t rate, sk_audio_device *out);
/* For a device that pulls on its own thread: the mixer's next frames. */
bool sk_audio_pull(sk_audio *a, int16_t *frames, size_t n);
/* True once sk_audio_close has begun, when a device's waits give up. */
bool sk_audio_closing(const sk_audio *a);

/* ---- Linux's players, here so the tests run on every system ---- */

typedef enum { SK_AUDIO_PW_PLAY, SK_AUDIO_PAPLAY, SK_AUDIO_APLAY, SK_AUDIO_PLAYERS } sk_audio_player;

/* "pw-play", "paplay" or "aplay". */
const char *sk_audio_player_name(sk_audio_player p);
/* The command line that plays raw interleaved signed 16-bit PCM from
 * stdin, path as argv[0], in the byte order big_endian says. */
void sk_audio_player_argv(sk_strv *out, sk_audio_player p, const char *path, uint32_t rate,
                          unsigned channels, bool big_endian);
/* The players in search_path, a PATH value, in the order of
 * sk_audio_player, each with the first executable file of its name in
 * the path's absolute directories: found[i] the player, paths the file.
 * Returns how many. */
size_t sk_audio_player_find(const char *search_path, sk_audio_player found[SK_AUDIO_PLAYERS],
                            sk_strv *paths);

#endif
