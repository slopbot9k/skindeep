/* stb_image, its functions static, for sk_tool_decode. */
#include "imgdecode.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 16384
#include "../third_party/stb/stb_image.h"

#include <fcntl.h>
#include <unistd.h>

static unsigned char *slurp(const char *path, size_t *n)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return NULL;
    size_t cap = 65536, len = 0;
    unsigned char *b = sk_xmalloc(cap);
    for (;;) {
        if (len == cap) {
            if (cap > ((size_t)1 << 30)) {
                free(b);
                close(fd);
                return NULL;
            }
            cap *= 2;
            b = sk_xrealloc(b, cap);
        }
        ssize_t r = read(fd, b + len, cap - len);
        if (r < 0) {
            free(b);
            close(fd);
            return NULL;
        }
        if (r == 0)
            break;
        len += (size_t)r;
    }
    close(fd);
    *n = len;
    return b;
}

bool sk_tool_decode(const char *path, sk_image *out)
{
    size_t n = 0;
    unsigned char *data = slurp(path, &n);
    if (!data || n > (size_t)0x7fffffff) {
        free(data);
        return false;
    }
    int w, h, comp;
    unsigned char *px = stbi_load_from_memory(data, (int)n, &w, &h, &comp, 4);
    free(data);
    if (!px || w <= 0 || h <= 0 || (size_t)w * (size_t)h > ((size_t)1 << 26)) {
        if (px)
            stbi_image_free(px);
        return false;
    }
    sk_image_init(out, w, h);
    for (int y = 0; y < h; y++) {
        sk_px *row = sk_image_row(out, y);
        const unsigned char *s = px + (size_t)y * (size_t)w * 4;
        for (int x = 0; x < w; x++)
            row[x] = sk_premul(SK_RGBA(s[4 * x], s[4 * x + 1], s[4 * x + 2], s[4 * x + 3]));
    }
    stbi_image_free(px);
    return true;
}
