/* The text of a drop of files, as app/renderer/renderer.js made it. */
#include "drop.h"

#include "identity.h"

void sk_drop_text(sk_buf *out, const char *const *paths, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (!paths[i] || !*paths[i])
            continue;
        sk_identity_quote(out, sk_str_c(paths[i]));
        sk_buf_putc(out, ' ');
    }
}
