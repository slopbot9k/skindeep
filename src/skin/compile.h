/* A user's skin as the app compiles it (decision 0037): a skin opened
 * by path is compiled by skinc, run as a child process so a decoder a
 * hostile file breaks breaks there, into <cache>/packs/<hash>.pack, and
 * taken from there while its files stay the same. Every pack goes
 * through the loader, cached or not, and a cached pack that fails it is
 * deleted and compiled again.
 *
 * The hash is the cache key and the source hash in every pack's header,
 * which skinc computes the same way. */
#ifndef SK_SKIN_COMPILE_H
#define SK_SKIN_COMPILE_H

#include "../base/str.h"
#include "pack.h"

/* skinc's own version, part of the key: a skinc that writes a skin's
 * pack differently must change it, so no cache keeps an older pack. */
#define SK_SKINC_VERSION "0.5.0"

/* The cache key of the skin at dir: the SHA-256 of SK_PACK_MAJOR and
 * SK_PACK_MINOR (u16 each, little-endian), SK_SKINC_VERSION and a NUL,
 * then of each file under dir in the byte order of its path relative to
 * dir ('/' between the parts), a link to a file counting as that file
 * and a link to a directory as nothing: its path, a NUL, its size (u64,
 * little-endian) and its bytes. False with why in err (err_len bytes)
 * when dir or one of its files cannot be read. */
bool sk_skin_hash(const char *dir, unsigned char out[32], char *err, size_t err_len);

/* The skinc beside the program self names (argv[0], looked for in path,
 * a PATH value, when it holds no '/'), past any link to the program;
 * malloc'd, or NULL when there is none. */
char *sk_skinc_beside(const char *self, const char *path);

typedef struct {
    sk_pack pack;
    uint8_t *bytes; /* the pack file, which pack points into */
    size_t len;
    char *path;  /* where it is cached */
    bool cached; /* taken from the cache, not compiled now */
} sk_skin_pack;

/* The skin at dir as a pack: <cache>/packs/<hash>.pack when it is there
 * and passes the loader, else compiled by running `skinc DIR -o TMP`
 * and renamed there. cache is decision 0036's cache directory, or NULL
 * for this system's (paths.h). False with what went wrong appended to
 * messages: skinc's output, its stdout and stderr as it wrote them, for
 * a skin that does not compile, else a line of why. */
bool sk_skin_compile(const char *skinc, const char *cache, const char *dir, sk_skin_pack *out, sk_buf *messages);
/* How long skinc may take before the skin is refused and skinc killed,
 * in ms; tests make it shorter. */
extern unsigned sk_skinc_deadline_ms;
void sk_skin_pack_free(sk_skin_pack *p);

#endif
