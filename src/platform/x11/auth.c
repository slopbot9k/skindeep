/* The Xauthority file: records of a family (16 bits, big-endian), then
 * the address, the display number, the scheme's name and its data, each a
 * 16-bit big-endian length and its bytes. Matched as Xlib matches a local
 * connection, the first record that fits winning. */
#include "x11.h"

#include "../../base/paths.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define COOKIE "MIT-MAGIC-COOKIE-1"
/* An Xauthority file of a few records is small; one past this is not one. */
#define MAX_FILE (1 << 20)

typedef struct {
    const uint8_t *p, *end;
} reader;

static bool field(reader *r, sk_str *out)
{
    if (r->end - r->p < 2)
        return false;
    size_t n = (size_t)r->p[0] << 8 | r->p[1];
    r->p += 2;
    if ((size_t)(r->end - r->p) < n)
        return false;
    *out = sk_str_n((const char *)r->p, n);
    r->p += n;
    return true;
}

bool sk_x11_xauth_find(const uint8_t *buf, size_t n, const char *host, const char *number,
                       sk_x11_cookie *out)
{
    reader r = {buf, buf + n};
    while (r.end - r.p >= 2) {
        unsigned family = (unsigned)r.p[0] << 8 | r.p[1];
        r.p += 2;
        sk_str addr, num, name, data;
        if (!field(&r, &addr) || !field(&r, &num) || !field(&r, &name) || !field(&r, &data))
            return false;
        bool here = family == SK_X11_FAMILY_WILD ||
                    (family == SK_X11_FAMILY_LOCAL && host && sk_str_eq_c(addr, host));
        if (!here || (num.len && !sk_str_eq_c(num, number)) || !sk_str_eq_c(name, COOKIE) ||
            name.len >= sizeof out->name || data.len > sizeof out->data)
            continue;
        memset(out, 0, sizeof *out);
        memcpy(out->name, name.ptr, name.len);
        out->name_len = name.len;
        memcpy(out->data, data.ptr, data.len);
        out->data_len = data.len;
        return true;
    }
    return false;
}

bool sk_x11_xauth_load(const char *number, sk_x11_cookie *out)
{
    const char *path = getenv("XAUTHORITY");
    char *own = NULL;
    if (!path || !*path) {
        const char *home = getenv("HOME");
        if (!home || !*home)
            return false;
        path = own = sk_path_join(home, ".Xauthority");
    }
    FILE *f = fopen(path, "rb");
    free(own);
    if (!f)
        return false;
    sk_buf b = {0};
    char chunk[4096];
    size_t got;
    while ((got = fread(chunk, 1, sizeof chunk, f)) > 0 && b.len < MAX_FILE)
        sk_buf_append(&b, chunk, got);
    fclose(f);
    char host[256]; /* POSIX: at most 255 bytes */
    if (gethostname(host, sizeof host) != 0)
        host[0] = 0;
    host[sizeof host - 1] = 0;
    bool found = sk_x11_xauth_find((const uint8_t *)b.ptr, b.len, host, number, out);
    sk_buf_free(&b);
    return found;
}
