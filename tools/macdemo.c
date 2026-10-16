/* macdemo: one window of platform/ui.h, to look at by hand. It draws
 * bands that move at each frame, prints every event and the frame rate,
 * and quits when the window is closed or after SECONDS (default 5).
 *
 *   make macdemo && build/release/macdemo [SECONDS] */
#include "../src/platform/ui.h"

#include <stdio.h>
#include <stdlib.h>

static sk_window *win;
static int frames, shift;

static void draw(sk_window *w)
{
    sk_image *f = sk_window_frame(w);
    for (int y = 0; y < f->h; y++) {
        sk_px *row = sk_image_row(f, y);
        for (int x = 0; x < f->w; x++) {
            int band = ((x + y + shift) / 32) % 3;
            row[x] = band == 0 ? sk_premul(SK_RGB(200, 60, 40)) : band == 1 ? sk_premul(SK_RGB(40, 160, 90))
                                                                          : sk_premul(SK_RGB(30, 30, 30));
        }
    }
    /* The top-left corner, white, to show which way is up. */
    for (int y = 0; y < SK_MIN(f->h, 40); y++)
        for (int x = 0; x < SK_MIN(f->w, 40); x++)
            sk_image_row(f, y)[x] = 0xffffffffu;
    sk_window_present(w, NULL, 0);
}

static void on_event(void *ud, const sk_event *ev)
{
    (void)ud;
    switch (ev->type) {
    case SK_EV_CLOSE:
        printf("close\n");
        sk_ui_quit();
        break;
    case SK_EV_RESIZE:
        printf("resize %dx%d @%g\n", ev->size.w, ev->size.h, ev->size.scale);
        draw(ev->win);
        break;
    case SK_EV_FRAME:
        frames++;
        shift += 4;
        draw(ev->win);
        sk_window_request_frame(ev->win);
        break;
    case SK_EV_FOCUS:
        printf("focus %d\n", ev->focused);
        break;
    case SK_EV_KEY_DOWN:
    case SK_EV_KEY_UP:
        printf("key %s key=%d text=\"%s\" base=%u mods=%x repeat=%d code=%u\n",
               ev->type == SK_EV_KEY_DOWN ? "down" : "up", ev->key.key, ev->key.text, ev->key.base, ev->key.mods,
               ev->key.repeat, ev->key.code);
        break;
    case SK_EV_MOUSE_DOWN:
    case SK_EV_MOUSE_UP:
        printf("mouse %s %d at %.1f,%.1f clicks %d mods %x\n", ev->type == SK_EV_MOUSE_DOWN ? "down" : "up",
               ev->mouse.button, ev->mouse.x, ev->mouse.y, ev->mouse.clicks, ev->mouse.mods);
        break;
    case SK_EV_MOUSE_MOVE:
        printf("move %.1f,%.1f\n", ev->mouse.x, ev->mouse.y);
        break;
    case SK_EV_MOUSE_LEAVE:
        printf("leave\n");
        break;
    case SK_EV_SCROLL:
        printf("scroll %.2f,%.2f precise %d\n", ev->scroll.dx, ev->scroll.dy, ev->scroll.precise);
        break;
    default:
        break;
    }
    fflush(stdout);
}

static void second(void *ud)
{
    (void)ud;
    printf("%d frames a second\n", frames);
    fflush(stdout);
    frames = 0;
}

static void stop(void *ud)
{
    (void)ud;
    sk_ui_quit();
}

int main(int argc, char **argv)
{
    char *err = NULL;
    if (!sk_ui_open("macdemo", on_event, NULL, &err)) {
        fprintf(stderr, "macdemo: %s\n", err);
        return 1;
    }
    sk_window_opts o = {.title = "macdemo", .w = 480, .h = 320, .min_w = 200, .min_h = 120};
    win = sk_window_new(&o, &err);
    if (!win) {
        fprintf(stderr, "macdemo: %s\n", err);
        return 1;
    }
    int w, h;
    sk_window_size(win, &w, &h);
    printf("window %dx%d @%g\n", w, h, sk_window_scale(win));
    draw(win);
    sk_window_request_frame(win);
    sk_ui_timer(1000, 1000, second, NULL);
    sk_ui_timer((uint64_t)(argc > 1 ? atof(argv[1]) * 1000 : 5000), 0, stop, NULL);
    sk_ui_run();
    sk_ui_close();
    return 0;
}
