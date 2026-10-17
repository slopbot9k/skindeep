#include "log.h"

#include "str.h"

#include <stdatomic.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static atomic_int log_fd = 2;

void sk_log_set_fd(int fd)
{
    atomic_store(&log_fd, fd);
}

void sk_log_now(const char *a, const char *b)
{
    struct timespec ts;
    struct tm tm;
    clock_gettime(CLOCK_REALTIME, &ts);
    gmtime_r(&ts.tv_sec, &tm);
    char line[640];
    int n = snprintf(line, sizeof line, "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ skinned: %s%s\n", tm.tm_year + 1900,
                     tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000, a, b);
    if (n <= 0)
        return;
    size_t len = SK_MIN((size_t)n, sizeof line - 1);
    line[len - 1] = '\n';
    int fd = atomic_load(&log_fd);
    for (size_t done = 0; done < len;) {
        ssize_t w = write(fd, line + done, len - done);
        if (w <= 0)
            break;
        done += (size_t)w;
    }
}

void sk_log(const char *fmt, ...)
{
    struct timespec ts;
    struct tm tm;
    clock_gettime(CLOCK_REALTIME, &ts);
    gmtime_r(&ts.tv_sec, &tm);
    sk_buf line = {0};
    sk_buf_printf(&line, "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ skinned: ", tm.tm_year + 1900,
                  tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                  ts.tv_nsec / 1000000);
    va_list ap;
    va_start(ap, fmt);
    sk_buf_vprintf(&line, fmt, ap);
    va_end(ap);
    sk_buf_putc(&line, '\n');
    const char *p = line.ptr;
    size_t left = line.len;
    int fd = atomic_load(&log_fd);
    while (left) {
        ssize_t w = write(fd, p, left);
        if (w <= 0)
            break;
        p += w;
        left -= (size_t)w;
    }
    sk_buf_free(&line);
}
