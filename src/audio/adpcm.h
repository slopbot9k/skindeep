/* IMA ADPCM, mono, in the blocks of WAVE's format 0x11: how a skin
 * pack keeps its sounds (decision 0037's SNDS). skinc encodes; the
 * pack loader checks and the player decodes, a block at a time.
 *
 * A block of b bytes is a 4-byte header, the first sample as a
 * little-endian int16, the step index (0 to 88) and a zero byte, then
 * b - 4 bytes of 4-bit codes, the low nibble first: 1 + 2 (b - 4)
 * samples in all. Every block starts over from its header, so the
 * player can begin at any of them. The last block of a sound is whole,
 * its samples past the sound's end the last sample held. */
#ifndef SK_AUDIO_ADPCM_H
#define SK_AUDIO_ADPCM_H

#include "../base/base.h"

#define SK_ADPCM_MIN_BLOCK 5u
#define SK_ADPCM_MAX_BLOCK 65535u
/* What skinc writes: 2041 samples a block, 46 ms at 44.1 kHz. */
#define SK_ADPCM_BLOCK 1024u

/* The samples a block of block_bytes holds; 0 for a size out of range. */
size_t sk_adpcm_block_samples(unsigned block_bytes);
/* The bytes of the blocks that cover frames samples; 0 for no frames,
 * a block size out of range, or a size past SIZE_MAX. */
size_t sk_adpcm_size(size_t frames, unsigned block_bytes);

/* frames samples of 16-bit PCM as blocks, malloc'd, their size
 * (sk_adpcm_size) in *n; NULL for no frames or a block size out of
 * range. */
uint8_t *sk_adpcm_encode(const int16_t *pcm, size_t frames, unsigned block_bytes, size_t *n);

/* Whether a block's header is one the encoder writes: a step index of
 * at most 88 and the zero byte. */
bool sk_adpcm_block_ok(const uint8_t *block);
/* The block's sk_adpcm_block_samples(block_bytes) samples into out;
 * false, writing nothing, when its header is not ok. */
bool sk_adpcm_decode_block(const uint8_t *block, unsigned block_bytes, int16_t *out);
/* Whether n bytes are the blocks of frames samples: n is
 * sk_adpcm_size(frames, block_bytes) and every header is ok. The pack
 * loader's check. */
bool sk_adpcm_valid(const uint8_t *data, size_t n, unsigned block_bytes, size_t frames);
/* The frames samples of n bytes of blocks into out; false, with out
 * untouched, unless sk_adpcm_valid. */
bool sk_adpcm_decode(const uint8_t *data, size_t n, unsigned block_bytes, size_t frames, int16_t *out);

#endif
