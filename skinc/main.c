/* skinc: the skin compiler of decision 0036. For now the front end of
 * decision 0037: it checks a skin folder as app/skins/validate.js
 * does, parses and checks its CSS, builds its element tree and
 * computes every element's style in every state it can reach, then
 * prints a summary.
 *
 *   skinc [-v] DIR
 *
 * Errors go to stderr, one a line, `<file>: <field or line N>: <what>`;
 * the exit status is then 1. -v prints every element's states and the
 * values its styles hold. */
#include "front.h"

#include <stdio.h>
#include <string.h>

static int usage(void)
{
    fprintf(stderr, "usage: skinc [-v] DIR\n");
    return 2;
}

int main(int argc, char **argv)
{
    bool verbose = false;
    const char *dir = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v"))
            verbose = true;
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
    for (size_t i = 0; i < d.items.len; i++)
        fprintf(stderr, "%s\n", d.items.ptr[i]);
    if (ok)
        sk_front_summary(&f, stdout, verbose);
    sk_front_free(&f);
    sk_diags_free(&d);
    return ok ? 0 : 1;
}
