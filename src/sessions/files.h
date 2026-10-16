/* The few file operations the sessions, the profiles and the boards
 * share, done as Node does them where the order or the result shows. */
#ifndef SK_FILES_H
#define SK_FILES_H

#include "../base/strv.h"

/* The names in a directory but . and .., sorted by strcmp as
 * fs.readdirSync sorts them (libuv's scandir). False when it cannot be
 * read. */
bool sk_files_list(const char *dir, sk_strv *names);
/* The whole of a regular file, at most cap bytes; false when it cannot
 * be read or is larger. */
bool sk_files_read(const char *path, size_t cap, sk_buf *out);
/* stat's mtime in ms with its fraction, as Node's mtimeMs, links
 * followed; false when path is gone. */
bool sk_files_mtime_ms(const char *path, double *ms);
/* True when path is a directory, links followed. */
bool sk_files_is_dir(const char *path);
/* path.resolve of one path: absolute against the working directory,
 * with ., .., repeated and trailing slashes taken out. malloc'd. */
char *sk_files_resolve(const char *path);

#endif
