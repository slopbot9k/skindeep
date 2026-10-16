/* skinc's messages: one line each, `<file>: <where>: <what>`, as
 * validate.js writes them, where is a field path in skin.json and
 * "line N" in the stylesheet. */
#ifndef SKINC_DIAG_H
#define SKINC_DIAG_H

#include "../src/base/str.h"

typedef struct {
    SK_VEC(char *) items;
} sk_diags;

void sk_diag(sk_diags *d, const char *file, const char *where, const char *fmt, ...) SK_PRINTF(4, 5);
/* "line N" as where. */
void sk_diag_line(sk_diags *d, const char *file, uint32_t line, const char *fmt, ...) SK_PRINTF(4, 5);
void sk_diags_free(sk_diags *d);

#endif
