/* The control socket the live checks drive skinned through (decision
 * 0036, in place of Playwright): with SKINNED_CONTROL=PATH set, the app
 * listens on a Unix socket at PATH for one JSON request a line and
 * answers each with one JSON line, in order per connection. The socket
 * is the user's alone (0600 in a 0700 directory of the caller's). */
#ifndef SK_APP_CONTROL_H
#define SK_APP_CONTROL_H

#include "../base/json.h"
#include "../core/loop.h"

typedef struct sk_control sk_control;
typedef struct sk_control_req sk_control_req;

/* A request, on the loop's thread. The handler answers it then or
 * later, from any thread, with sk_control_answer, exactly once. */
typedef void (*sk_control_fn)(void *ud, sk_control_req *req, const sk_json *msg);

/* NULL with *err a malloc'd reason. */
sk_control *sk_control_open(sk_loop *loop, const char *path, sk_control_fn fn, void *ud, char **err);
/* Closes every connection and removes the socket. Requests not yet
 * answered are answered to nobody. */
void sk_control_close(sk_control *c);

/* Any thread: json, one JSON value, as the request's answer; frees req. */
void sk_control_answer(sk_control_req *req, const char *json, size_t n);
/* {"ok":true}, or {"ok":false,"error":why}. */
void sk_control_ok(sk_control_req *req, const char *why);

#endif
