/* The skin sounds' mixer (decisions 0024, 0036). A voice plays one
 * sound, 16-bit mono PCM at any rate from 8 to 48 kHz, once or in a
 * loop, at its own volume. The mixer resamples each voice to the output
 * rate with 4-point cubic (Catmull-Rom) interpolation, sums them and
 * writes interleaved 16-bit stereo, the mono sum on both channels,
 * through a soft clipper that leaves the sum alone below 3/4 of full
 * scale and bends it towards full scale above, so no number of voices
 * wraps. A voice fades in over 2 ms and out over 10 ms when stopped, so
 * neither clicks.
 *
 * Two sides meet here. The control side (the UI or core thread) makes
 * sounds and starts and stops voices; its calls take a mutex that only
 * the control side takes. The output side, AudioQueue's callback thread
 * or the writer thread (platform/audio.h), calls sk_mixer_pull, which
 * takes no lock, allocates and frees nothing and never waits. They share
 * a fixed array of voice slots, and each slot's atomic state says which
 * side owns its fields: a free slot is the control side's to fill; the
 * release store that makes it playing hands it to the output side; the
 * release store that marks it done hands it back, and the control side
 * then drops its sound. A stop is one atomic flag the output side reads.
 * So no field is ever written by one side while the other reads it. */
#ifndef SK_MIXER_H
#define SK_MIXER_H

#include "../base/base.h"

/* The output format the platforms ask for. */
#define SK_MIXER_RATE 48000u
#define SK_MIXER_CHANNELS 2u
/* Voices at once: twice decision 0024's 16 sounds a skin, so a new
 * skin's sounds start while the old ones fade. */
#define SK_MIXER_VOICES 32u
/* The rates a sound may have, as packs store them (decision 0037). */
#define SK_PCM_MIN_RATE 8000u
#define SK_PCM_MAX_RATE 48000u

/* A decoded sound: mono 16-bit frames at a rate. It is shared by
 * reference count, so a sound freed while a voice plays it lasts until
 * the voice is done. Make and drop references on the control side. */
typedef struct sk_pcm sk_pcm;

/* A copy of n frames. NULL when n is 0 or above 2^31, or the rate is
 * outside SK_PCM_MIN_RATE to SK_PCM_MAX_RATE. */
sk_pcm *sk_pcm_new(const int16_t *frames, size_t n, uint32_t rate);
/* As sk_pcm_new, taking frames, from malloc, as its own; frees them when
 * it returns NULL. */
sk_pcm *sk_pcm_adopt(int16_t *frames, size_t n, uint32_t rate);
sk_pcm *sk_pcm_ref(sk_pcm *p);
/* Drops a reference; NULL does nothing. */
void sk_pcm_unref(sk_pcm *p);
size_t sk_pcm_frames(const sk_pcm *p);
uint32_t sk_pcm_rate(const sk_pcm *p);
/* Its length in whole milliseconds, rounded up. */
uint64_t sk_pcm_ms(const sk_pcm *p);

typedef struct sk_mixer sk_mixer;
/* One play of a sound; 0 is none. */
typedef uint64_t sk_voice;

/* A mixer writing rate frames a second, 8000 to 192000. */
sk_mixer *sk_mixer_new(uint32_t rate);
/* Once no output pulls from it any more. */
void sk_mixer_free(sk_mixer *m);
uint32_t sk_mixer_rate(const sk_mixer *m);

/* Plays p at volume, 0 to 1, from its start; a loop plays until it is
 * stopped. The voice holds a reference to p. 0 when every slot is busy.
 * The voice starts at the output's next pull. */
sk_voice sk_mixer_play(sk_mixer *m, sk_pcm *p, float volume, bool loop);
/* Fades the voice out; nothing once it has ended. */
void sk_mixer_stop(sk_mixer *m, sk_voice v);
void sk_mixer_stop_all(sk_mixer *m);
/* True while the voice sounds or waits for the output to start it, and
 * neither ended nor was stopped. */
bool sk_mixer_playing(sk_mixer *m, sk_voice v);
/* The voices the output has yet to finish, stopping ones included. Any
 * thread. */
unsigned sk_mixer_live(const sk_mixer *m);

/* fn(ud) runs on the control side, with the mixer's mutex held, each
 * time a voice starts, so an idle output can wake; it must not call the
 * mixer. NULL removes it. */
void sk_mixer_on_wake(sk_mixer *m, void (*fn)(void *ud), void *ud);

/* The output side: frames frames of interleaved stereo into out. True
 * when a voice sounded in them; silence otherwise. One output pulls at a
 * time: a second pull that meets a first gets silence. */
bool sk_mixer_pull(sk_mixer *m, int16_t *out, size_t frames);

#endif
