/* A frame's samples as butterchurn 2.6.7 hears them (milk.h): its
 * AudioProcessor, FFT and AudioLevels, ported from its JavaScript
 * (MIT, Copyright (c) 2013-2018 Jordan Berg; third_party/butterchurn/
 * LICENSE). The spectrum is worked in double where butterchurn rounds
 * to float32, so gcc and clang agree to the last bit or nearly. */
#include "milk.h"

#include <math.h>
#include <pthread.h>
#include <string.h>

#define NFREQ SK_MILK_SAMPLES
#define BINS SK_MILK_WAVE

/* ---- The FFT ---- */

/* Butterchurn's FFT(1024, 512, equalize): a radix-2 transform of the
 * 1024 signed samples, its first 512 magnitudes each weighted by
 * -0.02 ln((512 - k) / 512), which is 0 for the constant term and rises
 * toward the top bins. The tables are made once, by whichever pane's
 * thread comes first; they hold only what the sizes give. */
static struct {
    uint16_t bitrev[NFREQ];
    double cs[2][16];
    double equalize[BINS];
} fft;
static pthread_once_t fft_once = PTHREAD_ONCE_INIT;

static void fft_tables(void)
{
    for (int i = 0; i < NFREQ; i++)
        fft.bitrev[i] = (uint16_t)i;
    for (int i = 0, j = 0; i < NFREQ; i++) {
        if (j > i) {
            uint16_t t = fft.bitrev[i];
            fft.bitrev[i] = fft.bitrev[j];
            fft.bitrev[j] = t;
        }
        int m = NFREQ >> 1;
        while (m >= 1 && j >= m) {
            j -= m;
            m >>= 1;
        }
        j += m;
    }
    int t = 0;
    for (int size = 2; size <= NFREQ; size <<= 1, t++) {
        double theta = -2.0 * M_PI / size;
        fft.cs[0][t] = cos(theta);
        fft.cs[1][t] = sin(theta);
    }
    for (int i = 0; i < BINS; i++)
        fft.equalize[i] = -0.02 * log((double)(BINS - i) / BINS);
}

static void fft_init(void)
{
    pthread_once(&fft_once, fft_tables);
}

static void spectrum(const int8_t *in, double *out)
{
    double re[NFREQ], im[NFREQ];
    for (int i = 0; i < NFREQ; i++) {
        re[i] = in[fft.bitrev[i]];
        im[i] = 0;
    }
    int t = 0;
    for (int size = 2; size <= NFREQ; size <<= 1, t++) {
        double wpr = fft.cs[0][t], wpi = fft.cs[1][t], wr = 1, wi = 0;
        int half = size >> 1;
        for (int m = 0; m < half; m++) {
            for (int i = m; i < NFREQ; i += size) {
                int j = i + half;
                double tr = wr * re[j] - wi * im[j];
                double ti = wr * im[j] + wi * re[j];
                re[j] = re[i] - tr;
                im[j] = im[i] - ti;
                re[i] += tr;
                im[i] += ti;
            }
            double w = wr;
            wr = w * wpr - wi * wpi;
            wi = wi * wpr + w * wpi;
        }
    }
    for (int i = 0; i < BINS; i++)
        out[i] = fft.equalize[i] * sqrt(re[i] * re[i] + im[i] * im[i]);
}

/* ---- Levels ---- */

/* JavaScript's Math.round: halves go up. */
static double js_round(double x)
{
    return floor(x + 0.5);
}

/* The bands' bins, [start, stop): 20 Hz to 320 Hz, to 2800 Hz, to
 * 11025 Hz, each rounded to a bin of 44100 / 1024 Hz less one, as
 * AudioLevels has it. That is 0-6, 6-64 and 64-255. */
static int band_edge(double hz)
{
    double bucket = 44100.0 / NFREQ;
    double k = js_round(hz / bucket) - 1;
    return (int)(k < 0 ? 0 : k > BINS - 1 ? BINS - 1 : k);
}

void sk_milk_audio_init(sk_milk_audio *a)
{
    memset(a, 0, sizeof *a);
    for (int i = 0; i < 3; i++)
        a->avg[i] = a->long_avg[i] = 1;
    a->bass_att = a->mid_att = a->treb_att = 1;
    fft_init();
}

void sk_milk_audio_update(sk_milk_audio *a, const float *samples, double fps, int frame)
{
    fft_init();
    /* The bytes synth.js hands butterchurn, then AudioProcessor's
     * signed copy and its half-rate waves. Int8Array truncates the
     * average toward zero. */
    int8_t prev = 0;
    for (int i = 0; i < NFREQ; i++) {
        double s = samples[i], b = js_round(128 + 127 * s);
        b = !(b == b) ? 0 : b < 0 ? 0 : b > 255 ? 255 : b;
        int8_t v = (int8_t)((int)b - 128);
        a->time[i] = v;
        if (i == 0)
            prev = v;
        if (i % 2 == 0)
            a->wave_l[i / 2] = a->wave_r[i / 2] = (int8_t)(int)(0.5 * (v + prev));
        prev = v;
    }
    spectrum(a->time, a->spectrum);

    /* AudioLevels: the rate of each average set for 30 fps and adjusted
     * to the renderer's, which is held to 15..144. */
    double efps = fps;
    if (!isfinite(efps) || efps < 15)
        efps = 15;
    else if (efps > 144)
        efps = 144;
    const int start[3] = {band_edge(20), band_edge(320), band_edge(2800)};
    const int stop[3] = {band_edge(320), band_edge(2800), band_edge(11025)};
    double val[3], att[3];
    for (int b = 0; b < 3; b++) {
        a->imm[b] = 0;
        for (int k = start[b]; k < stop[b]; k++)
            a->imm[b] += a->spectrum[k];
        double rate = pow(a->imm[b] > a->avg[b] ? 0.2 : 0.5, 30.0 / efps);
        a->avg[b] = a->avg[b] * rate + a->imm[b] * (1 - rate);
        rate = pow(frame < 50 ? 0.9 : 0.992, 30.0 / efps);
        a->long_avg[b] = a->long_avg[b] * rate + a->imm[b] * (1 - rate);
        if (a->long_avg[b] < 0.001) {
            val[b] = att[b] = 1;
        } else {
            val[b] = a->imm[b] / a->long_avg[b];
            att[b] = a->avg[b] / a->long_avg[b];
        }
    }
    a->bass = val[0], a->mid = val[1], a->treb = val[2];
    a->bass_att = att[0], a->mid_att = att[1], a->treb_att = att[2];
}

/* ---- The test signal ----
 *
 * Made in integers alone, so every machine makes the same samples to
 * the bit: a byte of difference would move a wave by a pixel, which the
 * feedback would carry on. */

/* splitmix64's finalizer: a hash of the sample's place. */
static uint64_t mix64(uint64_t x)
{
    x += 0x9e3779b97f4a7c15u;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9u;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebu;
    return x ^ (x >> 31);
}

/* A sine near enough, of 65536ths of a turn, -32767..32767: Bhaskara
 * I's 16 x (1 - x) / (5 - 4 x (1 - x)) over each half turn. */
static int32_t isin(uint32_t phase)
{
    int64_t x = phase & 0x7fff, p = x * (32768 - x);
    int32_t s = (int32_t)(16 * p * 32767 / (5 * ((int64_t)1 << 30) - 4 * p));
    return phase & 0x8000 ? -s : s;
}

/* A tone of hz256 / 256 Hz at sample t of 44100 a second. Its phase in
 * 65536ths of a turn is hz256 t 256 / 44100, which repeats every
 * 44100 * 256 samples. */
static int32_t tone(int64_t hz256, int64_t t)
{
    t %= 44100 * 256;
    return isin((uint32_t)(((hz256 * t * 256) / 44100) & 0xffff));
}

/* lo + (hi - lo) (1 + sin) / 2, of a phase in 65536ths. */
static int64_t swing(int64_t lo, int64_t hi, uint32_t phase)
{
    return lo + (hi - lo) * (isin(phase) + 32767) / 65534;
}

void sk_milk_demo_signal(int n, float *out)
{
    /* Frames 1/60 s apart, 735 samples, so successive frames overlap as
     * the synthesiser's do. Each tone's pitch and loudness drift with
     * the frame; loudness is of 32767. */
    const int64_t step = 735;
    uint32_t k = (uint32_t)(n < 0 ? -n : n);
    int64_t f1 = swing(55 * 256, 165 * 256, k * 136), a1 = swing(3300, 8200, k * 220);
    int64_t f2 = swing(460 * 256, 860 * 256, k * 73 + 10000), a2 = swing(0, 3900, k * 178 + 20000);
    int64_t f3 = swing(3600 * 256, 6000 * 256, k * 323), a3 = swing(0, 2000, k * 450 + 40000);
    /* A thump every 30 frames that fades over 0.1 s, and a burst of
     * noise for 8 frames of every 47. */
    int64_t beat = k % 30, burst = k % 47, fade = 4410;
    for (int i = 0; i < SK_MILK_SAMPLES; i++) {
        int64_t t = (int64_t)k * step + i, since = beat * step + i;
        int64_t s = a1 * tone(f1, t) + a2 * tone(f2, t) + a3 * tone(f3, t);
        if (since < fade)
            s += 14700 * tone(55 * 256, since) / fade * (fade - since) / fade * (fade - since);
        if (burst < 8) {
            int64_t noise = (int64_t)(mix64((uint64_t)t) >> 49) - 16384;
            s += noise * 2 * (9800 - 1225 * burst);
        }
        /* s is of 32767 squared. */
        s /= 32767;
        s = s < -32767 ? -32767 : s > 32767 ? 32767 : s;
        out[i] = (float)s / 32767.0f;
    }
}
