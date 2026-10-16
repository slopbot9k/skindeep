/* Open addressing with linear probing and backward-shift deletion, so no
 * tombstones build up. */
#include "map.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct sk_map_slot {
    char *key; /* NULL: empty */
    size_t klen;
    uint64_t hash;
    void *val;
};

#define ROTL(x, b) (uint64_t)(((x) << (b)) | ((x) >> (64 - (b))))
#define SIPROUND                                                             \
    do {                                                                     \
        v0 += v1, v1 = ROTL(v1, 13), v1 ^= v0, v0 = ROTL(v0, 32);            \
        v2 += v3, v3 = ROTL(v3, 16), v3 ^= v2;                               \
        v0 += v3, v3 = ROTL(v3, 21), v3 ^= v0;                               \
        v2 += v1, v1 = ROTL(v1, 17), v1 ^= v2, v2 = ROTL(v2, 32);            \
    } while (0)

static uint64_t le64(const unsigned char *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--)
        v = (v << 8) | p[i];
    return v;
}

uint64_t sk_siphash(uint64_t k0, uint64_t k1, const void *data, size_t n)
{
    const unsigned char *p = data;
    uint64_t v0 = 0x736f6d6570736575ULL ^ k0, v1 = 0x646f72616e646f6dULL ^ k1;
    uint64_t v2 = 0x6c7967656e657261ULL ^ k0, v3 = 0x7465646279746573ULL ^ k1;
    size_t whole = n - n % 8;
    for (size_t i = 0; i < whole; i += 8) {
        uint64_t m = le64(p + i);
        v3 ^= m;
        SIPROUND;
        SIPROUND;
        v0 ^= m;
    }
    uint64_t b = (uint64_t)n << 56;
    for (size_t i = 0; i < n % 8; i++)
        b |= (uint64_t)p[whole + i] << (8 * i);
    v3 ^= b;
    SIPROUND;
    SIPROUND;
    v0 ^= b;
    v2 ^= 0xff;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    return v0 ^ v1 ^ v2 ^ v3;
}

bool sk_random_bytes(void *buf, size_t n)
{
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0)
        return false;
    unsigned char *p = buf;
    while (n) {
        ssize_t r = read(fd, p, n);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0) {
            close(fd);
            return false;
        }
        p += r;
        n -= (size_t)r;
    }
    close(fd);
    return true;
}

void sk_map_init(sk_map *m)
{
    m->slots = NULL;
    m->cap = m->len = 0;
    uint64_t k[2];
    if (!sk_random_bytes(k, sizeof k)) {
        /* Without /dev/urandom the map still works; only its defence
         * against chosen collisions is weaker. */
        k[0] = (uint64_t)time(NULL) ^ (uint64_t)(uintptr_t)m;
        k[1] = (uint64_t)getpid() * 0x9E3779B97F4A7C15ULL;
    }
    m->k0 = k[0], m->k1 = k[1];
}

static uint64_t hash(const sk_map *m, sk_str key)
{
    return sk_siphash(m->k0, m->k1, key.ptr, key.len);
}

static size_t find(const sk_map *m, sk_str key, uint64_t h)
{
    size_t mask = m->cap - 1;
    for (size_t i = (size_t)h & mask;; i = (i + 1) & mask) {
        const sk_map_slot *s = &m->slots[i];
        if (!s->key)
            return i;
        if (s->hash == h && s->klen == key.len && memcmp(s->key, key.ptr, key.len) == 0)
            return i;
    }
}

static void grow(sk_map *m)
{
    size_t cap = m->cap ? m->cap * 2 : 16;
    if (cap < m->cap || cap > SIZE_MAX / sizeof(sk_map_slot))
        sk_die("map too large");
    sk_map_slot *old = m->slots;
    size_t old_cap = m->cap;
    m->slots = sk_xcalloc(cap, sizeof *m->slots);
    m->cap = cap;
    for (size_t i = 0; i < old_cap; i++) {
        if (!old[i].key)
            continue;
        size_t mask = cap - 1;
        size_t j = (size_t)old[i].hash & mask;
        while (m->slots[j].key)
            j = (j + 1) & mask;
        m->slots[j] = old[i];
    }
    free(old);
}

void **sk_map_put(sk_map *m, sk_str key)
{
    if ((m->len + 1) * 4 > m->cap * 3)
        grow(m);
    uint64_t h = hash(m, key);
    sk_map_slot *s = &m->slots[find(m, key, h)];
    if (!s->key) {
        s->key = sk_xstrndup(key.len ? key.ptr : "", key.len);
        s->klen = key.len;
        s->hash = h;
        s->val = NULL;
        m->len++;
    }
    return &s->val;
}

void *sk_map_get(const sk_map *m, sk_str key)
{
    if (!m->len)
        return NULL;
    const sk_map_slot *s = &m->slots[find(m, key, hash(m, key))];
    return s->key ? s->val : NULL;
}

bool sk_map_has(const sk_map *m, sk_str key)
{
    return m->len && m->slots[find(m, key, hash(m, key))].key;
}

void *sk_map_del(sk_map *m, sk_str key)
{
    if (!m->len)
        return NULL;
    size_t mask = m->cap - 1;
    size_t i = find(m, key, hash(m, key));
    if (!m->slots[i].key)
        return NULL;
    void *val = m->slots[i].val;
    free(m->slots[i].key);
    m->slots[i].key = NULL;
    m->len--;
    /* Shift the following run back so every key stays reachable from
     * its home slot. */
    for (size_t j = (i + 1) & mask; m->slots[j].key; j = (j + 1) & mask) {
        size_t home = (size_t)m->slots[j].hash & mask;
        bool movable = i <= j ? (home <= i || home > j) : (home <= i && home > j);
        if (movable) {
            m->slots[i] = m->slots[j];
            m->slots[j].key = NULL;
            i = j;
        }
    }
    return val;
}

bool sk_map_next(const sk_map *m, size_t *i, sk_str *key, void **val)
{
    for (; *i < m->cap; (*i)++) {
        const sk_map_slot *s = &m->slots[*i];
        if (s->key) {
            if (key)
                *key = sk_str_n(s->key, s->klen);
            if (val)
                *val = s->val;
            (*i)++;
            return true;
        }
    }
    return false;
}

void sk_map_free(sk_map *m)
{
    for (size_t i = 0; i < m->cap; i++)
        free(m->slots[i].key);
    free(m->slots);
    m->slots = NULL;
    m->cap = m->len = 0;
}
