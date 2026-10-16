/* Files embedded in the program as generated C arrays (decision 0036):
 * tools/bin2c writes them at build time, so no data file has to be
 * installed beside the binary. */
#ifndef SK_EMBED_H
#define SK_EMBED_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *name; /* the file's basename */
    const uint8_t *data;
    size_t size;
} sk_embedded_file;

/* The DejaVu faces of third_party/dejavu, in sk_embedded_font's order
 * (text/text.h). */
extern const sk_embedded_file sk_embedded_fonts[];
extern const size_t sk_embedded_fonts_count;

#endif
