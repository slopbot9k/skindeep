/* The four shipped skins' packs (decision 0037): skinc compiles
 * skins/coffee, deck, library and phosphor during the build, and
 * tools/bin2c turns the packs into one object linked into skinned and
 * the tests. It is not in libsk.a: skinc links the library, so packs in
 * it would need skinc to build. Each is named for its file,
 * "deck.pack", and goes through sk_pack_load like any other. */
#ifndef SK_SKIN_SHIPPED_H
#define SK_SKIN_SHIPPED_H

#include "../base/embed.h"

#include <string.h>

extern const sk_embedded_file sk_shipped_packs[];
extern const size_t sk_shipped_packs_count;

/* The shipped pack of the skin named ("deck"), or NULL. */
static inline const sk_embedded_file *sk_shipped_pack(const char *name)
{
    size_t n = strlen(name);
    for (size_t i = 0; i < sk_shipped_packs_count; i++)
        if (!strncmp(sk_shipped_packs[i].name, name, n) && !strcmp(sk_shipped_packs[i].name + n, ".pack"))
            return &sk_shipped_packs[i];
    return NULL;
}

#endif
