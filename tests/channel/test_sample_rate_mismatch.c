#include "rtnc/modem.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static size_t resample_clock(const float *input, size_t input_count, float *output, size_t capacity, float ppm) {
    const double ratio = 1.0 + (double) ppm * 1.0e-6;
    size_t       output_count = 0U;
    double       position = 0.0;
    while (position + 1.0 < (double) input_count && output_count < capacity) {
        const size_t lower = (size_t) position;
        const float  fraction = (float) (position - (double) lower);
        output[output_count] = input[lower] * (1.0F - fraction) +
                               input[lower + 1U] * fraction;
        ++output_count;
        position += ratio;
    }
    return output_count;
}

static bool run_case(fec_mode_t mode, uint8_t payload_class, float ppm, float *timing_symbols) {
    static float                  transmitted[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    static float                  received[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    static rtnc_modem_workspace_t workspace;
    uint8_t                       payload[RTNC_FRAME_MAX_PAYLOAD];
    uint8_t                       decoded[RTNC_FRAME_MAX_PAYLOAD];
    rtnc_modem_t                  modem;
    rtnc_sync_metrics_t           metrics;
    size_t                        transmitted_count = 0U;
    size_t                        received_count;
    size_t                        decoded_length = 0U;
    size_t                        index;
    bool                          success;

    for (index = 0U; index < payload_class; ++index) {
        payload[index] = (uint8_t) (index * 13U + 0x37U);
    }
    assert(rtnc_modem_init_config(&modem, mode, payload_class));
    assert(rtnc_modem_tx_audio(&modem, payload, payload_class, transmitted, RTNC_MODEM_MAX_AUDIO_SAMPLES, &transmitted_count) == RTNC_MODEM_OK);
    received_count = resample_clock(transmitted, transmitted_count, received, RTNC_MODEM_MAX_AUDIO_SAMPLES, ppm);
    success = rtnc_modem_rx_audio(&modem, received, received_count, decoded, sizeof(decoded), &decoded_length, &metrics, &workspace) == RTNC_MODEM_OK &&
              decoded_length == payload_class &&
              memcmp(payload, decoded, payload_class) == 0;
    *timing_symbols = metrics.timing_symbols;
    rtnc_modem_deinit(&modem);
    return success;
}

int main(void) {
    static const float ppm_points[] = {
        -1000.0F,
        -500.0F,
        -200.0F,
        -100.0F,
        -50.0F,
        0.0F,
        50.0F,
        100.0F,
        200.0F,
        500.0F,
        1000.0F,
    };
    static const struct {
        fec_mode_t mode;
        uint8_t    payload_class;
    } profiles[] = {
        {FEC_NONE,         64U },
        { FEC_NONE,        128U},
        { FEC_LDPC_ROBUST, 64U },
        { FEC_LDPC_ROBUST, 128U},
    };
    size_t profile_index;
    (void) printf("fec_mode,payload_class,clock_ppm,success,timing_symbols\n");
    for (profile_index = 0U;
         profile_index < sizeof(profiles) / sizeof(profiles[0]);
         ++profile_index) {
        size_t       point;
        unsigned int successes = 0U;
        for (point = 0U; point < sizeof(ppm_points) / sizeof(ppm_points[0]);
             ++point) {
            float      timing_symbols = 0.0F;
            const bool success =
                run_case(profiles[profile_index].mode, profiles[profile_index].payload_class, ppm_points[point], &timing_symbols);
            if (success) {
                ++successes;
            }
            (void) printf("%d,%u,%.0f,%d,%.6f\n", (int) profiles[profile_index].mode, profiles[profile_index].payload_class, (double) ppm_points[point], success ? 1 : 0, (double) timing_symbols);
        }
        assert(successes == (unsigned int) (sizeof(ppm_points) / sizeof(ppm_points[0])));
    }
    return 0;
}
