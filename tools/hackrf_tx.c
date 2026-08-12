#define _POSIX_C_SOURCE 200809L

#include "rtnc/hackrf_tx.h"
#include "rtnc/modem.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(int argc, char **argv) {
    static float            frame_audio[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    float                  *audio = NULL;
    uint8_t                 payload[128U];
    rtnc_modem_t            modem;
    rtnc_hackrf_tx_config_t config;
    size_t                  payload_bytes = 64U;
    size_t                  sample_count = 0U;
    size_t                  index;
    float                   peak = 0.0F;
    uint64_t                iq_samples = 0U;
    unsigned int            frame_count = 1U;
    unsigned int            frame;
    unsigned int            delivered = 0U;
    unsigned int            gap_ms = 750U;
    size_t                  series_samples = 0U;
    size_t                  series_capacity;
    size_t                  gap_samples;
    uint32_t                symbol_rate_baud = 1200U;
    float                   carrier_hz = 1650.0F;
    float                   rrc_rolloff = 0.25F;
    rtnc_modulation_t       modulation = RTNC_MODULATION_QPSK;
    rtnc_phy_profile_t      phy_profile;

    rtnc_hackrf_tx_default_config(&config);
    if (argc > 1) {
        config.frequency_hz = strtoull(argv[1], NULL, 10);
    }
    if (argc > 2) {
        config.txvga_gain_db = (uint8_t) strtoul(argv[2], NULL, 10);
    }
    if (argc > 3) {
        config.deviation_hz = strtof(argv[3], NULL);
    }
    if (argc > 4) {
        payload_bytes = (size_t) strtoul(argv[4], NULL, 10);
    }
    if (argc > 5) {
        frame_count = (unsigned int) strtoul(argv[5], NULL, 10);
    }
    if (argc > 6) {
        gap_ms = (unsigned int) strtoul(argv[6], NULL, 10);
    }
    if (argc > 7) {
        symbol_rate_baud = (uint32_t) strtoul(argv[7], NULL, 10);
    }
    if (argc > 8) {
        carrier_hz = strtof(argv[8], NULL);
    }
    if (argc > 9) {
        rrc_rolloff = strtof(argv[9], NULL);
    }
    if (argc > 10) {
        if (strcmp(argv[10], "qpsk") == 0) {
            modulation = RTNC_MODULATION_QPSK;
        } else if (strcmp(argv[10], "8psk") == 0) {
            modulation = RTNC_MODULATION_8PSK;
        } else {
            modulation = (rtnc_modulation_t) 99;
        }
    }
    if (argc > 11 || frame_count == 0U || frame_count > 50U || gap_ms > 5000U ||
        (payload_bytes != 64U && payload_bytes != 128U) ||
        !rtnc_phy_profile_psk(modulation, symbol_rate_baud, carrier_hz, &phy_profile) ||
        rrc_rolloff <= 0.0F || rrc_rolloff > 1.0F ||
        !rtnc_hackrf_tx_config_is_valid(&config)) {
        (void) fprintf(stderr, "usage: %s [FREQ_HZ [TXVGA_DB [DEVIATION_HZ "
                               "[PAYLOAD_64_OR_128 [COUNT [GAP_MS "
                               "[SYMBOL_RATE [CARRIER_HZ [RRC_ROLLOFF "
                               "[qpsk|8psk]]]]]]]]]]\n",
                       argv[0]);
        return 2;
    }
    gap_samples = (size_t) config.audio_sample_rate_hz * gap_ms / 1000U;
    series_capacity = frame_count * RTNC_MODEM_MAX_AUDIO_SAMPLES +
                      (frame_count - 1U) * gap_samples;
    audio = calloc(series_capacity, sizeof(*audio));
    if (audio == NULL) {
        (void) fprintf(stderr, "series allocation failed\n");
        return 1;
    }
    (void) printf("transmitting freq=%llu txvga=%u deviation=%.1f payload=%zu "
                  "frames=%u gap_ms=%u modulation=%s baud=%u carrier=%.1f "
                  "alpha=%.2f\n",
                  (unsigned long long) config.frequency_hz,
                  (unsigned int) config.txvga_gain_db,
                  (double) config.deviation_hz,
                  payload_bytes,
                  frame_count,
                  gap_ms,
                  modulation == RTNC_MODULATION_8PSK ? "8psk" : "qpsk",
                  symbol_rate_baud,
                  (double) carrier_hz,
                  (double) rrc_rolloff);
    (void) fflush(stdout);
    phy_profile.rrc_rolloff = rrc_rolloff;
    for (frame = 0U; frame < frame_count; ++frame) {
        for (index = 0U; index < payload_bytes; ++index) {
            payload[index] = (uint8_t) (index * 37U + 0x29U);
        }
        payload[0] = (uint8_t) (frame >> 24U);
        payload[1] = (uint8_t) (frame >> 16U);
        payload[2] = (uint8_t) (frame >> 8U);
        payload[3] = (uint8_t) frame;
        if (!rtnc_modem_init_profile(&modem, FEC_LDPC_ROBUST, (uint8_t) payload_bytes, &phy_profile) ||
            rtnc_modem_tx_audio(&modem, payload, payload_bytes, frame_audio, RTNC_MODEM_MAX_AUDIO_SAMPLES, &sample_count) !=
                RTNC_MODEM_OK) {
            (void) fprintf(stderr, "frame %u waveform generation failed\n", frame);
            return 1;
        }
        rtnc_modem_deinit(&modem);
        for (index = 0U; index < sample_count; ++index) {
            peak = fmaxf(peak, fabsf(frame_audio[index]));
        }
        for (index = 0U; index < sample_count; ++index) {
            audio[series_samples + index] = frame_audio[index];
        }
        series_samples += sample_count;
        if (frame + 1U < frame_count) {
            series_samples += gap_samples;
        }
    }
    if (peak <= 0.0F) {
        free(audio);
        return 1;
    }
    for (index = 0U; index < series_samples; ++index) {
        audio[index] = 0.70F * audio[index] / peak;
    }
    if (rtnc_hackrf_tx_audio(&config, audio, series_samples, &iq_samples)) {
        delivered = frame_count;
    }
    free(audio);
    (void) printf("transmit_complete=%u/%u iq_samples=%llu duration=%.3f\n", delivered, frame_count, (unsigned long long) iq_samples, (double) iq_samples / (double) config.iq_sample_rate_hz);
    return delivered == frame_count ? 0 : 1;
}
