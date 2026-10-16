#include "internal.h"

#include "../base/embed.h"

#include <pthread.h>

static sk_font *embedded[SK_FONT_EMBEDDED_COUNT];
static pthread_once_t embedded_once = PTHREAD_ONCE_INIT;

static void open_embedded(void)
{
    if (sk_embedded_fonts_count != SK_FONT_EMBEDDED_COUNT)
        sk_die("%zu embedded fonts, want %d", sk_embedded_fonts_count, (int)SK_FONT_EMBEDDED_COUNT);
    for (int i = 0; i < SK_FONT_EMBEDDED_COUNT; i++) {
        embedded[i] = sk_font_open(sk_embedded_fonts[i].data, sk_embedded_fonts[i].size, 0);
        if (!embedded[i])
            sk_die("embedded font %s does not open", sk_embedded_fonts[i].name);
    }
}

const sk_font *sk_font_embedded(sk_embedded_font which)
{
    if ((unsigned)which >= SK_FONT_EMBEDDED_COUNT)
        sk_die("no embedded font %d", (int)which);
    pthread_once(&embedded_once, open_embedded);
    return embedded[which];
}
