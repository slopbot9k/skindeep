#include "http.h"

#include <string.h>

void sk_http_req_init(sk_http_req *q, size_t body_cap)
{
    memset(q, 0, sizeof *q);
    q->state = SK_HTTP_HEAD;
    q->body_cap = body_cap ? body_cap : SK_HTTP_MAX_BODY;
}

void sk_http_req_free(sk_http_req *q)
{
    sk_buf_free(&q->head);
    sk_buf_free(&q->body);
}

static size_t error(sk_http_req *q, const char *msg, size_t used)
{
    q->state = SK_HTTP_ERROR;
    q->error = msg;
    return used;
}

/* The next line of head from *pos, without its CR LF or bare LF. */
static bool next_line(sk_str head, size_t *pos, sk_str *line)
{
    sk_str rest = sk_str_sub(head, *pos, SIZE_MAX);
    size_t nl = sk_str_find(rest, '\n');
    if (nl == SIZE_MAX)
        return false;
    *line = sk_str_sub(rest, 0, nl);
    if (line->len && line->ptr[line->len - 1] == '\r')
        line->len--;
    *pos += nl + 1;
    return true;
}

static bool token_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           (c && strchr("!#$%&'*+-.^_`|~", c));
}

/* The whole head is in q->head: the request line and the headers. */
static bool parse_head(sk_http_req *q)
{
    sk_str head = sk_buf_str(&q->head), line;
    size_t pos = 0;
    if (!next_line(head, &pos, &line))
        return false;
    size_t sp1 = sk_str_find(line, ' ');
    if (sp1 == SIZE_MAX || sp1 == 0)
        return false;
    q->method = sk_str_sub(line, 0, sp1);
    sk_str rest = sk_str_sub(line, sp1 + 1, SIZE_MAX);
    size_t sp2 = sk_str_find(rest, ' ');
    if (sp2 == SIZE_MAX || sp2 == 0)
        return false;
    q->target = sk_str_sub(rest, 0, sp2);
    sk_str version = sk_str_sub(rest, sp2 + 1, SIZE_MAX);
    if (!sk_str_eq_c(version, "HTTP/1.1") && !sk_str_eq_c(version, "HTTP/1.0"))
        return false;
    for (size_t i = 0; i < q->method.len; i++)
        if (!token_char(q->method.ptr[i]))
            return false;
    bool have_length = false;
    while (next_line(head, &pos, &line) && line.len) {
        size_t colon = sk_str_find(line, ':');
        if (colon == SIZE_MAX || colon == 0)
            return false;
        sk_str name = sk_str_sub(line, 0, colon);
        for (size_t i = 0; i < name.len; i++)
            if (!token_char(name.ptr[i]))
                return false;
        sk_str value = sk_str_trim(sk_str_sub(line, colon + 1, SIZE_MAX));
        if (sk_str_eq_nocase(name, SK_STR("content-length"))) {
            uint64_t len;
            if (!sk_str_to_u64(value, &len))
                return false;
            /* Two lengths that differ make the body's end ambiguous. */
            if (have_length && len != q->content_length)
                return false;
            q->content_length = len;
            have_length = true;
        } else if (sk_str_eq_nocase(name, SK_STR("transfer-encoding"))) {
            return false; /* the relays never send chunked bodies */
        }
    }
    return true;
}

size_t sk_http_req_feed(sk_http_req *q, const char *p, size_t n)
{
    size_t used = 0;
    if (q->state == SK_HTTP_HEAD) {
        /* Take bytes up to the blank line that ends the head. */
        while (used < n) {
            sk_buf_putc(&q->head, p[used++]);
            if (q->head.len > SK_HTTP_MAX_HEAD)
                return error(q, "request head too large", used);
            sk_str h = sk_buf_str(&q->head);
            if (sk_str_suffix(h, SK_STR("\r\n\r\n")) || sk_str_suffix(h, SK_STR("\n\n"))) {
                if (!parse_head(q))
                    return error(q, "malformed request head", used);
                q->state = q->content_length ? SK_HTTP_BODY : SK_HTTP_DONE;
                q->oversize = q->content_length > q->body_cap;
                break;
            }
        }
    }
    if (q->state == SK_HTTP_BODY && used < n) {
        uint64_t left = q->content_length - q->body_seen;
        size_t take = (uint64_t)(n - used) < left ? n - used : (size_t)left;
        if (!q->oversize)
            sk_buf_append(&q->body, p + used, take);
        q->body_seen += take;
        used += take;
        if (q->body_seen == q->content_length)
            q->state = SK_HTTP_DONE;
    }
    return used;
}

void sk_http_write_204(sk_buf *out)
{
    sk_buf_append_c(out, "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n");
}

void sk_http_write_post(sk_buf *out, const char *path, size_t body_len)
{
    sk_buf_printf(out,
                  "POST %s HTTP/1.1\r\nHost: skins\r\nContent-Type: application/json\r\n"
                  "Content-Length: %zu\r\nConnection: close\r\n\r\n",
                  path, body_len);
}
