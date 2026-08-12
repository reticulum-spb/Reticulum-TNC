#include "rtnc/modem.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { TRIALS = 10U,
       PAYLOAD_BYTES = 64U };

static uint32_t random_state;

static float uniform_open(void) {
    random_state = random_state * 1664525U + 1013904223U;
    return ((float) (random_state >> 8U) + 0.5F) / 16777216.0F;
}

static float gaussian(void) {
    return sqrtf(-2.0F * logf(uniform_open())) *
           cosf(2.0F * (float) M_PI * uniform_open());
}

static unsigned int measure(rtnc_modulation_t modulation, uint32_t rate, float ebn0_db, double *goodput) {
    static float                  clean[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    static float                  noisy[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    static rtnc_modem_workspace_t workspace;
    uint8_t                       payload[PAYLOAD_BYTES];
    uint8_t                       decoded[PAYLOAD_BYTES];
    rtnc_phy_profile_t            profile;
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
    assert(rtnc_phy_profile_psk(modulation, rate, 1650.0F, &profile));
    assert(rtnc_modem_init_profile(&modem, FEC_LDPC_ROBUST, PAYLOAD_BYTES, &profile));
    assert(rtnc_modem_tx_audio(&modem, payload, sizeof(payload), clean, RTNC_MODEM_MAX_AUDIO_SAMPLES, &sample_count) == RTNC_MODEM_OK);
    for (index = 0U; index < sample_count; ++index) {
        energy += (double) clean[index] * (double) clean[index];
    }
    sigma = sqrtf((float) (energy / ((double) PAYLOAD_BYTES * 8.0)) / (2.0F * powf(10.0F, ebn0_db / 10.0F)));
    random_state = UINT32_C(0x243f6a88) ^ rate ^
                   (uint32_t) (ebn0_db * 10.0F);
    for (trial = 0U; trial < TRIALS; ++trial) {
        rtnc_sync_metrics_t metrics = { 0 };
        size_t              decoded_length = 0U;
        for (index = 0U; index < sample_count; ++index) {
            noisy[index] = clean[index] + sigma * gaussian();
        }
        if (rtnc_modem_rx_audio(&modem, noisy, sample_count, decoded, sizeof(decoded), &decoded_length, &metrics, &workspace) == RTNC_MODEM_OK &&
            decoded_length == sizeof(payload) &&
            memcmp(payload, decoded, sizeof(payload)) == 0) {
            successes += 1U;
        }
    }
    *goodput = (double) successes * PAYLOAD_BYTES * 48000.0 /
               ((double) TRIALS * (double) sample_count);
    rtnc_modem_deinit(&modem);
    return successes;
}

int main(void) {
    static const struct {
        rtnc_modulation_t modulation;
        uint32_t          rate;
        const char       *name;
    } profiles[] = {
        {RTNC_MODULATION_QPSK,  800U,  "qpsk"},
        { RTNC_MODULATION_QPSK, 1200U, "qpsk"},
        { RTNC_MODULATION_QPSK, 1500U, "qpsk"},
        { RTNC_MODULATION_QPSK, 1600U, "qpsk"},
        { RTNC_MODULATION_QPSK, 1920U, "qpsk"},
        { RTNC_MODULATION_QPSK, 2000U, "qpsk"},
        { RTNC_MODULATION_QPSK, 2400U, "qpsk"},
        { RTNC_MODULATION_8PSK, 600U,  "8psk"},
        { RTNC_MODULATION_8PSK, 800U,  "8psk"},
        { RTNC_MODULATION_8PSK, 1000U, "8psk"},
        { RTNC_MODULATION_8PSK, 1200U, "8psk"},
        { RTNC_MODULATION_8PSK, 1600U, "8psk"},
    };
    static const float points[] = { 8.0F, 12.0F, 16.0F, 20.0F };
    size_t             rate_index;
    (void) printf("modulation,baud,EbN0_dB,successes,trials,goodput_Bps\n");
    for (rate_index = 0U;
         rate_index < sizeof(profiles) / sizeof(profiles[0]);
         ++rate_index) {
        size_t       point;
        unsigned int high_successes = 0U;
        for (point = 0U; point < sizeof(points) / sizeof(points[0]); ++point) {
            double             goodput = 0.0;
            const unsigned int successes =
                measure(profiles[rate_index].modulation, profiles[rate_index].rate, points[point], &goodput);
            high_successes = successes;
            (void) printf("%s,%u,%.1f,%u,%u,%.3f\n", profiles[rate_index].name, profiles[rate_index].rate, (double) points[point], successes, TRIALS, goodput);
        }
        assert(high_successes >= (TRIALS * 3U) / 4U);
    }
    return 0;
}
