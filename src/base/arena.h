/* Arenas: many small allocations freed together, such as the values of
 * one parsed JSON line. */
#ifndef SK_ARENA_H
#define SK_ARENA_H

#include "base.h"

typedef struct sk_arena_block sk_arena_block;

typedef struct {
    sk_arena_block *head;
    sk_arena_block *first; /* the first block of the ordinary size, kept by reset */
    size_t block_size;
} sk_arena;

/* block_size 0 takes the default, 64 KiB. */
void sk_arena_init(sk_arena *a, size_t block_size);
/* n bytes aligned for any type, uninitialised. Never NULL. */
void *sk_arena_alloc(sk_arena *a, size_t n);
void *sk_arena_zalloc(sk_arena *a, size_t n);
/* count elements of size bytes, checking the product. */
void *sk_arena_array(sk_arena *a, size_t count, size_t size);
/* A NUL-terminated copy of n bytes. */
char *sk_arena_strndup(sk_arena *a, const char *s, size_t n);
/* Frees every block but the first of the ordinary size, which it keeps
 * for reuse; a block made for one large request never stays. */
void sk_arena_reset(sk_arena *a);
void sk_arena_free(sk_arena *a);

#endif
