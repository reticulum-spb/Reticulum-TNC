#include "rtnc/modem.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { TRIALS = 8U,
       PAYLOAD_BYTES = 128U };

static uint32_t random_state;

typedef struct {
    unsigned int successes;
    unsigned int no_frame;
    unsigned int truncated;
    unsigned int rejected;
    float        acquisition_sum;
    float        training_sum;
    float        evm_sum;
    float        cfo_sum;
} measurement_t;

static float uniform_open(void) {
    random_state = random_state * 1664525U + 1013904223U;
    return ((float) (random_state >> 8U) + 0.5F) / 16777216.0F;
}

static float gaussian(void) {
    return sqrtf(-2.0F * logf(uniform_open())) *
           cosf(2.0F * (float) M_PI * uniform_open());
}

static size_t resample_clock(const float *input, size_t input_count, float *output, float ppm) {
    const double ratio = 1.0 + (double) ppm * 1.0e-6;
    double       position = 0.0;
    size_t       output_count = 0U;
    while (position + 1.0 < (double) input_count &&
           output_count < RTNC_MODEM_MAX_AUDIO_SAMPLES) {
        const size_t lower = (size_t) position;
        const float  fraction = (float) (position - (double) lower);
        output[output_count++] = input[lower] * (1.0F - fraction) +
                                 input[lower + 1U] * fraction;
        position += ratio;
    }
    return output_count;
}

static measurement_t measure(float ppm, float noise_sigma) {
    static float                  transmitted[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    static float                  resampled[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    static float                  received[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    static rtnc_modem_workspace_t workspace;
    uint8_t                       payload[PAYLOAD_BYTES];
    uint8_t                       decoded[PAYLOAD_BYTES];
    rtnc_modem_t                  modem;
    size_t                        transmitted_count = 0U;
    size_t                        received_count;
    size_t                        index;
    measurement_t                 measurement = { 0 };
    unsigned int                  trial;

    for (index = 0U; index < PAYLOAD_BYTES; ++index) {
        payload[index] = (uint8_t) (index * 23U + 0x5dU);
    }
    assert(rtnc_modem_init_config(&modem, FEC_LDPC_ROBUST, PAYLOAD_BYTES));
    assert(rtnc_modem_tx_audio(&modem, payload, sizeof(payload), transmitted, RTNC_MODEM_MAX_AUDIO_SAMPLES, &transmitted_count) == RTNC_MODEM_OK);
    received_count = resample_clock(transmitted, transmitted_count, resampled, ppm);
    random_state = 0x85a308d3U ^ (uint32_t) (ppm + 1000.0F) ^
                   ((uint32_t) (noise_sigma * 100000.0F) << 12U);
    for (trial = 0U; trial < TRIALS; ++trial) {
        rtnc_sync_metrics_t metrics;
        rtnc_modem_status_t status;
        size_t              decoded_length = 0U;
        for (index = 0U; index < received_count; ++index) {
            received[index] = resampled[index] + noise_sigma * gaussian();
        }
        status = rtnc_modem_rx_audio(&modem, received, received_count, decoded, sizeof(decoded), &decoded_length, &metrics, &workspace);
        measurement.acquisition_sum += metrics.acquisition_correlation;
        measurement.training_sum += metrics.training_correlation;
        measurement.evm_sum += metrics.evm_rms;
        measurement.cfo_sum += metrics.carrier_offset_hz;
        if (status == RTNC_MODEM_OK &&
            decoded_length == sizeof(payload) &&
            memcmp(payload, decoded, sizeof(payload)) == 0) {
            ++measurement.successes;
        } else if (status == RTNC_MODEM_NO_FRAME) {
            ++measurement.no_frame;
        } else if (status == RTNC_MODEM_TRUNCATED_FRAME) {
            ++measurement.truncated;
        } else if (status == RTNC_MODEM_FRAME_REJECTED) {
            ++measurement.rejected;
        }
    }
    rtnc_modem_deinit(&modem);
    return measurement;
}

int main(void) {
    static const float ppm_points[] = {
        -500.0F,
        0.0F,
        200.0F,
        300.0F,
        400.0F,
        500.0F,
    };
    static const float noise_points[] = { 0.0F, 0.005F, 0.010F, 0.020F };
    size_t             ppm_index;
    (void) printf("clock_ppm,noise_sigma,successes,no_frame,truncated,rejected,"
                  "acquisition_score,training_score,evm,cfo_hz\n");
    for (ppm_index = 0U;
         ppm_index < sizeof(ppm_points) / sizeof(ppm_points[0]);
         ++ppm_index) {
        size_t noise_index;
        for (noise_index = 0U;
             noise_index < sizeof(noise_points) / sizeof(noise_points[0]);
             ++noise_index) {
            const measurement_t measurement =
                measure(ppm_points[ppm_index], noise_points[noise_index]);
            (void) printf("%.0f,%.3f,%u,%u,%u,%u,%.6f,%.6f,%.6f,%.6f\n", (double) ppm_points[ppm_index], (double) noise_points[noise_index], measurement.successes, measurement.no_frame, measurement.truncated, measurement.rejected, (double) (measurement.acquisition_sum / (float) TRIALS), (double) (measurement.training_sum / (float) TRIALS), (double) (measurement.evm_sum / (float) TRIALS), (double) (measurement.cfo_sum / (float) TRIALS));
            assert(measurement.successes == TRIALS);
        }
    }
    return 0;
}
