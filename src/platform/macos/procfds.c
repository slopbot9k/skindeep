/* sk_proc_open_files through libproc: the process's descriptors, then the
 * path of each one that is a vnode. */
#include "../procfds.h"

#include <libproc.h>
#include <stdio.h>
#include <string.h>
#include <sys/proc_info.h>

typedef struct {
    char name[16]; /* the descriptor's number as a string, to sort by */
    char *path;
} open_file;

static int by_name(const void *a, const void *b)
{
    return strcmp(((const open_file *)a)->name, ((const open_file *)b)->name);
}

void sk_proc_open_files(pid_t pid, sk_strv *paths)
{
    int size = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, NULL, 0);
    if (size <= 0)
        return;
    /* Room for descriptors opened between the two calls. */
    size_t cap = (size_t)size / sizeof(struct proc_fdinfo) + 16;
    struct proc_fdinfo *fds = sk_xcalloc(cap, sizeof *fds);
    int got = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, fds, (int)(cap * sizeof *fds));
    size_t n = got > 0 ? (size_t)got / sizeof *fds : 0;
    SK_VEC(open_file) files = {0};
    for (size_t i = 0; i < n; i++) {
        if (fds[i].proc_fdtype != PROX_FDTYPE_VNODE)
            continue;
        struct vnode_fdinfowithpath vi;
        int r = proc_pidfdinfo(pid, fds[i].proc_fd, PROC_PIDFDVNODEPATHINFO, &vi, (int)sizeof vi);
        if (r <= 0 || (size_t)r != sizeof vi || !vi.pvip.vip_path[0])
            continue;
        open_file f;
        snprintf(f.name, sizeof f.name, "%d", (int)fds[i].proc_fd);
        f.path = sk_xstrndup(vi.pvip.vip_path, sizeof vi.pvip.vip_path);
        sk_vec_push(&files, f);
    }
    if (files.len)
        qsort(files.ptr, files.len, sizeof *files.ptr, by_name);
    for (size_t i = 0; i < files.len; i++) {
        sk_strv_push(paths, files.ptr[i].path);
        free(files.ptr[i].path);
    }
    sk_vec_free(&files);
    free(fds);
}

double sk_stat_mtime_ms(const struct stat *st)
{
    return (double)st->st_mtimespec.tv_sec * 1000 + (double)st->st_mtimespec.tv_nsec / 1e6;
}
