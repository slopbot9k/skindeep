/* skinc: the skin compiler of decisions 0036 and 0037. It checks a skin
 * folder as app/skins/validate.js does, parses and checks its CSS,
 * builds its element tree and computes every element's style in every
 * state it can reach; then it prints a summary, or writes the pack.
 *
 *   skinc [-v] DIR             check DIR and print a summary
 *   skinc [-v] DIR -o PACK     write DIR's pack to PACK
 *
 * Errors go to stderr, one a line, `<file>: <field or line N>: <what>`;
 * the exit status is then 1. -v prints every element's states and the
 * values its styles hold, and a pack's sections. A pack is written to a new file in PACK's
 * folder, then renamed to PACK, so no reader sees half of one. */
#include "../src/skin/compile.h"
#include "front.h"
#include "write.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int usage(void)
{
    fprintf(stderr, "usage: skinc [-v] DIR [-o PACK]\n");
    return 2;
}

/* n bytes to path, by way of a temporary file beside it. */
static bool save(const char *path, const void *data, size_t n)
{
    const char *slash = strrchr(path, '/');
    sk_buf tmp = {0};
    if (slash)
        sk_buf_printf(&tmp, "%.*s/.%s.XXXXXX", (int)(slash - path), path, slash + 1);
    else
        sk_buf_printf(&tmp, ".%s.XXXXXX", path);
    int fd = mkstemp(tmp.ptr);
    if (fd < 0) {
        fprintf(stderr, "skinc: cannot write %s: %s\n", path, strerror(errno));
        sk_buf_free(&tmp);
        return false;
    }
    mode_t mask = umask(0);
    umask(mask);
    bool ok = fchmod(fd, 0666 & ~mask) == 0;
    const char *p = data;
    size_t left = n;
    while (ok && left) {
        ssize_t w = write(fd, p, left);
        if (w < 0 && errno == EINTR)
            continue;
        ok = w > 0;
        if (ok) {
            p += w;
            left -= (size_t)w;
        }
    }
    int e = errno;
    if (close(fd) != 0 && ok) {
        ok = false;
        e = errno;
    }
    if (ok && rename(tmp.ptr, path) != 0) {
        ok = false;
        e = errno;
    }
    if (!ok) {
        fprintf(stderr, "skinc: cannot write %s: %s\n", path, strerror(e));
        unlink(tmp.ptr);
    }
    sk_buf_free(&tmp);
    return ok;
}

int main(int argc, char **argv)
{
    bool verbose = false;
    const char *dir = NULL, *pack = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v"))
            verbose = true;
        else if (!strcmp(argv[i], "-o") && i + 1 < argc && !pack)
            pack = argv[++i];
        else if (argv[i][0] == '-' || dir)
            return usage();
        else
            dir = argv[i];
    }
    if (!dir)
        return usage();
    sk_diags d = {0};
    sk_front f;
    bool ok = sk_front_run(&f, dir, &d);
    sk_buf out = {0};
    if (ok && pack) {
        unsigned char hash[32];
        char why[512];
        if (!sk_skin_hash(dir, hash, why, sizeof why)) {
            fprintf(stderr, "skinc: %s\n", why);
            ok = false;
        } else {
            ok = sk_pack_write(&f, hash, &out, &d);
        }
    }
    for (size_t i = 0; i < d.items.len; i++)
        fprintf(stderr, "%s\n", d.items.ptr[i]);
    if (ok && pack)
        ok = save(pack, out.ptr, out.len);
    if (ok && (!pack || verbose))
        sk_front_summary(&f, stdout, verbose);
    if (ok && pack && verbose) {
        sk_pack_print_sections((const uint8_t *)out.ptr, out.len, stdout);
        sk_pack_print_layers(&f, (const uint8_t *)out.ptr, out.len, stdout);
    }
    sk_buf_free(&out);
    sk_front_free(&f);
    sk_diags_free(&d);
    return ok ? 0 : 1;
}
