/* The app's side of user skins (decision 0037): the cache key, skinc
 * found beside the program, run as a child with its output kept, and
 * the pack it writes renamed into the cache and loaded from there. */
#include "compile.h"

#include "../base/paths.h"
#include "../base/sha256.h"
#include "../base/spawn.h"
#include "../core/command.h"
#include "../core/loop.h"
#include "packfmt.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

/* skinc's output kept for the messages, at most. */
#define MAX_MESSAGES (64 * 1024)
/* How deep the folders of a skin may go. */
#define MAX_DEPTH 32
/* What a skin folder may hold, before skinc sees it: its hash reads every
 * file, and a hostile folder may hold a sparse file of gigabytes. The
 * shipped skins hold a few megabytes. */
#define MAX_FILES 4096
#define MAX_FILE_BYTES ((uint64_t)64 << 20)
#define MAX_TOTAL_BYTES ((uint64_t)256 << 20)
/* How long skinc may take: the shipped skins compile in under half a
 * second. Past it, the skin is refused and skinc killed. */
unsigned sk_skinc_deadline_ms = 20000;

static bool say(char *err, size_t err_len, const char *fmt, ...) SK_PRINTF(3, 4);

static bool say(char *err, size_t err_len, const char *fmt, ...)
{
    if (err_len) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, err_len, fmt, ap);
        va_end(ap);
    }
    return false;
}

/* The file at path, at most max bytes; false with why. */
static bool slurp(const char *path, size_t max, uint8_t **out, size_t *n, char *err, size_t err_len)
{
    *out = NULL;
    *n = 0;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return say(err, err_len, "cannot read %s: %s", path, strerror(errno));
    sk_buf b = {0};
    char chunk[65536];
    ssize_t r;
    while ((r = read(fd, chunk, sizeof chunk)) != 0) {
        if (r < 0 && errno == EINTR)
            continue;
        if (r < 0 || b.len + (size_t)r > max) {
            int e = r < 0 ? errno : EFBIG;
            close(fd);
            sk_buf_free(&b);
            return say(err, err_len, "cannot read %s: %s", path, strerror(e));
        }
        sk_buf_append(&b, chunk, (size_t)r);
    }
    close(fd);
    if (!b.ptr)
        sk_buf_reserve(&b, 1);
    *out = (uint8_t *)b.ptr;
    *n = b.len;
    return true;
}

/* ---- The key ---- */

typedef SK_VEC(char *) path_list;

static int by_path(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* The files under root/rel, as paths relative to root. */
static bool gather(const char *root, const char *rel, int depth, path_list *out, char *err, size_t err_len)
{
    char *dir = rel ? sk_path_join(root, rel) : sk_xstrdup(root);
    DIR *d = opendir(dir);
    if (!d) {
        say(err, err_len, "cannot read %s: %s", dir, strerror(errno));
        free(dir);
        return false;
    }
    bool ok = true;
    struct dirent *e;
    while (ok && (e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        char *child = rel ? sk_path_join(rel, e->d_name) : sk_xstrdup(e->d_name);
        char *full = sk_path_join(root, child);
        struct stat st;
        if (lstat(full, &st) != 0) {
            ok = say(err, err_len, "cannot read %s: %s", full, strerror(errno));
        } else if (S_ISDIR(st.st_mode)) {
            if (depth >= MAX_DEPTH)
                ok = say(err, err_len, "%s: folders more than %d deep", full, MAX_DEPTH);
            else
                ok = gather(root, child, depth + 1, out, err, err_len);
        } else if (S_ISREG(st.st_mode) || (S_ISLNK(st.st_mode) && stat(full, &st) == 0 && S_ISREG(st.st_mode))) {
            if (out->len >= MAX_FILES) {
                ok = say(err, err_len, "%s: more than %d files", root, MAX_FILES);
            } else {
                sk_vec_push(out, child);
                child = NULL;
            }
        }
        free(child);
        free(full);
    }
    closedir(d);
    free(dir);
    return ok;
}

/* The file's name, its size in 8 bytes and its bytes into h, read a
 * chunk at a time: at most MAX_FILE_BYTES, and *total the folder's so
 * far at most MAX_TOTAL_BYTES. */
static bool hash_file(sk_sha256 *h, const char *full, const char *name, uint64_t *total, char *err, size_t err_len)
{
    /* Non-blocking: a fifo swapped in for a file never answers. */
    int fd = open(full, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0)
        return say(err, err_len, "cannot read %s: %s", full, strerror(errno));
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return say(err, err_len, "cannot read %s: not a file", full);
    }
    uint64_t n = (uint64_t)st.st_size;
    if (n > MAX_FILE_BYTES || *total + n > MAX_TOTAL_BYTES) {
        close(fd);
        return say(err, err_len, "%s: a skin's files may hold %llu MiB, and one %llu MiB", full,
                   (unsigned long long)(MAX_TOTAL_BYTES >> 20), (unsigned long long)(MAX_FILE_BYTES >> 20));
    }
    *total += n;
    uint8_t size[8];
    for (int k = 0; k < 8; k++)
        size[k] = (uint8_t)(n >> (8 * k));
    sk_sha256_update(h, name, strlen(name) + 1);
    sk_sha256_update(h, size, sizeof size);
    uint8_t chunk[65536];
    uint64_t done = 0;
    while (done < n) {
        ssize_t r = read(fd, chunk, (size_t)SK_MIN((uint64_t)sizeof chunk, n - done));
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0) {
            close(fd);
            return say(err, err_len, "cannot read %s: %s", full, r < 0 ? strerror(errno) : "it shrank as it was read");
        }
        sk_sha256_update(h, chunk, (size_t)r);
        done += (uint64_t)r;
    }
    close(fd);
    return true;
}

bool sk_skin_hash(const char *dir, unsigned char out[32], char *err, size_t err_len)
{
    path_list files = {0};
    bool ok = gather(dir, NULL, 0, &files, err, err_len);
    if (files.len > 1)
        qsort(files.ptr, files.len, sizeof *files.ptr, by_path);
    sk_sha256 h;
    sk_sha256_init(&h);
    uint8_t head[4] = {SK_PACK_MAJOR & 0xff, SK_PACK_MAJOR >> 8, SK_PACK_MINOR & 0xff, SK_PACK_MINOR >> 8};
    sk_sha256_update(&h, head, sizeof head);
    sk_sha256_update(&h, SK_SKINC_VERSION, sizeof SK_SKINC_VERSION);
    uint64_t total = 0;
    for (size_t i = 0; ok && i < files.len; i++) {
        char *full = sk_path_join(dir, files.ptr[i]);
        ok = hash_file(&h, full, files.ptr[i], &total, err, err_len);
        free(full);
    }
    for (size_t i = 0; i < files.len; i++)
        free(files.ptr[i]);
    sk_vec_free(&files);
    sk_sha256_final(&h, out);
    return ok;
}

/* ---- skinc ---- */

char *sk_skinc_beside(const char *self, const char *path)
{
    if (!self || !*self)
        return NULL;
    char *found = strchr(self, '/') ? sk_xstrdup(self) : sk_resolve_command(self, path);
    char *me = found ? realpath(found, NULL) : NULL;
    free(found);
    if (!me)
        return NULL;
    char *slash = strrchr(me, '/');
    if (slash)
        slash[slash == me ? 1 : 0] = 0;
    char *p = sk_path_join(me, "skinc");
    free(me);
    if (access(p, X_OK) != 0) {
        free(p);
        return NULL;
    }
    return p;
}

/* Runs skinc dir -o pack, its stdin /dev/null, its stdout and stderr
 * into messages. The exit status of waitpid, or -1 with why in
 * messages when it could not run. */
static int run_skinc(const char *skinc, const char *dir, const char *pack, sk_buf *messages)
{
    int fds[2];
    /* The write end is skinc's alone: no other thread spawns while it is
     * open here (base/spawn.h), or the read would wait on that child. */
    sk_spawn_lock();
    if (pipe(fds) != 0) {
        sk_spawn_unlock();
        sk_buf_printf(messages, "cannot run %s: %s\n", skinc, strerror(errno));
        return -1;
    }
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&fa, fds[1], 1);
    posix_spawn_file_actions_adddup2(&fa, fds[1], 2);
    if (fds[1] > 2)
        posix_spawn_file_actions_addclose(&fa, fds[1]);
    char *argv[] = {(char *)skinc, (char *)dir, "-o", (char *)pack, NULL};
    pid_t pid;
    int e = posix_spawn(&pid, skinc, &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(fds[1]);
    sk_spawn_unlock();
    if (e != 0) {
        close(fds[0]);
        sk_buf_printf(messages, "cannot run %s: %s\n", skinc, strerror(e));
        return -1;
    }
    /* Its messages until it ends them, then its end, all within
     * SKINC_MS: a skin that takes longer is refused and skinc killed. */
    uint64_t deadline = sk_now_ms() + sk_skinc_deadline_ms;
    char chunk[4096];
    size_t kept = 0;
    bool late = false;
    for (;;) {
        uint64_t now = sk_now_ms();
        if (now >= deadline) {
            late = true;
            break;
        }
        struct pollfd pf = {fds[0], POLLIN, 0};
        int p = poll(&pf, 1, (int)(deadline - now));
        if (p < 0 && errno == EINTR)
            continue;
        if (p == 0) {
            late = true;
            break;
        }
        ssize_t r = read(fds[0], chunk, sizeof chunk);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            break;
        size_t take = SK_MIN((size_t)r, MAX_MESSAGES - kept);
        sk_buf_append(messages, chunk, take);
        kept += take;
    }
    close(fds[0]);
    int status = 0;
    for (;;) {
        pid_t w = late ? 0 : waitpid(pid, &status, WNOHANG);
        if (w == pid)
            return status;
        if (w < 0 && errno == EINTR)
            continue;
        if (w < 0) {
            sk_buf_printf(messages, "cannot wait for %s: %s\n", skinc, strerror(errno));
            return -1;
        }
        if (late || sk_now_ms() >= deadline) {
            kill(pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
                ;
            sk_buf_printf(messages, "%s: skinc took more than %u ms, the skin is refused\n", dir, sk_skinc_deadline_ms);
            return -1;
        }
        struct timespec nap = {0, 10 * 1000000L};
        nanosleep(&nap, NULL);
    }
}

/* ---- The cache ---- */

static void hex_of(const unsigned char key[32], char out[65])
{
    for (int i = 0; i < 32; i++)
        snprintf(out + 2 * i, 3, "%02x", key[i]);
}

/* The pack at path, loaded, when it passes the loader and, given a key
 * (32 bytes), is the pack of the files the key was made of. */
static bool take(const char *path, const unsigned char *key, sk_skin_pack *out, char *err, size_t err_len)
{
    if (!slurp(path, SK_PACK_MAX_BYTES, &out->bytes, &out->len, err, err_len))
        return false;
    if (!sk_pack_load(out->bytes, out->len, &out->pack, err, err_len)) {
        free(out->bytes);
        out->bytes = NULL;
        return false;
    }
    if (key && memcmp(out->pack.source_hash, key, 32) != 0) {
        sk_pack_free(&out->pack);
        free(out->bytes);
        out->bytes = NULL;
        return say(err, err_len, "it was compiled from other files");
    }
    return true;
}

bool sk_skin_compile(const char *skinc, const char *cache, const char *dir, sk_skin_pack *out, sk_buf *messages)
{
    memset(out, 0, sizeof *out);
    char err[512];
    unsigned char key[32];
    if (!sk_skin_hash(dir, key, err, sizeof err)) {
        sk_buf_printf(messages, "%s\n", err);
        return false;
    }
    char *root = NULL;
    if (cache) {
        root = sk_xstrdup(cache);
    } else {
        sk_env env;
        sk_env_current(&env);
        root = sk_path_cache(&env);
    }
    if (!root) {
        sk_buf_printf(messages, "no cache directory: HOME is not set\n");
        return false;
    }
    char *packs = sk_path_join(root, "packs");
    free(root);
    char hex[65];
    hex_of(key, hex);
    sk_buf name = {0};
    sk_buf_printf(&name, "%s.pack", hex);
    char *final = sk_path_join(packs, name.ptr);
    sk_buf_free(&name);
    bool ok = false;
    if (take(final, key, out, err, sizeof err)) {
        out->cached = true;
        out->path = final;
        free(packs);
        return true;
    }
    /* Not there, or it fails: compiled again. */
    unlink(final);
    if (sk_mkdirs(packs, 0700) != 0) {
        sk_buf_printf(messages, "cannot make %s: %s\n", packs, strerror(errno));
        free(packs);
        free(final);
        return false;
    }
    sk_buf tmp = {0};
    sk_buf_printf(&tmp, "%s/%s.XXXXXX", packs, hex);
    int fd = mkstemp(tmp.ptr);
    if (fd < 0) {
        sk_buf_printf(messages, "cannot write in %s: %s\n", packs, strerror(errno));
        sk_buf_free(&tmp);
        free(final);
        free(packs);
        return false;
    }
    close(fd);
    int status = run_skinc(skinc, dir, tmp.ptr, messages);
    if (status < 0)
        goto out;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (WIFSIGNALED(status))
            sk_buf_printf(messages, "skinc stopped on signal %d\n", WTERMSIG(status));
        else if (!messages->len)
            sk_buf_printf(messages, "skinc failed with status %d\n", WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        goto out;
    }
    if (!take(tmp.ptr, NULL, out, err, sizeof err)) {
        sk_buf_printf(messages, "skinc wrote a pack the loader refuses: %s\n", err);
        goto out;
    }
    /* A skin that changed while skinc read it gives a pack of other
     * files: it is kept under its own key. */
    if (memcmp(out->pack.source_hash, key, 32) != 0) {
        free(final);
        hex_of(out->pack.source_hash, hex);
        sk_buf other = {0};
        sk_buf_printf(&other, "%s/%s.pack", packs, hex);
        final = other.ptr;
    }
    if (rename(tmp.ptr, final) == 0) {
        out->path = final;
        final = NULL;
    }
    ok = true;
out:
    unlink(tmp.ptr);
    sk_buf_free(&tmp);
    free(final);
    free(packs);
    return ok;
}

void sk_skin_pack_free(sk_skin_pack *p)
{
    sk_pack_free(&p->pack);
    free(p->bytes);
    free(p->path);
    memset(p, 0, sizeof *p);
}
