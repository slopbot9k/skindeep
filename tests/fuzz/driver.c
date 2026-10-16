/* The in-tree fuzzer (decision 0036), for systems without libFuzzer.
 * Apple's tools ship none.
 *
 *   fuzz-NAME [-t seconds] [-r runs] [-s seed] [-m max_len] [-o dir] [seeds...]
 *   fuzz-NAME -x file...      run each file once, to reproduce a crash
 *
 * It mutates inputs from its corpus, starting with the seed files and
 * directories it is given, and keeps every input that reaches code no
 * earlier input reached. Coverage comes from clang's trace-pc-guard
 * instrumentation of the code under test; without it the mutation is
 * blind. A crash, a sanitizer report or a hang of more than 5 s writes
 * the input to crash-<hash> or hang-<hash>, in the -o directory or the
 * current one. */
#include "fuzz.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define HANG_SECONDS 5

/* Coverage: each instrumented edge has a guard numbered from 1. An edge
 * counts the first time it is reached, then its guard is zeroed so it
 * costs nothing more. */
static uint32_t edges, new_edges;

void __sanitizer_cov_trace_pc_guard_init(uint32_t *start, uint32_t *stop);
void __sanitizer_cov_trace_pc_guard(uint32_t *guard);

void __sanitizer_cov_trace_pc_guard_init(uint32_t *start, uint32_t *stop)
{
    if (start == stop || *start)
        return;
    for (uint32_t *g = start; g < stop; g++)
        *g = ++edges;
}

void __sanitizer_cov_trace_pc_guard(uint32_t *guard)
{
    if (*guard) {
        new_edges++;
        *guard = 0;
    }
}

/* The input being run, for the crash handlers to write out. */
static const uint8_t *cur;
static size_t cur_len;
static const char *out_dir = ".";

static void dump(const char *kind)
{
    uint64_t h = 1469598103934665603ULL; /* FNV-1a */
    for (size_t i = 0; i < cur_len; i++)
        h = (h ^ cur[i]) * 1099511628211ULL;
    char name[512];
    /* Only async-signal-safe calls from here: build the name by hand. */
    size_t n = 0;
    for (const char *p = out_dir; *p && n < 400; p++)
        name[n++] = *p;
    name[n++] = '/';
    for (const char *p = kind; *p; p++)
        name[n++] = *p;
    name[n++] = '-';
    for (int i = 15; i >= 0; i--)
        name[n++] = "0123456789abcdef"[(h >> (4 * i)) & 15];
    name[n] = 0;
    int fd = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        size_t left = cur_len;
        const uint8_t *p = cur;
        while (left) {
            ssize_t w = write(fd, p, left);
            if (w <= 0)
                break;
            p += w;
            left -= (size_t)w;
        }
        close(fd);
    }
    const char msg[] = "fuzz: the failing input is in ";
    ssize_t w = write(2, msg, sizeof msg - 1);
    w = write(2, name, n);
    w = write(2, "\n", 1);
    (void)w;
}

static void on_crash(void)
{
    dump("crash");
}

static void on_signal(int signo)
{
    dump(signo == SIGALRM ? "hang" : "crash");
    signal(signo, SIG_DFL);
    signal(SIGABRT, SIG_DFL); /* a hang ends in SIGABRT: not a second report */
    raise(signo == SIGALRM ? SIGABRT : signo);
}

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define HAVE_ASAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define HAVE_ASAN 1
#endif
#ifdef HAVE_ASAN
void __sanitizer_set_death_callback(void (*callback)(void));
#endif

/* splitmix64 */
static uint64_t rng;
static uint64_t next(void)
{
    uint64_t z = (rng += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}
static size_t below(size_t n)
{
    return n ? (size_t)(next() % n) : 0;
}

typedef struct {
    uint8_t *data;
    size_t len;
} input;

static input *corpus;
static size_t corpus_len, corpus_cap;
static size_t max_len = 4096;
static const char *save_dir;

static void add(const uint8_t *d, size_t n)
{
    if (corpus_len == corpus_cap) {
        corpus_cap = corpus_cap ? corpus_cap * 2 : 64;
        corpus = realloc(corpus, corpus_cap * sizeof *corpus);
        if (!corpus)
            abort();
    }
    uint8_t *copy = malloc(n ? n : 1);
    if (!copy)
        abort();
    memcpy(copy, d, n);
    corpus[corpus_len++] = (input){copy, n};
}

static void run(const uint8_t *d, size_t n)
{
    cur = d;
    cur_len = n;
    alarm(HANG_SECONDS);
    LLVMFuzzerTestOneInput(d, n);
    alarm(0);
}

static bool read_file(const char *path, uint8_t **d, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    size_t cap = 4096, len = 0;
    uint8_t *buf = malloc(cap);
    size_t r;
    while (buf && (r = fread(buf + len, 1, cap - len, f)) > 0) {
        len += r;
        if (len == cap)
            buf = realloc(buf, cap *= 2);
    }
    fclose(f);
    if (!buf)
        return false;
    *d = buf;
    *n = len;
    return true;
}

static void seed(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "fuzz: %s: %s\n", path, strerror(errno));
        exit(2);
    }
    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        struct dirent *e;
        while (dir && (e = readdir(dir))) {
            if (e->d_name[0] == '.')
                continue;
            char p[4096];
            snprintf(p, sizeof p, "%s/%s", path, e->d_name);
            seed(p);
        }
        if (dir)
            closedir(dir);
        return;
    }
    uint8_t *d;
    size_t n;
    if (read_file(path, &d, &n)) {
        if (n > max_len)
            n = max_len;
        add(d, n);
        free(d);
    }
}

static void save(const uint8_t *d, size_t n)
{
    if (!save_dir)
        return;
    char p[4096];
    snprintf(p, sizeof p, "%s/cov-%zu-%llu", save_dir, corpus_len, (unsigned long long)(next() & 0xffffff));
    FILE *f = fopen(p, "wb");
    if (f) {
        fwrite(d, 1, n, f);
        fclose(f);
    }
}

static const uint8_t INTERESTING[] = {0, 1, 0x7f, 0x80, 0xff, '"', '\\', '{', '}', '[', ']',
                                      ',', ':', '\n', '\r', ' ', '0', '-', 0xc3, 0xe2, 0xf0};

/* One random change to buf[0..*n), which has room for max_len bytes. */
static void mutate(uint8_t *buf, size_t *n)
{
    size_t len = *n;
    switch (below(9)) {
    case 0: /* flip a bit */
        if (len)
            buf[below(len)] ^= (uint8_t)(1u << below(8));
        break;
    case 1: /* a random byte */
        if (len)
            buf[below(len)] = (uint8_t)next();
        break;
    case 2: /* an interesting byte */
        if (len)
            buf[below(len)] = INTERESTING[below(sizeof INTERESTING)];
        break;
    case 3: { /* insert random bytes */
        size_t k = 1 + below(8), at = below(len + 1);
        if (len + k > max_len)
            break;
        memmove(buf + at + k, buf + at, len - at);
        for (size_t i = 0; i < k; i++)
            buf[at + i] = (uint8_t)next();
        len += k;
        break;
    }
    case 4: { /* delete a range */
        if (!len)
            break;
        size_t at = below(len), k = 1 + below(len - at < 16 ? len - at : 16);
        memmove(buf + at, buf + at + k, len - at - k);
        len -= k;
        break;
    }
    case 5: { /* copy a range elsewhere */
        if (!len)
            break;
        size_t from = below(len), k = 1 + below(len - from), at = below(len + 1);
        if (len + k > max_len)
            break;
        uint8_t tmp[4096];
        if (k > sizeof tmp)
            break;
        memcpy(tmp, buf + from, k);
        memmove(buf + at + k, buf + at, len - at);
        memcpy(buf + at, tmp, k);
        len += k;
        break;
    }
    case 6: { /* a dictionary token */
        size_t count = 0;
        while (sk_fuzz_dict[count])
            count++;
        if (!count)
            break;
        const char *tok = sk_fuzz_dict[below(count)];
        size_t k = strlen(tok), at = below(len + 1);
        if (len + k > max_len)
            break;
        memmove(buf + at + k, buf + at, len - at);
        memcpy(buf + at, tok, k);
        len += k;
        break;
    }
    case 7: { /* splice another input's tail onto this one's head */
        const input *o = &corpus[below(corpus_len)];
        size_t keep = below(len + 1), from = below(o->len + 1), k = o->len - from;
        if (keep + k > max_len)
            k = max_len - keep;
        memcpy(buf + keep, o->data + from, k);
        len = keep + k;
        break;
    }
    case 8: /* truncate */
        len = below(len + 1);
        break;
    }
    *n = len;
}

int main(int argc, char **argv)
{
    double seconds = 30;
    long long runs = -1;
    bool replay = false;
    rng = (uint64_t)time(NULL) ^ ((uint64_t)getpid() << 32);
    int i = 1;
    for (; i < argc && argv[i][0] == '-'; i++) {
        const char *opt = argv[i];
        if (!strcmp(opt, "-x")) {
            replay = true;
            continue;
        }
        if (i + 1 >= argc) {
            fprintf(stderr, "fuzz: %s needs a value\n", opt);
            return 2;
        }
        const char *val = argv[++i];
        if (!strcmp(opt, "-t"))
            seconds = atof(val);
        else if (!strcmp(opt, "-r"))
            runs = atoll(val);
        else if (!strcmp(opt, "-s"))
            rng = strtoull(val, NULL, 10);
        else if (!strcmp(opt, "-m"))
            max_len = (size_t)atoll(val) > 4096 ? 4096 : (size_t)atoll(val);
        else if (!strcmp(opt, "-o"))
            save_dir = out_dir = val;
        else {
            fprintf(stderr, "fuzz: unknown option %s\n", opt);
            return 2;
        }
    }
#ifdef HAVE_ASAN
    __sanitizer_set_death_callback(on_crash);
#else
    (void)on_crash;
#endif
    int sigs[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT, SIGALRM};
    for (size_t s = 0; s < sizeof sigs / sizeof sigs[0]; s++)
        signal(sigs[s], on_signal);

    if (replay) {
        for (; i < argc; i++) {
            uint8_t *d;
            size_t n;
            if (!read_file(argv[i], &d, &n)) {
                fprintf(stderr, "fuzz: cannot read %s\n", argv[i]);
                return 2;
            }
            run(d, n);
            free(d);
            printf("fuzz: %s ran clean\n", argv[i]);
        }
        return 0;
    }

    for (; i < argc; i++)
        seed(argv[i]);
    if (!corpus_len)
        add((const uint8_t *)"", 0);
    for (size_t k = 0; k < corpus_len; k++)
        run(corpus[k].data, corpus[k].len);
    uint32_t covered = new_edges;
    new_edges = 0;

    static uint8_t buf[4096];
    time_t start = time(NULL);
    long long done = 0;
    while ((runs < 0 || done < runs) && difftime(time(NULL), start) < seconds) {
        for (int batch = 0; batch < 256 && (runs < 0 || done < runs); batch++, done++) {
            const input *base = &corpus[below(corpus_len)];
            size_t n = base->len > max_len ? max_len : base->len;
            memcpy(buf, base->data, n);
            for (size_t m = 1 + below(6); m > 0; m--)
                mutate(buf, &n);
            run(buf, n);
            if (new_edges) {
                covered += new_edges;
                new_edges = 0;
                add(buf, n);
                save(buf, n);
            }
        }
    }
    printf("fuzz: %lld runs, %zu inputs kept, %u of %u edges\n", done, corpus_len, covered, edges);
    return 0;
}
