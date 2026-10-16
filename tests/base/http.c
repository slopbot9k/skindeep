#include "../../src/base/http.h"
#include "../test.h"

/* What app/relay/hook.py sends, byte for byte. */
static const char HOOK_REQUEST[] = "POST /hook HTTP/1.1\r\nHost: skins\r\n"
                                   "Content-Type: application/json\r\n"
                                   "Content-Length: 13\r\nConnection: close\r\n\r\n"
                                   "{\"a\":\"hook\"}\n";

static void test_hook_request_byte_by_byte(void)
{
    sk_http_req q;
    sk_http_req_init(&q, 0);
    size_t n = sizeof HOOK_REQUEST - 1;
    for (size_t i = 0; i < n; i++)
        CHECK_INT(sk_http_req_feed(&q, HOOK_REQUEST + i, 1), 1);
    CHECK_INT(q.state, SK_HTTP_DONE);
    CHECK_MEM(q.method, "POST");
    CHECK_MEM(q.target, "/hook");
    CHECK_INT(q.content_length, 13);
    CHECK_MEM(sk_buf_str(&q.body), "{\"a\":\"hook\"}\n");
    CHECK(!q.oversize);
    sk_http_req_free(&q);
}

static void test_request_and_what_follows(void)
{
    sk_http_req q;
    sk_http_req_init(&q, 0);
    sk_buf in = {0};
    sk_buf_append_c(&in, HOOK_REQUEST);
    sk_buf_append_c(&in, "junk after the body");
    size_t used = sk_http_req_feed(&q, in.ptr, in.len);
    CHECK_INT(used, sizeof HOOK_REQUEST - 1);
    CHECK_INT(q.state, SK_HTTP_DONE);
    sk_http_req_free(&q);
    sk_buf_free(&in);
}

/* Node's http.request, as statusline.js uses it: lower-case headers. */
static void test_statusline_request(void)
{
    const char *req = "POST /statusline HTTP/1.1\r\ncontent-type: application/json\r\n"
                      "content-length: 2\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n{}";
    sk_http_req q;
    sk_http_req_init(&q, 0);
    sk_http_req_feed(&q, req, strlen(req));
    CHECK_INT(q.state, SK_HTTP_DONE);
    CHECK_MEM(q.target, "/statusline");
    CHECK_MEM(sk_buf_str(&q.body), "{}");
    sk_http_req_free(&q);
}

static void test_bare_newlines_and_no_body(void)
{
    const char *req = "GET / HTTP/1.0\nX-Thing: 1\n\n";
    sk_http_req q;
    sk_http_req_init(&q, 0);
    CHECK_INT(sk_http_req_feed(&q, req, strlen(req)), strlen(req));
    CHECK_INT(q.state, SK_HTTP_DONE);
    CHECK_MEM(q.method, "GET");
    CHECK_INT(q.body.len, 0);
    sk_http_req_free(&q);
}

static void test_oversize_body_read_through(void)
{
    sk_http_req q;
    sk_http_req_init(&q, 8);
    const char *head = "POST /hook HTTP/1.1\r\nContent-Length: 20\r\n\r\n";
    sk_http_req_feed(&q, head, strlen(head));
    CHECK_INT(q.state, SK_HTTP_BODY);
    CHECK(q.oversize);
    CHECK_INT(sk_http_req_feed(&q, "0123456789", 10), 10);
    CHECK_INT(q.state, SK_HTTP_BODY);
    CHECK_INT(sk_http_req_feed(&q, "0123456789abc", 13), 10);
    CHECK_INT(q.state, SK_HTTP_DONE);
    CHECK_INT(q.body.len, 0); /* not kept */
    sk_http_req_free(&q);
}

static void test_malformed(void)
{
    const char *bad[] = {
        "POST\r\n\r\n",
        "POST /hook\r\n\r\n",
        "POST /hook HTTP/2\r\n\r\n",
        "PO ST /hook HTTP/1.1\r\n\r\n",
        "POST /hook HTTP/1.1\r\nno colon\r\n\r\n",
        "POST /hook HTTP/1.1\r\n: empty name\r\n\r\n",
        "POST /hook HTTP/1.1\r\nContent-Length: -1\r\n\r\n",
        "POST /hook HTTP/1.1\r\nContent-Length: 1x\r\n\r\n",
        "POST /hook HTTP/1.1\r\nContent-Length: 2\r\nContent-Length: 3\r\n\r\n",
        "POST /hook HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n",
        "POST /hook HTTP/1.1\r\nContent-Length: 99999999999999999999999\r\n\r\n",
    };
    for (size_t i = 0; i < SK_LEN(bad); i++) {
        sk_http_req q;
        sk_http_req_init(&q, 0);
        sk_http_req_feed(&q, bad[i], strlen(bad[i]));
        if (q.state != SK_HTTP_ERROR)
            sk_test_fail(__FILE__, __LINE__, "accepted: %s", bad[i]);
        sk_http_req_free(&q);
    }
}

static void test_head_too_large(void)
{
    sk_http_req q;
    sk_http_req_init(&q, 0);
    sk_buf in = {0};
    sk_buf_append_c(&in, "POST /hook HTTP/1.1\r\n");
    while (in.len <= SK_HTTP_MAX_HEAD)
        sk_buf_append_c(&in, "X-Padding: xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\r\n");
    sk_http_req_feed(&q, in.ptr, in.len);
    CHECK_INT(q.state, SK_HTTP_ERROR);
    CHECK_STR(q.error, "request head too large");
    sk_http_req_free(&q);
    sk_buf_free(&in);
}

static void test_writers(void)
{
    sk_buf b = {0};
    sk_http_write_204(&b);
    CHECK_STR(b.ptr, "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n");
    sk_buf_clear(&b);
    sk_http_write_post(&b, "/hook", 13);
    sk_buf_append_c(&b, "{\"a\":\"hook\"}\n");
    CHECK_STR(b.ptr, HOOK_REQUEST);
    sk_buf_free(&b);
}

const sk_test sk_suite_http[] = {
    T(test_hook_request_byte_by_byte),
    T(test_request_and_what_follows),
    T(test_statusline_request),
    T(test_bare_newlines_and_no_body),
    T(test_oversize_body_read_through),
    T(test_malformed),
    T(test_head_too_large),
    T(test_writers),
    {0},
};
