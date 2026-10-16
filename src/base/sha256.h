/* SHA-256 (FIPS 180-4). The generated settings files and the recorded
 * profiles are named for the first 16 hex digits of one. */
#ifndef SK_SHA256_H
#define SK_SHA256_H

#include "base.h"

typedef struct {
    uint32_t h[8];
    uint64_t bytes;
    unsigned char block[64];
    size_t fill;
} sk_sha256;

void sk_sha256_init(sk_sha256 *c);
void sk_sha256_update(sk_sha256 *c, const void *data, size_t n);
void sk_sha256_final(sk_sha256 *c, unsigned char out[32]);
/* The digest of data in lowercase hex, NUL-terminated. */
void sk_sha256_hex(const void *data, size_t n, char out[65]);

#endif
