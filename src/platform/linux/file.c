#include "../file.h"

double sk_stat_mtime_ms(const struct stat *st)
{
    return (double)st->st_mtim.tv_sec * 1000 + (double)st->st_mtim.tv_nsec / 1e6;
}
