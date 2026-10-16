/* Sounds through skinc's decoder: any bytes, an Ogg Vorbis stream for
 * stb_vorbis or a WAV, by their first bytes. The input is copied to a
 * buffer of its own size. What must hold: a sound that comes back has
 * frames, at most the length asked for, at a rate of the limits, and
 * goes through the pack's ADPCM and back. */
#include "../../skinc/media.h"
#include "../../src/audio/adpcm.h"
#include "fuzz.h"

#include <stdlib.h>
#include <string.h>

const char *const sk_fuzz_dict[] = {
    "OggS", "\x01vorbis", "\x03vorbis", "\x05vorbis", "BCV", "\x00\x02", "\x44\xac\x00\x00", "\x80\xbb\x00\x00",
    "RIFF", "WAVE", "fmt ", "data", "\x01\x00\x01\x00", "\x01\x00\x02\x00", "\x03\x00", "\xfe\xff", "\x10\x00",
    "\x18\x00", "\x20\x00", NULL,
};

#define MAX_SECONDS 10.0

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint8_t *exact = malloc(size ? size : 1);
    if (!exact)
        abort();
    if (size)
        memcpy(exact, data, size);
    sk_sound s;
    if (sk_media_sound(exact, size, MAX_SECONDS, &s, NULL)) {
        if (!s.frames || !s.pcm || s.rate < SK_MEDIA_MIN_RATE || s.rate > SK_MEDIA_MAX_RATE ||
            (double)s.frames > MAX_SECONDS * s.rate)
            abort();
        size_t n = 0;
        uint8_t *blocks = sk_media_adpcm(&s, SK_ADPCM_BLOCK, &n);
        int16_t *back = malloc(s.frames * sizeof *back);
        if (!blocks || !back || !sk_adpcm_decode(blocks, n, SK_ADPCM_BLOCK, s.frames, back) || back[0] != s.pcm[0])
            abort();
        free(back);
        free(blocks);
        sk_sound_free(&s);
    } else if (s.pcm || s.frames) {
        abort();
    }
    free(exact);
    return 0;
}
