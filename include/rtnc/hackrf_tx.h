#ifndef RTNC_HACKRF_TX_H
#define RTNC_HACKRF_TX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t frequency_hz;
    uint32_t iq_sample_rate_hz;
    uint32_t audio_sample_rate_hz;
    float    deviation_hz;
    uint32_t lead_ms;
    uint32_t tail_ms;
    uint8_t  txvga_gain_db;
    int8_t   iq_amplitude;
} rtnc_hackrf_tx_config_t;

void rtnc_hackrf_tx_default_config(rtnc_hackrf_tx_config_t *config);
bool rtnc_hackrf_tx_config_is_valid(const rtnc_hackrf_tx_config_t *config);

/** Blocking transmission of one normalized mono audio waveform as NFM. */
bool rtnc_hackrf_tx_audio(const rtnc_hackrf_tx_config_t *config, const float *audio, size_t audio_samples, uint64_t *iq_samples_sent);

#endif
