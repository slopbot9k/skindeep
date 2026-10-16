/* IMA ADPCM both ways. The first two bytes pick a block size from 5 to
 * 1025 and a sample count; the rest are blocks to decode, and, read as
 * 16-bit samples, a sound to encode. What must hold: the check and the
 * decoder agree, a block decodes alone as it does among the others,
 * and an encoded sound is valid, decodes, and keeps each block's first
 * sample exactly. */
#include "../../src/audio/adpcm.h"
#include "fuzz.h"

#include <stdlib.h>
#include <string.h>

const char *const sk_fuzz_dict[] = {"\x00\x00\x00\x00", "\x58\x00", "\x59\x00", "\xff\x7f", "\x00\x80",
                                    "\x77\x77", "\x88\x88", "\x0f\xf0", NULL};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 2)
        return 0;
    unsigned block = 5 + (unsigned)data[0] * 4;
    size_t n = size - 2;
    uint8_t *bytes = malloc(n ? n : 1);
    if (!bytes)
        abort();
    if (n)
        memcpy(bytes, data + 2, n);
    size_t per = sk_adpcm_block_samples(block);
    /* Decoding: the frames the blocks hold, less up to data[1]. */
    size_t blocks = n / block, frames = blocks * per;
    frames = frames > data[1] ? frames - data[1] : frames;
    int16_t *out = malloc((frames ? frames : 1) * sizeof *out);
    int16_t *one = malloc(per * sizeof *one);
    if (!out || !one)
        abort();
    bool valid = sk_adpcm_valid(bytes, blocks * block, block, frames);
    if (valid != sk_adpcm_decode(bytes, blocks * block, block, frames, out))
        abort();
    for (size_t b = 0; valid && b < blocks; b++) {
        if (!sk_adpcm_decode_block(bytes + b * block, block, one))
            abort();
        size_t from = b * per, k = frames - from < per ? frames - from : per;
        if (memcmp(one, out + from, k * sizeof *one) != 0)
            abort();
    }
    free(out);
    /* Encoding: the bytes as samples. */
    size_t count = n / 2;
    int16_t *pcm = malloc((count ? count : 1) * sizeof *pcm);
    if (!pcm)
        abort();
    for (size_t i = 0; i < count; i++)
        pcm[i] = (int16_t)(uint16_t)(bytes[2 * i] | bytes[2 * i + 1] << 8);
    size_t len = 0;
    uint8_t *enc = sk_adpcm_encode(pcm, count, block, &len);
    if (count) {
        int16_t *back = malloc(count * sizeof *back);
        if (!enc || !back || len != sk_adpcm_size(count, block) || !sk_adpcm_valid(enc, len, block, count) ||
            !sk_adpcm_decode(enc, len, block, count, back))
            abort();
        for (size_t i = 0; i < count; i += per)
            if (back[i] != pcm[i])
                abort();
        free(back);
    } else if (enc) {
        abort();
    }
    free(enc);
    free(pcm);
    free(one);
    free(bytes);
    return 0;
}
