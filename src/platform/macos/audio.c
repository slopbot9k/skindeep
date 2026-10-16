/* platform/audio.h on macOS (decision 0036): an AudioToolbox AudioQueue
 * of BUFFERS buffers of SK_AUDIO_PERIOD frames, about 64 ms at 48 kHz.
 * Its callback runs on one of the queue's own threads, pulls the mixer
 * into the buffer it is handed and queues it again; it takes no lock.
 * The output's thread starts the queue when the sounds start and pauses
 * it after a silence. A paused queue keeps its buffers, so the next
 * start resumes it; only the first start fills them. AudioQueue
 * converts 48 kHz to the device's own rate. */
#include "../audio.h"

#include "../../base/log.h"

#include <AudioToolbox/AudioQueue.h>
#include <stdatomic.h>

#define BUFFERS 3

typedef struct {
    sk_audio *a;
    AudioQueueRef q;
    AudioQueueBufferRef bufs[BUFFERS];
    bool primed;
    bool told; /* that the queue would not start */
    atomic_bool closing;
} aq_dev;

static void fill(void *ud, AudioQueueRef q, AudioQueueBufferRef b)
{
    aq_dev *d = ud;
    /* A stop calls back with the buffers it returns. */
    if (atomic_load_explicit(&d->closing, memory_order_acquire))
        return;
    UInt32 frame_bytes = SK_MIXER_CHANNELS * sizeof(int16_t);
    UInt32 frames = b->mAudioDataBytesCapacity / frame_bytes;
    sk_audio_pull(d->a, b->mAudioData, frames);
    b->mAudioDataByteSize = frames * frame_bytes;
    AudioQueueEnqueueBuffer(q, b, 0, NULL);
}

static bool aq_start(void *dev)
{
    aq_dev *d = dev;
    if (!d->primed) {
        for (size_t i = 0; i < BUFFERS; i++)
            fill(d, d->q, d->bufs[i]);
        d->primed = true;
    }
    OSStatus st = AudioQueueStart(d->q, NULL);
    if (st != noErr) {
        if (!d->told)
            sk_log("skin sounds: the AudioQueue would not start (%d): silent until the sounds next start",
                   (int)st);
        d->told = true;
        return false;
    }
    d->told = false;
    return true;
}

static void aq_stop(void *dev)
{
    aq_dev *d = dev;
    AudioQueuePause(d->q);
}

static void aq_free(void *dev)
{
    aq_dev *d = dev;
    atomic_store_explicit(&d->closing, true, memory_order_release);
    AudioQueueStop(d->q, true);
    AudioQueueDispose(d->q, true);
    free(d);
}

bool sk_audio_device_open(sk_audio *a, uint32_t rate, sk_audio_device *out)
{
    aq_dev *d = sk_xcalloc(1, sizeof *d);
    d->a = a;
    atomic_init(&d->closing, false);
    UInt32 frame_bytes = SK_MIXER_CHANNELS * sizeof(int16_t);
    AudioStreamBasicDescription f = {
        .mSampleRate = rate,
        .mFormatID = kAudioFormatLinearPCM,
        .mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked |
                        kAudioFormatFlagsNativeEndian,
        .mBytesPerPacket = frame_bytes,
        .mFramesPerPacket = 1,
        .mBytesPerFrame = frame_bytes,
        .mChannelsPerFrame = SK_MIXER_CHANNELS,
        .mBitsPerChannel = 16,
    };
    OSStatus st = AudioQueueNewOutput(&f, fill, d, NULL, NULL, 0, &d->q);
    if (st != noErr) {
        sk_log("skin sounds: no AudioQueue (%d): they are silent", (int)st);
        free(d);
        return false;
    }
    for (size_t i = 0; i < BUFFERS; i++) {
        st = AudioQueueAllocateBuffer(d->q, SK_AUDIO_PERIOD * frame_bytes, &d->bufs[i]);
        if (st != noErr) {
            sk_log("skin sounds: no AudioQueue buffer (%d): they are silent", (int)st);
            AudioQueueDispose(d->q, true);
            free(d);
            return false;
        }
    }
    *out = (sk_audio_device){
        .name = "AudioQueue",
        .dev = d,
        .start = aq_start,
        .stop = aq_stop,
        .free = aq_free,
    };
    return true;
}
