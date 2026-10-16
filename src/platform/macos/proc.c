/* sk_proc_read from the kernel's process table, through sysctl. */
#include "../proc.h"

#include <sys/sysctl.h>
#include <sys/types.h>

bool sk_proc_read(pid_t pid, sk_proc_stat *out)
{
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, (int)pid};
    struct kinfo_proc kp;
    size_t len = sizeof kp;
    if (sysctl(mib, 4, &kp, &len, NULL, 0) != 0 || len != sizeof kp || kp.kp_proc.p_pid != pid)
        return false;
    out->ppid = kp.kp_eproc.e_ppid;
    out->start = (uint64_t)kp.kp_proc.p_starttime.tv_sec * 1000000u +
                 (uint64_t)kp.kp_proc.p_starttime.tv_usec;
    return true;
}
