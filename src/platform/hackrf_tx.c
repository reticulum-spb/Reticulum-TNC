#define _POSIX_C_SOURCE 200809L

#include "rtnc/hackrf_tx.h"

#include <libhackrf/hackrf.h>

#include <math.h>
#include <stdatomic.h>
#include <time.h>

typedef struct {
    const rtnc_hackrf_tx_config_t *config;
    const float                   *audio;
    size_t                         audio_samples;
    uint64_t                       lead_iq_samples;
    uint64_t                       waveform_iq_samples;
    uint64_t                       total_iq_samples;
    uint64_t                       generated_iq_samples;
    double                         phase;
    atomic_bool                    complete;
    atomic_bool                    flush_complete;
    atomic_bool                    flush_success;
} tx_context_t;

static void transmit_flush_callback(void *argument, int success) {
    tx_context_t *context = argument;
    if (context == NULL) {
        return;
    }
    atomic_store_explicit(&context->flush_success, success != 0, memory_order_relaxed);
    atomic_store_explicit(&context->flush_complete, true, memory_order_release);
}

void rtnc_hackrf_tx_default_config(rtnc_hackrf_tx_config_t *config) {
    if (config == NULL) {
        return;
    }
    config->frequency_hz = 446006250U;
    config->iq_sample_rate_hz = 2400000U;
    config->audio_sample_rate_hz = 48000U;
    config->deviation_hz = 2100.0F;
    config->lead_ms = 100U;
    config->tail_ms = 100U;
    config->txvga_gain_db = 20U;
    config->iq_amplitude = 100;
}

bool rtnc_hackrf_tx_config_is_valid(const rtnc_hackrf_tx_config_t *config) {
    return config != NULL && config->frequency_hz > 0U &&
           config->iq_sample_rate_hz >= 2000000U &&
           config->iq_sample_rate_hz <= 20000000U &&
           config->audio_sample_rate_hz > 0U &&
           (config->iq_sample_rate_hz % config->audio_sample_rate_hz) == 0U &&
           config->deviation_hz >= 500.0F && config->deviation_hz <= 4000.0F &&
           config->lead_ms <= 2000U && config->tail_ms <= 2000U &&
           config->txvga_gain_db <= 47U && config->iq_amplitude > 0 &&
           config->iq_amplitude <= 120;
}

static int transmit_callback(hackrf_transfer *transfer) {
    const double  pi = 3.14159265358979323846;
    tx_context_t *context = transfer != NULL ? transfer->tx_ctx : NULL;
    uint64_t      remaining;
    size_t        capacity_samples;
    size_t        output_samples;
    size_t        index;
    if (context == NULL || transfer->buffer == NULL ||
        transfer->buffer_length <= 0 ||
        atomic_load_explicit(&context->complete, memory_order_acquire)) {
        return -1;
    }
    remaining = context->total_iq_samples - context->generated_iq_samples;
    capacity_samples = (size_t) transfer->buffer_length / 2U;
    output_samples = remaining < (uint64_t) capacity_samples
                         ? (size_t) remaining
                         : capacity_samples;
    for (index = 0U; index < output_samples; ++index) {
        const uint64_t position = context->generated_iq_samples + index;
        float          sample = 0.0F;
        if (position >= context->lead_iq_samples &&
            position < context->lead_iq_samples +
                           context->waveform_iq_samples) {
            const uint64_t waveform_position = position - context->lead_iq_samples;
            const size_t   audio_index = (size_t) (waveform_position * context->config->audio_sample_rate_hz /
                                                 context->config->iq_sample_rate_hz);
            sample = context->audio[audio_index];
            if (sample > 1.0F) {
                sample = 1.0F;
            } else if (sample < -1.0F) {
                sample = -1.0F;
            }
        }
        context->phase +=
            2.0 * pi * (double) context->config->deviation_hz * (double) sample /
            (double) context->config->iq_sample_rate_hz;
        if (context->phase > pi) {
            context->phase -= 2.0 * pi;
        } else if (context->phase < -pi) {
            context->phase += 2.0 * pi;
        }
        transfer->buffer[2U * index] = (uint8_t) (int8_t) lrint(
            (double) context->config->iq_amplitude * cos(context->phase)
        );
        transfer->buffer[2U * index + 1U] = (uint8_t) (int8_t) lrint(
            (double) context->config->iq_amplitude * sin(context->phase)
        );
    }
    transfer->valid_length = (int) (output_samples * 2U);
    context->generated_iq_samples += output_samples;
    if (context->generated_iq_samples >= context->total_iq_samples) {
        atomic_store_explicit(&context->complete, true, memory_order_release);
    }
    return 0;
}

bool rtnc_hackrf_tx_audio(const rtnc_hackrf_tx_config_t *config, const float *audio, size_t audio_samples, uint64_t *iq_samples_sent) {
    const struct timespec polling = { .tv_sec = 0, .tv_nsec = 10000000L };
    hackrf_device        *device = NULL;
    tx_context_t          context;
    bool                  library_initialized = false;
    bool                  streaming_started = false;
    bool                  success = false;
    if (!rtnc_hackrf_tx_config_is_valid(config) || audio == NULL ||
        audio_samples == 0U || iq_samples_sent == NULL) {
        return false;
    }
    *iq_samples_sent = 0U;
    context.config = config;
    context.audio = audio;
    context.audio_samples = audio_samples;
    context.lead_iq_samples =
        (uint64_t) config->iq_sample_rate_hz * config->lead_ms / 1000U;
    context.waveform_iq_samples =
        (uint64_t) audio_samples * config->iq_sample_rate_hz /
        config->audio_sample_rate_hz;
    context.total_iq_samples =
        context.lead_iq_samples + context.waveform_iq_samples +
        (uint64_t) config->iq_sample_rate_hz * config->tail_ms / 1000U;
    context.generated_iq_samples = 0U;
    context.phase = 0.0;
    atomic_init(&context.complete, false);
    atomic_init(&context.flush_complete, false);
    atomic_init(&context.flush_success, false);
    if (hackrf_init() != HACKRF_SUCCESS) {
        goto done;
    }
    library_initialized = true;
    if (hackrf_open(&device) != HACKRF_SUCCESS ||
        hackrf_set_sample_rate(device, (double) config->iq_sample_rate_hz) !=
            HACKRF_SUCCESS ||
        hackrf_set_freq(device, config->frequency_hz) != HACKRF_SUCCESS ||
        hackrf_set_amp_enable(device, 0U) != HACKRF_SUCCESS ||
        hackrf_set_txvga_gain(device, config->txvga_gain_db) != HACKRF_SUCCESS ||
        hackrf_enable_tx_flush(device, transmit_flush_callback, &context) !=
            HACKRF_SUCCESS ||
        hackrf_start_tx(device, transmit_callback, &context) != HACKRF_SUCCESS) {
        goto done;
    }
    streaming_started = true;
    {
        unsigned int   polls = 0U;
        const uint64_t expected_ms =
            context.total_iq_samples * UINT64_C(1000) /
            config->iq_sample_rate_hz;
        const uint64_t required_polls = expected_ms / UINT64_C(10) +
                                        UINT64_C(500);
        const unsigned int poll_limit =
            required_polls < UINT32_MAX ? (unsigned int) required_polls
                                        : UINT32_MAX;
        while (!atomic_load_explicit(&context.flush_complete, memory_order_acquire) &&
               polls < poll_limit) {
            (void) nanosleep(&polling, NULL);
            polls += 1U;
        }
    }
    if (atomic_load_explicit(&context.flush_complete, memory_order_acquire) &&
        atomic_load_explicit(&context.flush_success, memory_order_relaxed) &&
        atomic_load_explicit(&context.complete, memory_order_acquire)) {
        success = true;
    }

done:
    if (streaming_started) {
        (void) hackrf_stop_tx(device);
    }
    if (device != NULL) {
        (void) hackrf_close(device);
    }
    if (library_initialized) {
        (void) hackrf_exit();
    }
    *iq_samples_sent = context.generated_iq_samples;
    return success;
}
