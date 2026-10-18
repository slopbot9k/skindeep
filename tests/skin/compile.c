/* User skins as the app compiles them (decision 0037): the deck skin
 * compiled by skinc, run as a child, into a fresh cache; taken from
 * the cache the next time; compiled again when its cached pack is
 * spoiled; a skin that does not compile giving skinc's messages and
 * leaving nothing behind; the cache key made as compile.h says; skinc
 * found beside the program. */
#include "../../src/base/paths.h"
#include "../../src/base/sha256.h"
#include "../../src/core/loop.h"
#include "../../src/skin/compile.h"
#include "../skinc/util.h"
#include "../test.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *skinc(void)
{
    const char *b = getenv("SK_SKINC_BIN");
    if (!b || access(b, X_OK) != 0) {
        printf("    (skipped: SK_SKINC_BIN not set)\n");
        return NULL;
    }
    return b;
}

/* The files in dir, but . and .. */
static int files_in(const char *dir)
{
    DIR *d = opendir(dir);
    int n = 0;
    struct dirent *e;
    while (d && (e = readdir(d)))
        n += strcmp(e->d_name, ".") && strcmp(e->d_name, "..");
    if (d)
        closedir(d);
    return n;
}

static void test_compile_and_cache(void)
{
    const char *bin = skinc();
    if (!bin)
        return;
    char *cache = sk_path_join(sk_test_tmpdir(), "cache");
    char *packs = sk_path_join(cache, "packs");
    sk_skin_pack p;
    sk_buf msg = {0};
    CHECK(sk_skin_compile(bin, cache, "skins/deck", &p, &msg));
    CHECK_STR(sk_buf_cstr(&msg), "");
    CHECK(!p.cached);
    CHECK_STR(p.pack.meta.name, "deck");
    unsigned char key[32];
    CHECK(sk_skin_hash("skins/deck", key, NULL, 0));
    CHECK(!memcmp(p.pack.source_hash, key, 32));
    sk_buf want = {0};
    sk_buf_printf(&want, "%s/", packs);
    for (int i = 0; i < 32; i++)
        sk_buf_printf(&want, "%02x", key[i]);
    sk_buf_append_c(&want, ".pack");
    CHECK_STR(p.path, sk_buf_cstr(&want));
    CHECK(access(sk_buf_cstr(&want), R_OK) == 0);
    sk_skin_pack_free(&p);
    /* Again: from the cache. */
    CHECK(sk_skin_compile(bin, cache, "skins/deck", &p, &msg));
    CHECK(p.cached);
    CHECK_STR(p.pack.meta.name, "deck");
    sk_skin_pack_free(&p);
    /* A cached pack that fails the loader is deleted and compiled again. */
    sk_test_write(packs, strrchr(sk_buf_cstr(&want), '/') + 1, "SKINPACK spoiled");
    CHECK(sk_skin_compile(bin, cache, "skins/deck", &p, &msg));
    CHECK(!p.cached);
    CHECK_STR(p.pack.meta.name, "deck");
    sk_skin_pack_free(&p);
    CHECK(sk_skin_compile(bin, cache, "skins/deck", &p, &msg));
    CHECK(p.cached);
    sk_skin_pack_free(&p);
    /* Nothing but the pack is left in the cache. */
    CHECK_INT(files_in(packs), 1);
    CHECK_STR(sk_buf_cstr(&msg), "");
    sk_buf_free(&msg);
    sk_buf_free(&want);
    free(packs);
    free(cache);
}

static void test_a_skin_that_does_not_compile(void)
{
    const char *bin = skinc();
    if (!bin)
        return;
    char *cache = sk_path_join(sk_test_tmpdir(), "cache");
    char *packs = sk_path_join(cache, "packs");
    sk_skin_pack p;
    sk_buf msg = {0};
    CHECK(!sk_skin_compile(bin, cache, "tests/fixtures/skins/broken-css", &p, &msg));
    CHECK(strstr(sk_buf_cstr(&msg), "skin.css: line 2: @import is not allowed\n") != NULL);
    CHECK(p.bytes == NULL && p.path == NULL);
    CHECK_INT(files_in(packs), 0);
    sk_buf_clear(&msg);
    CHECK(!sk_skin_compile(bin, cache, "no/such/skin", &p, &msg));
    CHECK(strstr(sk_buf_cstr(&msg), "cannot read no/such/skin") != NULL);
    sk_buf_clear(&msg);
    CHECK(!sk_skin_compile("/no/such/skinc", cache, "skins/deck", &p, &msg));
    CHECK(strstr(sk_buf_cstr(&msg), "cannot run /no/such/skinc") != NULL ||
          strstr(sk_buf_cstr(&msg), "skinc failed") != NULL);
    CHECK_INT(files_in(packs), 0);
    sk_buf_free(&msg);
    free(packs);
    free(cache);
}

/* The key of a folder, made by hand as compile.h defines it. */
static void key_by_hand(const char *const *paths, const char *const *texts, size_t n, unsigned char out[32])
{
    sk_sha256 h;
    sk_sha256_init(&h);
    uint8_t head[4] = {SK_PACK_MAJOR, 0, SK_PACK_MINOR, 0};
    sk_sha256_update(&h, head, 4);
    sk_sha256_update(&h, SK_SKINC_VERSION, strlen(SK_SKINC_VERSION) + 1);
    for (size_t i = 0; i < n; i++) {
        size_t len = strlen(texts[i]);
        uint8_t size[8] = {(uint8_t)len, (uint8_t)(len >> 8), 0, 0, 0, 0, 0, 0};
        sk_sha256_update(&h, paths[i], strlen(paths[i]) + 1);
        sk_sha256_update(&h, size, 8);
        sk_sha256_update(&h, texts[i], len);
    }
    sk_sha256_final(&h, out);
}

static void test_key(void)
{
    char *dir = sk_path_join(sk_test_tmpdir(), "keyed");
    /* "a.txt" sorts before "a/b": '.' is below '/'. */
    sk_test_write(dir, "b.css", "two");
    sk_test_write(dir, "a/b", "three");
    sk_test_write(dir, "a.txt", "one");
    unsigned char got[32], again[32], want[32];
    CHECK(sk_skin_hash(dir, got, NULL, 0));
    const char *const paths[] = {"a.txt", "a/b", "b.css"};
    const char *const texts[] = {"one", "three", "two"};
    key_by_hand(paths, texts, 3, want);
    CHECK(!memcmp(got, want, 32));
    CHECK(sk_skin_hash(dir, again, NULL, 0));
    CHECK(!memcmp(got, again, 32));
    /* A byte changed changes it. */
    sk_test_write(dir, "a/b", "thref");
    CHECK(sk_skin_hash(dir, again, NULL, 0));
    CHECK(memcmp(got, again, 32) != 0);
    char err[300];
    CHECK(!sk_skin_hash("no/such/skin", got, err, sizeof err));
    CHECK(strncmp(err, "cannot read no/such/skin: ", 26) == 0);
    free(dir);
}

static void test_beside(void)
{
    const char *bin = skinc();
    if (!bin)
        return;
    char *real = realpath(bin, NULL);
    char *found = sk_skinc_beside(bin, NULL);
    CHECK(real && found);
    if (real && found)
        CHECK_STR(found, real);
    free(found);
    free(real);
    CHECK(sk_skinc_beside("no-such-program", "") == NULL);
    CHECK(sk_skinc_beside("", "") == NULL);
}

/* A skinc that does not finish in time is killed and the skin refused,
 * so a hostile skin holds the app for its deadline at most. */
static void test_a_skinc_too_slow_is_killed(void)
{
    char *dir = sk_path_join(sk_test_tmpdir(), "slow"), *fake = sk_path_join(dir, "skinc");
    char *cache = sk_path_join(sk_test_tmpdir(), "cache-slow");
    CHECK_INT(sk_mkdirs(dir, 0700), 0);
    FILE *f = fopen(fake, "w");
    CHECK(f != NULL);
    if (f) {
        fputs("#!/bin/sh\nexec sleep 30\n", f);
        fclose(f);
    }
    chmod(fake, 0755);
    unsigned saved = sk_skinc_deadline_ms;
    sk_skinc_deadline_ms = 300;
    sk_skin_pack p;
    sk_buf msg = {0};
    uint64_t t0 = sk_now_ms();
    CHECK(!sk_skin_compile(fake, cache, "skins/deck", &p, &msg));
    CHECK(sk_now_ms() - t0 < 5000);
    CHECK(strstr(sk_buf_cstr(&msg), "skinc took more than 300 ms") != NULL);
    sk_skinc_deadline_ms = saved;
    sk_buf_free(&msg);
    free(dir);
    free(fake);
    free(cache);
}

/* The folder's hash reads no file whole, and a skin's files hold 256
 * MiB at most, one 64: a sparse file of 100 MiB is refused at once. */
static void test_a_huge_file_is_refused_before_skinc(void)
{
    char *dir = sk_path_join(sk_test_tmpdir(), "huge"), *big = sk_path_join(dir, "big.png");
    CHECK_INT(sk_mkdirs(dir, 0700), 0);
    int fd = open(big, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    CHECK(fd >= 0 && ftruncate(fd, (off_t)100 << 20) == 0);
    if (fd >= 0)
        close(fd);
    unsigned char key[32];
    char err[300] = "";
    CHECK(!sk_skin_hash(dir, key, err, sizeof err));
    CHECK(strstr(err, "a skin's files may hold 256 MiB, and one 64 MiB") != NULL);
    /* At the bound itself, it is read. */
    fd = open(big, O_WRONLY | O_TRUNC);
    CHECK(fd >= 0 && ftruncate(fd, (off_t)1 << 20) == 0);
    if (fd >= 0)
        close(fd);
    CHECK(sk_skin_hash(dir, key, err, sizeof err));
    free(dir);
    free(big);
}

const sk_test sk_suite_skin_compile[] = {
    T(test_a_skinc_too_slow_is_killed),
    T(test_a_huge_file_is_refused_before_skinc),
    T(test_compile_and_cache),
    T(test_a_skin_that_does_not_compile),
    T(test_key),
    T(test_beside),
    {0},
};
