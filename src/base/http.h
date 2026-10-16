/* The little HTTP/1.1 the hook socket speaks (decisions 0009, 0030): a
 * relay POSTs one JSON body with a Content-Length and closes, and the
 * app answers 204 at once. The parser takes bytes as they arrive. */
#ifndef SK_HTTP_H
#define SK_HTTP_H

#include "str.h"

#define SK_HTTP_MAX_HEAD (8 * 1024)
#define SK_HTTP_MAX_BODY (64 * 1024)

typedef enum {
    SK_HTTP_HEAD, /* reading the request line and headers */
    SK_HTTP_BODY,
    SK_HTTP_DONE,
    SK_HTTP_ERROR,
} sk_http_state;

typedef struct {
    sk_http_state state;
    const char *error;
    size_t body_cap;
    sk_buf head;
    sk_str method, target; /* views into head, once the head is read */
    uint64_t content_length;
    uint64_t body_seen;
    bool oversize; /* the body passed body_cap: read through, not kept */
    sk_buf body;
} sk_http_req;

/* body_cap 0 takes SK_HTTP_MAX_BODY, the cap the relays also keep. */
void sk_http_req_init(sk_http_req *q, size_t body_cap);
void sk_http_req_free(sk_http_req *q);
/* Takes up to n bytes and returns how many it used. It stops at the end
 * of the request; what follows belongs to no request and is the
 * caller's to drop. */
size_t sk_http_req_feed(sk_http_req *q, const char *p, size_t n);
/* The answer to every request: an empty 204 that closes. */
void sk_http_write_204(sk_buf *out);
/* The request head a relay sends before its body. */
void sk_http_write_post(sk_buf *out, const char *path, size_t body_len);

#endif
