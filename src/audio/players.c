/* The command lines of Linux's players (platform/audio.h), from their
 * manuals: pw-cat(1)'s --raw reads raw samples, here from stdin ("-"),
 * in its native-endian s16; pacat(1) reads stdin when no file is named,
 * with --raw and a format of explicit byte order, and --latency-msec
 * keeps its buffer short; aplay(1) takes -t raw with ALSA's format name
 * and reads stdin from "-". */
#include "../platform/audio.h"

#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *const names[SK_AUDIO_PLAYERS] = {"pw-play", "paplay", "aplay"};

const char *sk_audio_player_name(sk_audio_player p)
{
    return (unsigned)p < SK_AUDIO_PLAYERS ? names[p] : "?";
}

void sk_audio_player_argv(sk_strv *out, sk_audio_player p, const char *path, uint32_t rate,
                          unsigned channels, bool big_endian)
{
    sk_strv_push(out, path);
    switch (p) {
    case SK_AUDIO_PW_PLAY:
        sk_strv_push(out, "--raw");
        sk_strv_push(out, "--format=s16");
        sk_strv_pushf(out, "--rate=%u", (unsigned)rate);
        sk_strv_pushf(out, "--channels=%u", channels);
        sk_strv_push(out, "-");
        break;
    case SK_AUDIO_PAPLAY:
        sk_strv_push(out, "--raw");
        sk_strv_push(out, big_endian ? "--format=s16be" : "--format=s16le");
        sk_strv_pushf(out, "--rate=%u", (unsigned)rate);
        sk_strv_pushf(out, "--channels=%u", channels);
        sk_strv_pushf(out, "--latency-msec=%u", SK_AUDIO_LEAD_MS);
        sk_strv_push(out, "--client-name=skinned");
        break;
    case SK_AUDIO_APLAY:
    default:
        sk_strv_push(out, "-q");
        sk_strv_push(out, "-t");
        sk_strv_push(out, "raw");
        sk_strv_push(out, "-f");
        sk_strv_push(out, big_endian ? "S16_BE" : "S16_LE");
        sk_strv_push(out, "-r");
        sk_strv_pushf(out, "%u", (unsigned)rate);
        sk_strv_push(out, "-c");
        sk_strv_pushf(out, "%u", channels);
        sk_strv_push(out, "-");
        break;
    }
}

/* An executable regular file, as execvp would run it. */
static bool runnable(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode) && access(path, X_OK) == 0;
}

size_t sk_audio_player_find(const char *search_path, sk_audio_player found[SK_AUDIO_PLAYERS],
                            sk_strv *paths)
{
    size_t n = 0;
    for (unsigned p = 0; p < SK_AUDIO_PLAYERS; p++) {
        const char *dir = search_path ? search_path : "";
        for (;;) {
            const char *end = strchr(dir, ':');
            size_t len = end ? (size_t)(end - dir) : strlen(dir);
            bool ok = false;
            /* A relative entry would run whatever the working directory
             * holds when a sound plays. */
            if (len && dir[0] == '/') {
                sk_buf file = {0};
                sk_buf_printf(&file, "%.*s/%s", (int)len, dir, names[p]);
                ok = runnable(file.ptr);
                if (ok) {
                    found[n++] = (sk_audio_player)p;
                    sk_strv_push(paths, file.ptr);
                }
                sk_buf_free(&file);
            }
            if (ok || !end)
                break;
            dir = end + 1;
        }
    }
    return n;
}
