/* vizshot: runs a visualizer mode on the scripted session (src/viz/
 * script.h) for some frames at 60 a second and writes the last frame as
 * a QOI image, to look at with qoi2png or to compare with a golden one.
 * With -b it times each frame's drawing and prints the median, the 95th
 * percentile and the worst, in milliseconds.
 *
 *   vizshot [-s WxH] [-x SCALE] [-t START] [-b] MODE FRAMES [OUT.qoi]
 *
 * The pane is 240x150 points at scale 1 unless told; START is the
 * script's time of the first frame, 0 unless told. */
#include "../src/viz/script.h"
#include "../src/viz/viz.h"

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

static bool write_file(const char *path, const uint8_t *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    bool ok = fwrite(data, 1, n, f) == n;
    return fclose(f) == 0 && ok;
}

static int usage(void)
{
    fprintf(stderr, "usage: vizshot [-s WxH] [-x SCALE] [-t START] [-b] MODE FRAMES [OUT.qoi]\n");
    return 2;
}

int main(int argc, char **argv)
{
    int w = 240, h = 150, i = 1;
    double scale = 1, start = 0;
    bool bench = false;
    for (; i < argc && argv[i][0] == '-'; i++) {
        if (strcmp(argv[i], "-b") == 0)
            bench = true;
        else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc && sscanf(argv[i + 1], "%dx%d", &w, &h) == 2)
            i++;
        else if (strcmp(argv[i], "-x") == 0 && i + 1 < argc)
            scale = atof(argv[++i]);
        else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc)
            start = atof(argv[++i]);
        else
            return usage();
    }
    if (argc - i < 2 || argc - i > 3 || w < 1 || h < 1 || w > 8192 || h > 8192 || !(scale > 0 && scale <= 4))
        return usage();
    int mode = sk_viz_mode_find(argv[i]);
    int frames = atoi(argv[i + 1]);
    const char *out = argc - i == 3 ? argv[i + 2] : NULL;
    if (frames < 1 || frames > 1000000)
        return usage();
    sk_viz_run run;
    if (mode < 0 || !sk_viz_run_init(&run, mode, start, w, h, scale)) {
        fprintf(stderr, "vizshot: %s is no Canvas2D mode\n", argv[i]);
        return 2;
    }
    double *ms = bench ? sk_xcalloc((size_t)frames, sizeof *ms) : NULL;
    const sk_image *im = NULL;
    for (int k = 0; k < frames; k++) {
        sk_viz_run_feed(&run);
        double t0 = bench ? now_ms() : 0;
        im = sk_viz_run_draw(&run);
        if (bench)
            ms[k] = now_ms() - t0;
    }
    int status = 0;
    if (bench) {
        qsort(ms, (size_t)frames, sizeof *ms, by_value);
        printf("%s %dx%d at %gx: median %.2f ms, p95 %.2f, worst %.2f over %d frames\n", argv[i], w, h, scale,
               ms[frames / 2], ms[(int)(frames * 0.95)], ms[frames - 1], frames);
        free(ms);
    }
    if (out) {
        size_t n = 0;
        uint8_t *q = sk_qoi_encode(im, &n);
        if (!q || !write_file(out, q, n)) {
            fprintf(stderr, "vizshot: cannot write %s\n", out);
            status = 1;
        }
        free(q);
    }
    sk_viz_run_free(&run);
    return status;
}
