/* The sessions scan's Codex lookup (app/sessions.js codexRollout) on the
 * real discovery: one Discovery per lookup, as the JS makes one. */
#include "../../sessions/sessions.h"
#include "codex.h"

bool sk_codex_rollout_find(void *ud, pid_t pid, const char *codex_home, char *const *args,
                           const char *cwd, double launched, sk_codex_rollout *out)
{
    (void)ud;
    sk_codex_disc *d = sk_codex_disc_new(codex_home, args, cwd, launched);
    const sk_codex_found *f = sk_codex_disc_find(d, pid);
    bool hit = f && f->file;
    if (hit) {
        out->file = sk_xstrdup(f->file);
        out->thread = f->thread ? sk_xstrdup(f->thread) : NULL;
        out->how = f->how;
    }
    sk_codex_disc_free(d);
    return hit;
}
