/* A window's skin sounds (decision 0024), as app/renderer/sounds.js
 * plays them: the current skin's sounds and nothing else, at half the
 * manifest's volume. A loop plays without end. A random sound plays once
 * after a wait drawn evenly from its interval, its first play waiting
 * too, then waits again once it has ended. A new skin, the same skin
 * again or a mute stops every sound and every wait; unmuted, the skin's
 * sounds start over. A player starts muted, so nothing sounds before the
 * app has said whether the window is muted.
 *
 * The player lives on the control side of its mixer (audio/mixer.h) and
 * keeps time with a clock it is given. It does nothing on its own: each
 * call returns how long until it next has something to do, and the
 * caller calls sk_sounds_tick then. Several players, one a window, may
 * share a mixer; each stops only its own voices. */
#ifndef SK_SOUNDS_H
#define SK_SOUNDS_H

#include "mixer.h"

/* A skin's sounds at most, as the manifest allows. */
#define SK_SOUNDS_MAX 16u
/* The app's cap: a sound at full volume plays at half scale. */
#define SK_SOUNDS_MASTER 0.5f
/* The manifest's volume when it gives none. */
#define SK_SOUNDS_DEFAULT_VOLUME 0.3f
/* How often a random sound past its length is asked whether it ended,
 * since the output may run a little behind the clock. */
#define SK_SOUNDS_POLL_MS 50

/* One sound of the manifest, decoded. */
typedef struct {
    sk_pcm *pcm;       /* the player takes its own reference */
    bool loop;         /* played without end; else random */
    float volume;      /* the manifest's, 0 to 1 */
    float interval[2]; /* random: the wait's least and most, in seconds */
} sk_sounds_entry;

/* Milliseconds on a clock that never goes back. */
typedef uint64_t (*sk_clock_fn)(void *ud);

typedef struct sk_sounds sk_sounds;

/* clock NULL keeps time with CLOCK_MONOTONIC. seed starts the waits'
 * generator; the same seed draws the same waits. */
sk_sounds *sk_sounds_new(sk_mixer *m, sk_clock_fn clock, void *clock_ud, uint64_t seed);
/* Stops its sounds. */
void sk_sounds_free(sk_sounds *s);

/* The return of the calls below: milliseconds until sk_sounds_tick has
 * something to do, or -1 when nothing waits on the clock. */

/* A skin's sounds, the first SK_SOUNDS_MAX of them, in place of the
 * last skin's; the same skin again restarts them. An entry without a
 * sound keeps its place and stays silent. */
long sk_sounds_set(sk_sounds *s, const sk_sounds_entry *e, size_t n);
long sk_sounds_mute(sk_sounds *s, bool muted);
bool sk_sounds_muted(const sk_sounds *s);
/* Plays the random sounds whose wait is over, and starts the wait of
 * those that ended. */
long sk_sounds_tick(sk_sounds *s);

/* What one sound is doing, for the page's probe and the tests. */
typedef struct {
    bool loop;
    bool playing;  /* a voice of it sounds */
    bool waiting;  /* a random sound's wait runs */
    uint64_t due;  /* waiting: when it plays, on the clock */
    unsigned plays;
    float volume;  /* what it plays at: half the manifest's */
    sk_voice voice;
} sk_sounds_state;

size_t sk_sounds_count(const sk_sounds *s);
/* i below sk_sounds_count. */
sk_sounds_state sk_sounds_get(sk_sounds *s, size_t i);

#endif
