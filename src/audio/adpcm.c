#include "adpcm.h"

/* The IMA's tables (Recommended Practices for Enhancing Digital Audio
 * Compatibility in Multimedia Systems, 1992): the 89 quantizer steps,
 * each about 1.1 times the last, and how far each code moves the step
 * index, the sign bit aside. */
static const int16_t STEPS[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};
static const int8_t MOVES[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

#define MAX_INDEX 88

/* What the decoder keeps between codes. */
typedef struct {
    int pred, index;
} state;

/* One code applied: the next sample. The encoder runs the same, so its
 * prediction never drifts from the decoder's. */
static int16_t step(state *s, unsigned code)
{
    int st = STEPS[s->index], diff = st >> 3;
    if (code & 4)
        diff += st;
    if (code & 2)
        diff += st >> 1;
    if (code & 1)
        diff += st >> 2;
    s->pred += code & 8 ? -diff : diff;
    s->pred = s->pred < -32768 ? -32768 : s->pred > 32767 ? 32767 : s->pred;
    s->index += MOVES[code & 7];
    s->index = s->index < 0 ? 0 : s->index > MAX_INDEX ? MAX_INDEX : s->index;
    return (int16_t)s->pred;
}

/* The code for sample x: the bits of the difference over the step,
 * from the largest, as the IMA's encoder takes them. */
static unsigned code_for(const state *s, int x)
{
    int st = STEPS[s->index], diff = x - s->pred;
    unsigned code = 0;
    if (diff < 0) {
        code = 8;
        diff = -diff;
    }
    if (diff >= st) {
        code |= 4;
        diff -= st;
    }
    st >>= 1;
    if (diff >= st) {
        code |= 2;
        diff -= st;
    }
    st >>= 1;
    if (diff >= st)
        code |= 1;
    return code;
}

size_t sk_adpcm_block_samples(unsigned block_bytes)
{
    if (block_bytes < SK_ADPCM_MIN_BLOCK || block_bytes > SK_ADPCM_MAX_BLOCK)
        return 0;
    return 1 + 2 * (size_t)(block_bytes - 4);
}

size_t sk_adpcm_size(size_t frames, unsigned block_bytes)
{
    size_t per = sk_adpcm_block_samples(block_bytes);
    if (!per || !frames)
        return 0;
    size_t blocks = frames / per + (frames % per != 0);
    if (blocks > SIZE_MAX / block_bytes)
        return 0;
    return blocks * block_bytes;
}

uint8_t *sk_adpcm_encode(const int16_t *pcm, size_t frames, unsigned block_bytes, size_t *n)
{
    size_t size = sk_adpcm_size(frames, block_bytes), per = sk_adpcm_block_samples(block_bytes);
    if (!size)
        return NULL;
    uint8_t *out = sk_xmalloc(size);
    state s = {0, 0};
    size_t at = 0;
    for (uint8_t *b = out; b < out + size; b += block_bytes) {
        /* Past the end, the last sample held: the padding is silence. */
#define SAMPLE(i) ((i) < frames ? pcm[i] : pcm[frames - 1])
        int16_t first = SAMPLE(at);
        s.pred = first;
        b[0] = (uint8_t)((uint16_t)first & 0xff);
        b[1] = (uint8_t)((uint16_t)first >> 8);
        b[2] = (uint8_t)s.index;
        b[3] = 0;
        for (size_t k = 1; k < per; k++) {
            unsigned code = code_for(&s, SAMPLE(at + k));
            step(&s, code);
            uint8_t *byte = b + 4 + (k - 1) / 2;
            if (k & 1)
                *byte = (uint8_t)code;
            else
                *byte = (uint8_t)(*byte | code << 4);
        }
#undef SAMPLE
        at += per;
    }
    *n = size;
    return out;
}

bool sk_adpcm_block_ok(const uint8_t *block)
{
    return block[2] <= MAX_INDEX && block[3] == 0;
}

/* The first count samples of a block whose header is ok. */
static void decode(const uint8_t *b, size_t count, int16_t *out)
{
    int first = b[0] | b[1] << 8;
    state s = {first < 32768 ? first : first - 65536, b[2]};
    out[0] = (int16_t)s.pred;
    for (size_t k = 1; k < count; k++) {
        uint8_t byte = b[4 + (k - 1) / 2];
        out[k] = step(&s, k & 1 ? byte & 15u : (unsigned)byte >> 4);
    }
}

bool sk_adpcm_decode_block(const uint8_t *block, unsigned block_bytes, int16_t *out)
{
    size_t per = sk_adpcm_block_samples(block_bytes);
    if (!per || !sk_adpcm_block_ok(block))
        return false;
    decode(block, per, out);
    return true;
}

bool sk_adpcm_valid(const uint8_t *data, size_t n, unsigned block_bytes, size_t frames)
{
    if (!frames || n != sk_adpcm_size(frames, block_bytes))
        return false;
    for (size_t at = 0; at < n; at += block_bytes)
        if (!sk_adpcm_block_ok(data + at))
            return false;
    return true;
}

bool sk_adpcm_decode(const uint8_t *data, size_t n, unsigned block_bytes, size_t frames, int16_t *out)
{
    if (!sk_adpcm_valid(data, n, block_bytes, frames))
        return false;
    size_t per = sk_adpcm_block_samples(block_bytes);
    for (size_t at = 0, done = 0; at < n; at += block_bytes, done += per)
        decode(data + at, SK_MIN(per, frames - done), out + done);
    return true;
}
