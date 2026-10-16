/* IMA ADPCM against vectors worked by hand from the IMA's step and
 * index tables, and a sine through the encoder and back. */
#include "../../src/audio/adpcm.h"
#include "../test.h"

#include <math.h>

/* Header 0 at step index 0 (step 7), then the codes 7 7 7 7 15 0 8 4,
 * low nibble first. Each code adds or takes away step/8, plus step for
 * bit 4, step/2 for bit 2 and step/4 for bit 1, shifts rounding down:
 *   7: 0 + 7 + 3 + 1 = 11        -> 11, index 0 + 8 = 8 (step 16)
 *   7: 2 + 16 + 8 + 4 = 30       -> 41, index 16 (step 34)
 *   7: 4 + 34 + 17 + 8 = 63      -> 104, index 24 (step 73)
 *   7: 9 + 73 + 36 + 18 = 136    -> 240, index 32 (step 157)
 *  15: -(19 + 157 + 78 + 39)     -> -53, index 40 (step 337)
 *   0: 337 / 8 = 42              -> -11, index 39 (step 307)
 *   8: -(307 / 8) = -38          -> -49, index 38 (step 279)
 *   4: 34 + 279 = 313            -> 264, index 40 */
static const uint8_t BLOCK[8] = {0x00, 0x00, 0x00, 0x00, 0x77, 0x77, 0x0F, 0x48};
static const int16_t SAMPLES[9] = {0, 11, 41, 104, 240, -53, -11, -49, 264};

static void test_sizes(void)
{
    CHECK_INT(sk_adpcm_block_samples(4), 0);
    CHECK_INT(sk_adpcm_block_samples(5), 3);
    CHECK_INT(sk_adpcm_block_samples(8), 9);
    CHECK_INT(sk_adpcm_block_samples(SK_ADPCM_BLOCK), 2041);
    CHECK_INT(sk_adpcm_block_samples(65535), 131063);
    CHECK_INT(sk_adpcm_block_samples(65536), 0);
    CHECK_INT(sk_adpcm_size(0, 8), 0);
    CHECK_INT(sk_adpcm_size(1, 8), 8);
    CHECK_INT(sk_adpcm_size(9, 8), 8);
    CHECK_INT(sk_adpcm_size(10, 8), 16);
    CHECK_INT(sk_adpcm_size(18, 8), 16);
    CHECK_INT(sk_adpcm_size(19, 8), 24);
    CHECK_INT(sk_adpcm_size(10, 3), 0);
    CHECK_INT(sk_adpcm_size(SIZE_MAX, 5), 0);
}

static void test_decode_vector(void)
{
    int16_t out[9];
    CHECK(sk_adpcm_decode_block(BLOCK, 8, out));
    for (int i = 0; i < 9; i++)
        CHECK_INT(out[i], SAMPLES[i]);
    /* Part of the block, as a sound's last. */
    int16_t part[5] = {0};
    CHECK(sk_adpcm_decode(BLOCK, 8, 8, 5, part));
    for (int i = 0; i < 5; i++)
        CHECK_INT(part[i], SAMPLES[i]);
}

/* The encoder picks the same codes: its difference over the step, bit
 * by bit from 4, is the code that decodes nearest. */
static void test_encode_vector(void)
{
    size_t n = 0;
    uint8_t *b = sk_adpcm_encode(SAMPLES, 9, 8, &n);
    CHECK_INT(n, 8);
    CHECK(b && memcmp(b, BLOCK, 8) == 0);
    free(b);
}

/* At the top of the scale: header 32000 at index 88 (step 32767), codes
 * 7, 15, 15, 0. A step of 61436 clamps to 32767, then falls to -28669,
 * clamps to -32768, and code 0 adds 32767 / 8 = 4095. Index 88 stays
 * 88 under +8 and goes to 87 under -1. */
static void test_clamps(void)
{
    const uint8_t block[6] = {0x00, 0x7D, 88, 0, 0xF7, 0x0F};
    const int16_t want[5] = {32000, 32767, -28669, -32768, -28673};
    int16_t out[5];
    CHECK(sk_adpcm_decode_block(block, 6, out));
    for (int i = 0; i < 5; i++)
        CHECK_INT(out[i], want[i]);
    /* A negative first sample. */
    const uint8_t neg[5] = {0x00, 0x80, 0, 0, 0x00};
    int16_t three[3];
    CHECK(sk_adpcm_decode_block(neg, 5, three));
    CHECK_INT(three[0], -32768);
    CHECK_INT(three[1], -32768 + 0);
    CHECK_INT(three[2], -32768 + 0);
}

/* Blocks start over from their headers: each holds its first sample
 * exactly and carries the step index on; the last is padded with the
 * last sample. */
static void test_blocks(void)
{
    int16_t pcm[21];
    for (int i = 0; i < 21; i++)
        pcm[i] = (int16_t)(i * 300 - 3000);
    size_t n = 0;
    uint8_t *b = sk_adpcm_encode(pcm, 21, 8, &n);
    CHECK_INT(n, 24);
    CHECK(b != NULL);
    if (!b)
        return;
    CHECK(sk_adpcm_valid(b, n, 8, 21));
    CHECK_INT((int16_t)(b[8] | b[9] << 8), pcm[9]);
    CHECK_INT((int16_t)(b[16] | b[17] << 8), pcm[18]);
    CHECK(b[10] > 0); /* the index the first block reached */
    int16_t out[21];
    CHECK(sk_adpcm_decode(b, n, 8, 21, out));
    CHECK_INT(out[0], pcm[0]);
    CHECK_INT(out[9], pcm[9]);
    CHECK_INT(out[18], pcm[18]);
    free(b);
}

static void test_refusals(void)
{
    uint8_t b[8];
    memcpy(b, BLOCK, 8);
    int16_t out[9] = {0};
    b[2] = 89; /* past the step table */
    CHECK(!sk_adpcm_block_ok(b));
    CHECK(!sk_adpcm_decode_block(b, 8, out));
    CHECK_INT(out[0], 0);
    b[2] = 88;
    b[3] = 1; /* the zero byte */
    CHECK(!sk_adpcm_decode_block(b, 8, out));
    b[3] = 0;
    CHECK(sk_adpcm_decode_block(b, 8, out));
    CHECK(!sk_adpcm_decode_block(b, 4, out));
    /* The size must be the blocks of the frames, exactly. */
    CHECK(!sk_adpcm_valid(BLOCK, 8, 8, 10));
    CHECK(!sk_adpcm_valid(BLOCK, 7, 8, 9));
    CHECK(!sk_adpcm_valid(BLOCK, 8, 8, 0));
    CHECK(sk_adpcm_valid(BLOCK, 8, 8, 1));
    CHECK(!sk_adpcm_decode(BLOCK, 8, 8, 10, out));
    size_t n = 1;
    CHECK(sk_adpcm_encode(SAMPLES, 0, 8, &n) == NULL);
    CHECK(sk_adpcm_encode(SAMPLES, 9, 4, &n) == NULL);
    CHECK_INT(n, 1);
}

/* Two seconds of 440 Hz at 44.1 kHz and half scale, in skinc's blocks:
 * the error stays a small part of the signal, at least 40 dB under it
 * and never past 200 once the step has grown. */
static void test_sine(void)
{
    enum { RATE = 44100, N = 2 * RATE };
    int16_t *pcm = sk_xmalloc(N * sizeof *pcm), *out = sk_xmalloc(N * sizeof *out);
    for (int i = 0; i < N; i++)
        pcm[i] = (int16_t)lround(16000.0 * sin(2 * M_PI * 440.0 * i / RATE));
    size_t n = 0;
    uint8_t *b = sk_adpcm_encode(pcm, N, SK_ADPCM_BLOCK, &n);
    CHECK_INT(n, sk_adpcm_size(N, SK_ADPCM_BLOCK));
    CHECK(n <= N / 2 + SK_ADPCM_BLOCK); /* four bits a sample */
    CHECK(b && sk_adpcm_decode(b, n, SK_ADPCM_BLOCK, N, out));
    double sq = 0, sig = 0;
    int worst = 0;
    for (int i = 0; i < N; i++) {
        int e = abs(out[i] - pcm[i]);
        /* The first samples, while the step grows from 7, aside. */
        if (i >= 64)
            worst = SK_MAX(worst, e);
        sq += (double)e * e;
        sig += (double)pcm[i] * pcm[i];
    }
    double snr = 10 * log10(sig / sq);
    /* 44.7 dB and 152 as written. */
    if (snr < 40 || worst > 200)
        sk_test_fail(__FILE__, __LINE__, "SNR %.1f dB, worst error %d", snr, worst);
    free(b);
    free(pcm);
    free(out);
}

const sk_test sk_suite_adpcm[] = {
    T(test_sizes), T(test_decode_vector), T(test_encode_vector), T(test_clamps), T(test_blocks),
    T(test_refusals), T(test_sine), {0},
};
