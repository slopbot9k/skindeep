/* What POSIX names one way and macOS another, for files. */
#ifndef SK_FILE_H
#define SK_FILE_H

#include <sys/stat.h>

/* st's last modification in ms since the epoch, with its fraction, as
 * Node's mtimeMs: POSIX's st_mtim is st_mtimespec on macOS. */
double sk_stat_mtime_ms(const struct stat *st);

#endif
