#include "rtnc/modem.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { TRIALS = 10U };

static uint32_t random_state;

static float uniform_open(void) {
    random_state = random_state * 1664525U + 1013904223U;
    return ((float) (random_state >> 8U) + 0.5F) / 16777216.0F;
}

static float gaussian(void) {
    return sqrtf(-2.0F * logf(uniform_open())) *
           cosf(2.0F * (float) M_PI * uniform_open());
}

static unsigned int measure(fec_mode_t mode, uint8_t payload_class, float ebn0_db, double *goodput) {
    static float                  clean[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    static float                  noisy[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    static rtnc_modem_workspace_t workspace;
    uint8_t                       payload[RTNC_FRAME_MAX_PAYLOAD];
    uint8_t                       decoded[RTNC_FRAME_MAX_PAYLOAD];
    rtnc_modem_t                  modem;
    size_t                        sample_count = 0U;
    double                        energy = 0.0;
    float                         sigma;
    unsigned int                  successes = 0U;
    unsigned int                  trial;
    size_t                        index;

    for (index = 0U; index < payload_class; ++index) {
        payload[index] = (uint8_t) (index * 31U + payload_class);
    }
    assert(rtnc_modem_init_config(&modem, mode, payload_class));
    assert(rtnc_modem_tx_audio(&modem, payload, payload_class, clean, RTNC_MODEM_MAX_AUDIO_SAMPLES, &sample_count) == RTNC_MODEM_OK);
    for (index = 0U; index < sample_count; ++index) {
        energy += (double) clean[index] * (double) clean[index];
    }
    sigma = sqrtf((float) (energy / ((double) payload_class * 8.0)) / (2.0F * powf(10.0F, ebn0_db / 10.0F)));
    random_state = 0x13198a2eU ^ ((uint32_t) mode << 24U) ^
                   ((uint32_t) payload_class << 12U) ^
                   (uint32_t) (ebn0_db * 10.0F);
    for (trial = 0U; trial < TRIALS; ++trial) {
        rtnc_sync_metrics_t metrics;
        size_t              decoded_length = 0U;
        for (index = 0U; index < sample_count; ++index) {
            noisy[index] = clean[index] + sigma * gaussian();
        }
        if (rtnc_modem_rx_audio(&modem, noisy, sample_count, decoded, sizeof(decoded), &decoded_length, &metrics, &workspace) == RTNC_MODEM_OK &&
            decoded_length == payload_class &&
            memcmp(payload, decoded, payload_class) == 0) {
            ++successes;
        }
    }
    *goodput = (double) successes * payload_class * 48000.0 /
               ((double) TRIALS * (double) sample_count);
    rtnc_modem_deinit(&modem);
    return successes;
}

int main(void) {
    static const float      points[] = { 24.0F, 28.0F, 36.0F };
    static const fec_mode_t modes[] = { FEC_NONE, FEC_LDPC_ROBUST };
    size_t                  mode_index;
    (void) printf("fec_mode,EbN0_dB,class64_success,class128_success,"
                  "class64_Bps,class128_Bps\n");
    for (mode_index = 0U; mode_index < sizeof(modes) / sizeof(modes[0]);
         ++mode_index) {
        size_t       point;
        unsigned int first_128 = 0U;
        unsigned int last_128 = 0U;
        for (point = 0U; point < sizeof(points) / sizeof(points[0]); ++point) {
            double             goodput_64;
            double             goodput_128;
            const unsigned int success_64 =
                measure(modes[mode_index], 64U, points[point], &goodput_64);
            const unsigned int success_128 =
                measure(modes[mode_index], 128U, points[point], &goodput_128);
            if (point == 0U) {
                first_128 = success_128;
            }
            last_128 = success_128;
            (void) printf("%d,%.1f,%u,%u,%.3f,%.3f\n", (int) modes[mode_index], (double) points[point], success_64, success_128, goodput_64, goodput_128);
        }
        assert(last_128 >= first_128);
        assert(last_128 >= TRIALS / 2U);
    }
    return 0;
}
