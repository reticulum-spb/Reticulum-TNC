#include "rtnc/modem.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static uint32_t random_state = 0x6d2b79f5U;

static float uniform_open(void) {
    random_state = random_state * 1664525U + 1013904223U;
    return ((float) (random_state >> 8U) + 0.5F) / 16777216.0F;
}

static float gaussian(void) {
    const float magnitude = sqrtf(-2.0F * logf(uniform_open()));
    return magnitude * cosf(2.0F * (float) M_PI * uniform_open());
}

static void run_case(float phase_radians, float cfo_hz, float noise_sigma, size_t prefix_samples) {
    const uint8_t input[] = {
        0x00U,
        0x11U,
        0x22U,
        0x33U,
        0x44U,
        0x55U,
        0x66U,
        0x77U,
        0x88U,
        0x99U,
        0xaaU,
        0xbbU,
        0xccU,
        0xddU,
        0xeeU,
        0xffU,
    };
    static float                  audio[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    static rtnc_modem_workspace_t workspace;
    uint8_t                       output[RTNC_FRAME_MAX_PAYLOAD];
    rtnc_modem_t                  modem;
    rtnc_sync_metrics_t           metrics;
    size_t                        sample_count = 0U;
    size_t                        output_length = 0U;
    size_t                        index;

    (void) memset(audio, 0, sizeof(audio));
    assert(rtnc_modem_init(&modem));
    assert(rtnc_carrier_set_tx_phase(&modem.carrier, phase_radians));
    assert(rtnc_carrier_set_tx_frequency_offset(&modem.carrier, cfo_hz));
    assert(rtnc_modem_tx_audio(&modem, input, sizeof(input), &audio[prefix_samples], RTNC_MODEM_MAX_AUDIO_SAMPLES - prefix_samples, &sample_count) == RTNC_MODEM_OK);
    sample_count += prefix_samples;
    for (index = 0U; index < sample_count; ++index) {
        audio[index] += noise_sigma * gaussian();
    }
    assert(rtnc_modem_rx_audio(&modem, audio, sample_count, output, sizeof(output), &output_length, &metrics, &workspace) == RTNC_MODEM_OK);
    assert(output_length == sizeof(input));
    assert(memcmp(input, output, sizeof(input)) == 0);
    assert(fabsf(metrics.carrier_offset_hz - cfo_hz) < 0.35F);
    assert(fabsf(metrics.timing_symbols - (float) prefix_samples / 40.0F) <= 0.026F);
    rtnc_modem_deinit(&modem);
}

int main(void) {
    static const float  phases[] = { -2.4F, -0.7F, 0.85F, 2.6F };
    static const float  offsets_hz[] = { -8.0F, -4.0F, 0.0F, 4.0F, 8.0F };
    static const float  noise_sigmas[] = { 0.01F, 0.02F, 0.03F, 0.04F };
    static const size_t sample_offsets[] = { 0U, 1U, 19U, 39U, 73U };
    size_t              index;

    for (index = 0U; index < sizeof(phases) / sizeof(phases[0]); ++index) {
        run_case(phases[index], 0.0F, 0.0F, 0U);
    }
    for (index = 0U; index < sizeof(offsets_hz) / sizeof(offsets_hz[0]);
         ++index) {
        run_case(0.2F, offsets_hz[index], 0.0F, 0U);
    }
    for (index = 0U; index < sizeof(noise_sigmas) / sizeof(noise_sigmas[0]);
         ++index) {
        random_state = 0x6d2b79f5U + (uint32_t) index;
        run_case(0.35F, 4.0F, noise_sigmas[index], 0U);
    }
    for (index = 0U; index < sizeof(sample_offsets) / sizeof(sample_offsets[0]);
         ++index) {
        run_case(0.0F, 0.0F, 0.0F, sample_offsets[index]);
    }
    return 0;
}
