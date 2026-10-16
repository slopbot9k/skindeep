/* skinshot: a skin drawn as the C port draws it, to look at beside the
 * Electron app's screenshots.
 *
 *   skinshot [-t] [-v] SKIN OUT
 *
 * compiles SKIN with skinc's front end, lays it out at its skin.json
 * window size (less the identity strip), paints its default state at 1x
 * and 2x and writes OUT-1x.qoi and OUT-2x.qoi; build/<mode>/qoi2png
 * turns them into PNGs. -t fills the terminal's rect with its theme's
 * background, as the app shows it; -v prints every box laid out. */
#include "shot.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6;
}

static bool write_qoi(const char *path, const sk_image *im)
{
    size_t n = 0;
    uint8_t *q = sk_qoi_encode(im, &n);
    FILE *f = q ? fopen(path, "wb") : NULL;
    bool ok = f && fwrite(q, 1, n, f) == n;
    if (f && fclose(f) != 0)
        ok = false;
    free(q);
    return ok;
}

static void dump(const sk_shot *s)
{
    const sk_layout *l = &s->layout;
    for (uint32_t i = 0; i < l->n; i++) {
        const sk_lbox *b = &l->boxes[i];
        if (!b->generated)
            continue;
        const sk_el *e = s->front.els.ptr[i];
        printf("%4u ", i);
        for (const sk_el *p = e->parent; p; p = p->parent)
            printf(" ");
        printf("%s", e->tag ? e->tag : e->kind == SK_EL_BEFORE ? "::before" : e->kind == SK_EL_AFTER ? "::after" : "?");
        for (uint16_t k = 0; k < e->nclasses; k++)
            printf(".%s", e->classes[k]);
        if (b->svg_content)
            printf("  (svg)\n");
        else
            printf("  %.2f,%.2f %.2fx%.2f%s\n", (double)b->border.x, (double)b->border.y, (double)b->border.w,
                   (double)b->border.h, b->nruns ? "  text" : "");
    }
}

int main(int argc, char **argv)
{
    bool terminal = false, verbose = false;
    int a = 1;
    for (; a < argc && argv[a][0] == '-'; a++) {
        if (!strcmp(argv[a], "-t"))
            terminal = true;
        else if (!strcmp(argv[a], "-v"))
            verbose = true;
        else
            break;
    }
    if (argc - a != 2) {
        fprintf(stderr, "usage: skinshot [-t] [-v] SKIN OUT\n");
        return 2;
    }
    sk_diags d = {0};
    sk_shot s;
    double t0 = now_ms();
    if (!sk_shot_load(&s, argv[a], &d)) {
        for (size_t i = 0; i < d.items.len; i++)
            fprintf(stderr, "%s\n", d.items.ptr[i]);
        sk_diags_free(&d);
        return 1;
    }
    sk_diags_free(&d);
    double t1 = now_ms();
    if (verbose)
        dump(&s);
    printf("%s: compiled and laid out in %.1f ms, %u elements\n", argv[a], t1 - t0, s.layout.n);
    int status = 0;
    for (int scale = 1; scale <= 2; scale++) {
        sk_image im;
        double p0 = now_ms();
        sk_shot_paint(&s, (float)scale, terminal, &im);
        double p1 = now_ms();
        char path[4096];
        snprintf(path, sizeof path, "%s-%dx.qoi", argv[a + 1], scale);
        if (!write_qoi(path, &im)) {
            fprintf(stderr, "skinshot: cannot write %s\n", path);
            status = 1;
        }
        printf("%dx: %dx%d painted in %.1f ms -> %s\n", scale, im.w, im.h, p1 - p0, path);
        sk_image_free(&im);
    }
    sk_shot_free(&s);
    return status;
}
