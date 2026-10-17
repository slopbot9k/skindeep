/* platform/ui.h over an X11 connection (decision 0036). platform/linux/
 * ui.c gives these ui.h's names; here they take the display name, so the
 * tests drive the whole window layer against a fake server on any
 * system. Each does what its ui.h namesake does.
 *
 * The UI thread runs its own sk_loop (core/loop.h), which watches the X
 * socket. It takes no signals: they stay with the core thread's loop. */
#ifndef SK_X11_XUI_H
#define SK_X11_XUI_H

#include "../ui.h"

bool sk_xui_open(const char *display, const char *app_id, sk_ui_handler handler, void *ud,
                 char **err);
bool sk_xui_run(void);
void sk_xui_quit(void);
void sk_xui_close(void);
void sk_xui_post(sk_ui_task task, void *ud);
void sk_xui_post_owned(sk_ui_task task, void *ud, sk_ui_task drop);
int sk_xui_timer(uint64_t delay_ms, uint64_t repeat_ms, sk_ui_task task, void *ud);
void sk_xui_cancel(int id);

void sk_xui_clipboard_set(const char *utf8);
void sk_xui_primary_set(const char *utf8);
void sk_xui_clipboard_get(bool primary, void (*done)(void *ud, const char *utf8), void *ud);

sk_window *sk_xui_window_new(const sk_window_opts *o, char **err);
void sk_xui_window_free(sk_window *w);
void *sk_xui_window_ud(const sk_window *w);
void sk_xui_window_set_title(sk_window *w, const char *title);
void sk_xui_window_set_min_size(sk_window *w, int min_w, int min_h);
void sk_xui_window_size(const sk_window *w, int *pw, int *ph);
bool sk_xui_window_origin(const sk_window *w, int *x, int *y);
double sk_xui_window_scale(const sk_window *w);
void sk_xui_window_set_cursor(sk_window *w, sk_cursor c);
void sk_xui_window_focus(sk_window *w);
sk_image *sk_xui_window_frame(sk_window *w);
void sk_xui_window_present(sk_window *w, const sk_rect *dirty, size_t n);
void sk_xui_window_request_frame(sk_window *w);
bool sk_xui_window_readback(sk_window *w, sk_image *out);

/* For tests: the window's X id, one round of the UI loop, a round trip
 * after which the server has handled every request, and whether frames
 * reach the server through MIT-SHM. */
uint32_t sk_xui_window_id(const sk_window *w);
void sk_xui_once(int timeout_ms);
bool sk_xui_sync(void);
bool sk_xui_shm(void);

#endif
