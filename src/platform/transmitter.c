#define _POSIX_C_SOURCE 200809L

#include "rtnc/transmitter.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

static int16_t clamp_sample(float value) {
    if (value >= 32767.0F) {
        return 32767;
    }
    if (value <= -32768.0F) {
        return -32768;
    }
    return (int16_t) lrintf(value);
}

static bool condition_audio(const rtnc_tx_config_t *config, const int16_t *input, int16_t *output, size_t count) {
    size_t index;
    if (config == NULL || input == NULL || output == NULL || count == 0U ||
        config->filter_gain <= 0.0F) {
        return false;
    }
    for (index = 0U; index < count; ++index) {
        const float filtered = rtnc_tx_eq_apply_sample(
            config->response_eq_taps,
            input,
            count,
            index
        );
        output[index] = clamp_sample(config->filter_gain * filtered);
    }
    return true;
}

static bool sleep_milliseconds(uint16_t milliseconds) {
    struct timespec delay = {
        .tv_sec = (time_t) (milliseconds / 1000U),
        .tv_nsec = (long) (milliseconds % 1000U) * 1000000L,
    };
    while (nanosleep(&delay, &delay) < 0) {
        if (errno != EINTR) {
            return false;
        }
    }
    return true;
}

bool rtnc_transmit_audio(rtnc_audio_t *audio, rtnc_ptt_t *ptt, const rtnc_tx_config_t *config, const int16_t *samples, size_t count) {
    bool     success;
    int16_t *conditioned = NULL;
    if (audio == NULL || ptt == NULL || config == NULL || samples == NULL ||
        count == 0U) {
        return false;
    }
    conditioned = calloc(count, sizeof(*conditioned));
    if (conditioned == NULL ||
        !condition_audio(config, samples, conditioned, count) ||
        !rtnc_ptt_set(ptt, true)) {
        free(conditioned);
        return false;
    }
    success = sleep_milliseconds(config->lead_ms) &&
              rtnc_audio_send(audio, conditioned, count) &&
              rtnc_audio_wait(audio) &&
              sleep_milliseconds(config->tail_ms);
    if (!rtnc_ptt_set(ptt, false)) {
        success = false;
    }
    free(conditioned);
    return success;
}
