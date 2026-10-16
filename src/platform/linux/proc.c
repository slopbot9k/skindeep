/* sk_proc_read from /proc/<pid>/stat, as app/relay/hook.py reads it. */
#include "../proc.h"

#include "../../base/str.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

bool sk_proc_read(pid_t pid, sk_proc_stat *out)
{
    char path[64], buf[1024];
    snprintf(path, sizeof path, "/proc/%ld/stat", (long)pid);
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return false;
    ssize_t n = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n <= 0)
        return false;
    buf[n] = 0;
    /* The command name in parentheses may hold spaces and parentheses,
     * so the fields start after the last ')'. */
    char *close_paren = strrchr(buf, ')');
    if (!close_paren || close_paren[1] != ' ')
        return false;
    /* Fields 3 (state) onwards; ppid is field 4, starttime field 22. */
    sk_str rest = sk_str_c(close_paren + 2);
    uint64_t ppid = 0, start = 0;
    for (int field = 3; field <= 22 && rest.len; field++) {
        size_t sp = sk_str_find(rest, ' ');
        sk_str tok = sk_str_sub(rest, 0, sp);
        if (field == 4 && !sk_str_to_u64(tok, &ppid))
            return false;
        if (field == 22 && !sk_str_to_u64(tok, &start))
            return false;
        rest = sp == SIZE_MAX ? sk_str_n(NULL, 0) : sk_str_sub(rest, sp + 1, SIZE_MAX);
    }
    out->ppid = (pid_t)ppid;
    out->start = start;
    return true;
}
