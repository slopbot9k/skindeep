/* milkshot: a MilkDrop preset (src/viz/milk.h) run on the test signal
 * at 60 frames a second, its frames written as QOI and timed.
 *
 *   milkshot [options] PRESET FRAMES W H [OUT]
 *
 * PRESET is drift, rings, plasma, stardust or whirlpool; W by H is the
 * pane in points. OUT, if given, is a path prefix: the last frame goes
 * to OUT-<frame>.qoi, and with -e N every Nth frame as well.
 *
 *   -s SCALE   the pane's pixels per point (default 1)
 *   -e N       write every Nth frame too
 *   -t         print the frame times: the median, the 90th centile, the
 *              worst, over the frames after the first 10
 *   -j N       at most N threads (sk_milk_set_threads)
 *   -b P,F     at frame F, blend to preset P over 1.5 s, as viz.js does
 *   -x SEED    the renderer's seed (default 1) */
#include "../src/viz/milk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

static int by_value(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static bool save(const sk_image *im, const char *prefix, int frame)
{
    char path[4096];
    snprintf(path, sizeof path, "%s-%04d.qoi", prefix, frame);
    size_t n = 0;
    uint8_t *q = sk_qoi_encode(im, &n);
    FILE *f = q ? fopen(path, "wb") : NULL;
    bool ok = f && fwrite(q, 1, n, f) == n;
    if (f && fclose(f) != 0)
        ok = false;
    free(q);
    if (!ok)
        fprintf(stderr, "milkshot: cannot write %s\n", path);
    return ok;
}

static int usage(void)
{
    fprintf(stderr, "usage: milkshot [-s SCALE] [-e N] [-t] [-j N] [-b PRESET,FRAME] [-x SEED] PRESET FRAMES W H [OUT]\n");
    return 2;
}

int main(int argc, char **argv)
{
    int scale = 1, every = 0, threads = 0, blend_to = -1, blend_at = -1;
    bool timing = false;
    unsigned long long seed = 1;
    int i = 1;
    for (; i < argc && argv[i][0] == '-'; i++) {
        const char *o = argv[i];
        if (!strcmp(o, "-t")) {
            timing = true;
            continue;
        }
        if (i + 1 >= argc)
            return usage();
        const char *v = argv[++i];
        if (!strcmp(o, "-s"))
            scale = atoi(v);
        else if (!strcmp(o, "-e"))
            every = atoi(v);
        else if (!strcmp(o, "-j"))
            threads = atoi(v);
        else if (!strcmp(o, "-x"))
            seed = strtoull(v, NULL, 10);
        else if (!strcmp(o, "-b")) {
            char name[64];
            const char *comma = strchr(v, ',');
            if (!comma || (size_t)(comma - v) >= sizeof name)
                return usage();
            memcpy(name, v, (size_t)(comma - v));
            name[comma - v] = 0;
            blend_to = sk_milk_find(name);
            blend_at = atoi(comma + 1);
            if (blend_to < 0)
                return usage();
        } else
            return usage();
    }
    if (argc - i < 4)
        return usage();
    int preset = sk_milk_find(argv[i]);
    int frames = atoi(argv[i + 1]), w = atoi(argv[i + 2]), h = atoi(argv[i + 3]);
    const char *out = argc - i > 4 ? argv[i + 4] : NULL;
    if (preset < 0 || frames < 1 || w < 1 || h < 1 || scale < 1 || w > 8192 || h > 8192 || scale > 4)
        return usage();

    sk_milk_set_threads(threads);
    sk_milk *m = sk_milk_new(preset, seed);
    sk_image im;
    sk_image_init(&im, w * scale, h * scale);
    float samples[SK_MILK_SAMPLES];
    double *ms = sk_xmalloc((size_t)frames * sizeof *ms);
    int status = 0;
    for (int n = 0; n < frames; n++) {
        if (n == blend_at)
            sk_milk_load(m, blend_to, 1.5);
        sk_milk_demo_signal(n, samples);
        double t0 = now_ms();
        sk_milk_frame(m, samples, 1.0 / 60, &im, scale);
        ms[n] = now_ms() - t0;
        bool last = n == frames - 1;
        if (out && (last || (every > 0 && (n + 1) % every == 0)) && !save(&im, out, n + 1))
            status = 1;
    }
    if (timing) {
        int skip = frames > 20 ? 10 : 0, k = frames - skip;
        qsort(ms + skip, (size_t)k, sizeof *ms, by_value);
        const sk_milk_audio *a = sk_milk_levels(m);
        printf("%s %dx%d at %dx: %d frames, median %.2f ms, p90 %.2f ms, worst %.2f ms\n", argv[i], w, h, scale, k,
               ms[skip + k / 2], ms[skip + (k * 9) / 10], ms[frames - 1]);
        printf("levels: bass %.3f mid %.3f treb %.3f, att %.3f %.3f %.3f\n", a->bass, a->mid, a->treb, a->bass_att,
               a->mid_att, a->treb_att);
    }
    free(ms);
    sk_image_free(&im);
    sk_milk_free(m);
    return status;
}
