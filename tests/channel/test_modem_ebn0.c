#include "rtnc/modem.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { TRIALS = 20U,
       PAYLOAD_BYTES = 64U };

static uint32_t random_state = 0x243f6a88U;

static float uniform_open(void) {
    random_state = random_state * 1664525U + 1013904223U;
    return ((float) (random_state >> 8U) + 0.5F) / 16777216.0F;
}

static float gaussian(void) {
    return sqrtf(-2.0F * logf(uniform_open())) *
           cosf(2.0F * (float) M_PI * uniform_open());
}

static unsigned int measure(fec_mode_t mode, float ebn0_db, double *bytes_per_second, size_t *airtime_samples) {
    static float                  clean[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    static float                  noisy[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    static rtnc_modem_workspace_t workspace;
    uint8_t                       payload[PAYLOAD_BYTES];
    uint8_t                       decoded[RTNC_FRAME_MAX_PAYLOAD];
    rtnc_modem_t                  modem;
    size_t                        sample_count = 0U;
    double                        energy = 0.0;
    float                         sigma;
    unsigned int                  successes = 0U;
    unsigned int                  trial;
    size_t                        index;

    for (index = 0U; index < sizeof(payload); ++index) {
        payload[index] = (uint8_t) (index * 29U + 7U);
    }
    assert(rtnc_modem_init_config(&modem, mode, PAYLOAD_BYTES));
    assert(rtnc_modem_tx_audio(&modem, payload, sizeof(payload), clean, RTNC_MODEM_MAX_AUDIO_SAMPLES, &sample_count) == RTNC_MODEM_OK);
    for (index = 0U; index < sample_count; ++index) {
        energy += (double) clean[index] * (double) clean[index];
    }
    sigma = sqrtf((float) (energy / ((double) PAYLOAD_BYTES * 8.0)) / (2.0F * powf(10.0F, ebn0_db / 10.0F)));
    random_state = 0x243f6a88U + (uint32_t) mode * 0x9e3779b9U +
                   (uint32_t) (ebn0_db * 10.0F);
    for (trial = 0U; trial < TRIALS; ++trial) {
        rtnc_sync_metrics_t metrics;
        size_t              decoded_length = 0U;
        for (index = 0U; index < sample_count; ++index) {
            noisy[index] = clean[index] + sigma * gaussian();
        }
        if (rtnc_modem_rx_audio(&modem, noisy, sample_count, decoded, sizeof(decoded), &decoded_length, &metrics, &workspace) == RTNC_MODEM_OK &&
            decoded_length == sizeof(payload) &&
            memcmp(payload, decoded, sizeof(payload)) == 0) {
            ++successes;
        }
    }
    *airtime_samples = sample_count;
    *bytes_per_second = (double) successes * PAYLOAD_BYTES * 48000.0 /
                        ((double) TRIALS * (double) sample_count);
    rtnc_modem_deinit(&modem);
    return successes;
}

int main(void) {
    static const float points_db[] = { 20.0F, 24.0F, 28.0F, 36.0F };
    unsigned int       robust_low = 0U;
    unsigned int       robust_high = 0U;
    size_t             point;
    (void) printf("EbN0_dB,none_success,robust_success,normal_success,"
                  "none_Bps,robust_Bps,normal_Bps,none_samples,robust_samples,"
                  "normal_samples\n");
    for (point = 0U; point < sizeof(points_db) / sizeof(points_db[0]); ++point) {
        double             none_bps;
        double             robust_bps;
        double             normal_bps;
        size_t             none_samples;
        size_t             robust_samples;
        size_t             normal_samples;
        const unsigned int none = measure(FEC_NONE, points_db[point], &none_bps, &none_samples);
        const unsigned int robust =
            measure(FEC_LDPC_ROBUST, points_db[point], &robust_bps, &robust_samples);
        const unsigned int normal =
            measure(FEC_LDPC_NORMAL, points_db[point], &normal_bps, &normal_samples);
        if (point == 0U) {
            robust_low = robust;
        }
        robust_high = robust;
        (void) printf("%.1f,%u,%u,%u,%.3f,%.3f,%.3f,%zu,%zu,%zu\n", (double) points_db[point], none, robust, normal, none_bps, robust_bps, normal_bps, none_samples, robust_samples, normal_samples);
    }
    assert(robust_high >= robust_low);
    assert(robust_high >= (TRIALS * 3U) / 4U);
    return 0;
}
