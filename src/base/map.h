/* A hash map from byte-string keys to pointers. Keys are copied; values
 * are the caller's. SipHash with a per-process random key, since keys can
 * come from untrusted JSON. */
#ifndef SK_MAP_H
#define SK_MAP_H

#include "str.h"

typedef struct sk_map_slot sk_map_slot;

typedef struct {
    sk_map_slot *slots;
    size_t cap, len;
    uint64_t k0, k1;
} sk_map;

void sk_map_init(sk_map *m);
/* The value's slot for key, inserted holding NULL when absent. The
 * pointer is valid until the next put or delete. */
void **sk_map_put(sk_map *m, sk_str key);
void *sk_map_get(const sk_map *m, sk_str key);
bool sk_map_has(const sk_map *m, sk_str key);
/* Removes key; returns its value, or NULL. */
void *sk_map_del(sk_map *m, sk_str key);
/* Iteration in no particular order: size_t i = 0; while (sk_map_next(m,
 * &i, &k, &v)) ... Inserting or deleting during it is not allowed. */
bool sk_map_next(const sk_map *m, size_t *i, sk_str *key, void **val);
/* Frees the keys and slots, not the values. */
void sk_map_free(sk_map *m);

/* SipHash-2-4 of data under the 128-bit key k0, k1. */
uint64_t sk_siphash(uint64_t k0, uint64_t k1, const void *data, size_t n);
/* Fills buf from /dev/urandom; false when it cannot. */
bool sk_random_bytes(void *buf, size_t n);

#endif
