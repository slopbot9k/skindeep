/* procinfo.h from libproc and sysctl (decision 0036): proc_listallpids
 * for the table, KERN_PROC_PID for the parent, owner and start,
 * KERN_PROCARGS2 for the name and argv, PROC_PIDVNODEPATHINFO for the
 * folder. Ports what app/sessions.js reads of /proc. */
#include "../procinfo.h"

#include <libproc.h>
#include <string.h>
#include <sys/proc_info.h>
#include <sys/sysctl.h>

bool sk_procinfo_list(sk_procinfo_owners *out)
{
    int n = proc_listallpids(NULL, 0);
    if (n <= 0)
        return false;
    pid_t *pids = NULL;
    /* The table can grow between the two calls: room for more, and again
     * while it fills what was given. */
    for (size_t cap = (size_t)n + 64;; cap *= 2) {
        pids = sk_xreallocarray(pids, cap, sizeof *pids);
        n = proc_listallpids(pids, (int)(cap * sizeof *pids));
        if (n <= 0) {
            free(pids);
            return false;
        }
        if ((size_t)n < cap)
            break;
    }
    for (int i = 0; i < n; i++) {
        sk_procinfo info;
        if (pids[i] > 0 && sk_procinfo_read(pids[i], &info))
            sk_vec_push(out, ((sk_procinfo_owner){pids[i], info.uid}));
    }
    free(pids);
    return true;
}

bool sk_procinfo_read(pid_t pid, sk_procinfo *out)
{
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, (int)pid};
    struct kinfo_proc kp;
    size_t len = sizeof kp;
    if (sysctl(mib, 4, &kp, &len, NULL, 0) != 0 || len != sizeof kp || kp.kp_proc.p_pid != pid)
        return false;
    out->ppid = kp.kp_eproc.e_ppid;
    out->uid = kp.kp_eproc.e_ucred.cr_uid;
    out->start = (uint64_t)kp.kp_proc.p_starttime.tv_sec * 1000000u +
                 (uint64_t)kp.kp_proc.p_starttime.tv_usec;
    out->started_ms = out->start / 1000u;
    return true;
}

/* KERN_PROCARGS2 holds argc, the path exec was given, padding, argv,
 * then the environment. Only the path and argv are parsed. The buffer is
 * cleared before it is freed, so the environment, which can hold a key,
 * is never looked at nor left in memory. */
static bool procargs(pid_t pid, sk_buf *name, sk_strv *argv)
{
    int max_mib[2] = {CTL_KERN, KERN_ARGMAX};
    int argmax = 0;
    size_t len = sizeof argmax;
    if (sysctl(max_mib, 2, &argmax, &len, NULL, 0) != 0 || argmax <= 0)
        return false;
    /* A buffer smaller than the arguments gets their end, not their
     * start, so it is the whole of ARG_MAX. */
    char *buf = sk_xmalloc((size_t)argmax);
    int mib[3] = {CTL_KERN, KERN_PROCARGS2, (int)pid};
    len = (size_t)argmax;
    bool ok = sysctl(mib, 3, buf, &len, NULL, 0) == 0 && len > sizeof(int);
    if (ok) {
        int argc;
        memcpy(&argc, buf, sizeof argc);
        const char *p = buf + sizeof argc, *end = buf + len;
        const char *nul = memchr(p, 0, (size_t)(end - p));
        ok = nul != NULL && argc >= 0;
        if (ok && name) {
            const char *slash = nul;
            while (slash > p && slash[-1] != '/')
                slash--;
            sk_buf_append(name, slash, (size_t)(nul - slash));
        }
        if (ok && argv) {
            p = nul + 1;
            while (p < end && !*p)
                p++;
            for (int i = 0; i < argc && p < end; i++) {
                nul = memchr(p, 0, (size_t)(end - p));
                if (!nul)
                    break;
                sk_strv_push_n(argv, p, (size_t)(nul - p));
                p = nul + 1;
            }
        }
    }
    memset(buf, 0, SK_MIN(len, (size_t)argmax));
    free(buf);
    return ok;
}

bool sk_procinfo_name(pid_t pid, sk_buf *out)
{
    return procargs(pid, out, NULL);
}

bool sk_procinfo_argv(pid_t pid, sk_strv *out)
{
    return procargs(pid, NULL, out);
}

char *sk_procinfo_cwd(pid_t pid)
{
    struct proc_vnodepathinfo vpi;
    int n = proc_pidinfo(pid, PROC_PIDVNODEPATHINFO, 0, &vpi, sizeof vpi);
    if (n != (int)sizeof vpi || !vpi.pvi_cdir.vip_path[0])
        return NULL;
    return sk_xstrndup(vpi.pvi_cdir.vip_path, sizeof vpi.pvi_cdir.vip_path);
}
