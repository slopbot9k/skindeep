/* skinc's pack writer (decision 0037's "The file"): what the front end
 * computed, the element tree, its state tables, its styles and
 * keyframes, and the skin's images, fonts and sounds decoded, as the
 * bytes of a pack. */
#ifndef SKINC_WRITE_H
#define SKINC_WRITE_H

#include "front.h"

/* The pack of a skin f compiled, appended to out; hash is its source
 * hash, skin/compile.h's sk_skin_hash of its folder. False with messages
 * in d for an image, font or sound skinc cannot take, or a value past
 * what a pack holds; out is then partial. The same skin gives the same
 * bytes. */
bool sk_pack_write(const sk_front *f, const unsigned char hash[32], sk_buf *out, sk_diags *d);

/* The sections of the n bytes of a pack, a line each: its id, its
 * bytes and its count of records. */
void sk_pack_print_sections(const uint8_t *p, size_t n, FILE *out);

#endif
