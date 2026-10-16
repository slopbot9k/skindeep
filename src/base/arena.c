#include "arena.h"

#include <stdalign.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_BLOCK (64 * 1024)
#define ALIGN alignof(max_align_t)

struct sk_arena_block {
    sk_arena_block *next;
    size_t size, used;
    alignas(max_align_t) unsigned char data[];
};

static size_t round_up(size_t n)
{
    if (n > SIZE_MAX - ALIGN)
        sk_die("arena allocation too large");
    return (n + ALIGN - 1) & ~(size_t)(ALIGN - 1);
}

void sk_arena_init(sk_arena *a, size_t block_size)
{
    a->head = NULL;
    a->block_size = block_size ? block_size : DEFAULT_BLOCK;
}

static sk_arena_block *new_block(size_t size)
{
    if (size > SIZE_MAX - sizeof(sk_arena_block))
        sk_die("arena allocation too large");
    sk_arena_block *b = sk_xmalloc(sizeof *b + size);
    b->next = NULL;
    b->size = size;
    b->used = 0;
    return b;
}

void *sk_arena_alloc(sk_arena *a, size_t n)
{
    n = round_up(n ? n : 1);
    sk_arena_block *b = a->head;
    if (b && b->size - b->used >= n) {
        void *p = b->data + b->used;
        b->used += n;
        return p;
    }
    /* A large request gets a block of its own behind the current one,
     * so the current block's free space is not wasted. */
    if (n > a->block_size / 4 && b) {
        sk_arena_block *big = new_block(n);
        big->used = n;
        big->next = b->next;
        b->next = big;
        return big->data;
    }
    sk_arena_block *nb = new_block(SK_MAX(n, a->block_size));
    nb->next = b;
    a->head = nb;
    nb->used = n;
    return nb->data;
}

void *sk_arena_zalloc(sk_arena *a, size_t n)
{
    void *p = sk_arena_alloc(a, n);
    memset(p, 0, n);
    return p;
}

void *sk_arena_array(sk_arena *a, size_t count, size_t size)
{
    if (size && count > SIZE_MAX / size)
        sk_die("arena array overflows (%zu by %zu bytes)", count, size);
    return sk_arena_alloc(a, count * size);
}

char *sk_arena_strndup(sk_arena *a, const char *s, size_t n)
{
    if (n == SIZE_MAX)
        sk_die("arena string too large");
    char *d = sk_arena_alloc(a, n + 1);
    if (n)
        memcpy(d, s, n);
    d[n] = 0;
    return d;
}

void sk_arena_reset(sk_arena *a)
{
    sk_arena_block *b = a->head;
    if (!b)
        return;
    /* Keep the last block in the chain: the first one allocated, of the
     * ordinary size unless a single large request made it. */
    sk_arena_block *keep = b;
    while (keep->next)
        keep = keep->next;
    while (b != keep) {
        sk_arena_block *next = b->next;
        free(b);
        b = next;
    }
    keep->used = 0;
    a->head = keep;
}

void sk_arena_free(sk_arena *a)
{
    sk_arena_block *b = a->head;
    while (b) {
        sk_arena_block *next = b->next;
        free(b);
        b = next;
    }
    a->head = NULL;
}
