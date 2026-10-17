/* platform/ui.h on Linux: X11's wire protocol to the server $DISPLAY
 * names, which platform/x11/xui.c speaks; under Wayland, XWayland is that
 * server (decision 0036). */
#include "../ui.h"

#include "../x11/xui.h"

bool sk_ui_open(const char *app_id, sk_ui_handler handler, void *ud, char **err)
{
    return sk_xui_open(getenv("DISPLAY"), app_id, handler, ud, err);
}

bool sk_ui_run(void)
{
    return sk_xui_run();
}

void sk_ui_quit(void)
{
    sk_xui_quit();
}

void sk_ui_close(void)
{
    sk_xui_close();
}

void sk_ui_post(sk_ui_task task, void *ud)
{
    sk_xui_post(task, ud);
}

void sk_ui_post_owned(sk_ui_task task, void *ud, sk_ui_task drop)
{
    sk_xui_post_owned(task, ud, drop);
}

int sk_ui_timer(uint64_t delay_ms, uint64_t repeat_ms, sk_ui_task task, void *ud)
{
    return sk_xui_timer(delay_ms, repeat_ms, task, ud);
}

void sk_ui_cancel(int id)
{
    sk_xui_cancel(id);
}

void sk_clipboard_set(const char *utf8)
{
    sk_xui_clipboard_set(utf8);
}

void sk_primary_set(const char *utf8)
{
    sk_xui_primary_set(utf8);
}

void sk_clipboard_get(bool primary, void (*done)(void *ud, const char *utf8), void *ud)
{
    sk_xui_clipboard_get(primary, done, ud);
}

sk_window *sk_window_new(const sk_window_opts *o, char **err)
{
    return sk_xui_window_new(o, err);
}

void sk_window_free(sk_window *w)
{
    sk_xui_window_free(w);
}

void *sk_window_ud(const sk_window *w)
{
    return sk_xui_window_ud(w);
}

void sk_window_set_title(sk_window *w, const char *title)
{
    sk_xui_window_set_title(w, title);
}

void sk_window_size(const sk_window *w, int *pw, int *ph)
{
    sk_xui_window_size(w, pw, ph);
}

double sk_window_scale(const sk_window *w)
{
    return sk_xui_window_scale(w);
}

void sk_window_set_cursor(sk_window *w, sk_cursor c)
{
    sk_xui_window_set_cursor(w, c);
}

void sk_window_focus(sk_window *w)
{
    sk_xui_window_focus(w);
}

/* No input method asks where the cursor is until XIM (decision 0036). */
void sk_window_set_ime_rect(sk_window *w, int x, int y, int width, int height)
{
    (void)w, (void)x, (void)y, (void)width, (void)height;
}

sk_image *sk_window_frame(sk_window *w)
{
    return sk_xui_window_frame(w);
}

void sk_window_present(sk_window *w, const sk_rect *dirty, size_t n)
{
    sk_xui_window_present(w, dirty, n);
}

void sk_window_request_frame(sk_window *w)
{
    sk_xui_window_request_frame(w);
}

bool sk_window_readback(sk_window *w, sk_image *out)
{
    return sk_xui_window_readback(w, out);
}
