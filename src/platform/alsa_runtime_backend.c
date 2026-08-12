#define _POSIX_C_SOURCE 200809L

#include "rtnc/alsa_runtime_backend.h"

#include <errno.h>
#include <string.h>
#include <time.h>

static bool backend_set_ptt(void *opaque, bool enabled) {
    rtnc_alsa_runtime_context_t *context = opaque;
    return rtnc_ptt_set(context->ptt, enabled);
}

static bool backend_send_audio(void *opaque, const int16_t *samples, size_t count) {
    rtnc_alsa_runtime_context_t *context = opaque;
    return rtnc_audio_send(context->audio, samples, count);
}

static bool backend_wait_audio(void *opaque) {
    rtnc_alsa_runtime_context_t *context = opaque;
    return rtnc_audio_wait(context->audio);
}

static bool backend_channel_busy(void *opaque) {
    const rtnc_alsa_runtime_context_t *context = opaque;
    return context->channel_busy != NULL &&
           atomic_load_explicit(context->channel_busy, memory_order_acquire);
}

static bool backend_sleep(void *opaque, uint16_t milliseconds) {
    struct timespec delay = {
        .tv_sec = (time_t) (milliseconds / 1000U),
        .tv_nsec = (long) (milliseconds % 1000U) * 1000000L,
    };
    (void) opaque;
    while (nanosleep(&delay, &delay) < 0) {
        if (errno != EINTR) {
            return false;
        }
    }
    return true;
}

static uint64_t backend_now_ms(void *opaque) {
    struct timespec value = { 0 };
    (void) opaque;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
        return 0U;
    }
    return (uint64_t) value.tv_sec * 1000U +
           (uint64_t) value.tv_nsec / 1000000U;
}

bool rtnc_alsa_runtime_backend(rtnc_alsa_runtime_context_t *context, rtnc_audio_t *audio, rtnc_ptt_t *ptt, const atomic_bool *channel_busy, rtnc_runtime_backend_t *backend) {
    if (context == NULL || audio == NULL || ptt == NULL || backend == NULL) {
        return false;
    }
    context->audio = audio;
    context->ptt = ptt;
    context->channel_busy = channel_busy;
    (void) memset(backend, 0, sizeof(*backend));
    backend->context = context;
    backend->set_ptt = backend_set_ptt;
    backend->send_audio = backend_send_audio;
    backend->wait_audio = backend_wait_audio;
    backend->channel_busy = backend_channel_busy;
    backend->sleep_ms = backend_sleep;
    backend->now_ms = backend_now_ms;
    return true;
}
