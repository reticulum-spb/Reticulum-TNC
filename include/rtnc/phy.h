#ifndef RTNC_PHY_H
#define RTNC_PHY_H

#include <stdbool.h>
#include <complex.h>
#include <stdint.h>

enum { RTNC_EQUALIZER_DIAGNOSTIC_TAPS = 17U };

typedef enum {
    RTNC_MODULATION_QPSK = 0,
    RTNC_MODULATION_8PSK = 1,
} rtnc_modulation_t;

/** Fixed physical-layer parameters selected before transmission. */
typedef struct {
    uint32_t          sample_rate_hz;
    rtnc_modulation_t modulation;
    uint8_t           bits_per_symbol;
    uint32_t          symbol_rate_baud;
    float             carrier_hz;
    float             rrc_rolloff;
    float             acquisition_threshold;
    float             training_threshold;
    uint16_t          samples_per_symbol;
} rtnc_phy_profile_t;

/** Receiver observability exported for every frame attempt.
 * Correlation fields are normalized to 0..1 and remain useful on failure;
 * EVM is populated once payload symbols have reached the demapper, including
 * FEC/CRC-rejected frames. Equalizer fields describe only a successful
 * two-samples/symbol fallback retry.
 */
typedef struct {
    bool  frame_detected;
    float timing_symbols;
    float carrier_offset_hz;
    float phase_radians;
    float evm_rms;
    /** Effective post-demodulation SNR derived from unit-power symbol EVM. */
    float         training_snr_db;
    float         acquisition_correlation;
    float         training_correlation;
    bool          equalizer_used;
    float         equalizer_training_error;
    float complex equalizer_taps[RTNC_EQUALIZER_DIAGNOSTIC_TAPS];
} rtnc_sync_metrics_t;

/** Return the initial fixed QPSK-1200 audio profile. */
rtnc_phy_profile_t rtnc_phy_profile_qpsk_1200(void);

/** Build a fixed 48-kHz QPSK profile with an integer samples/symbol ratio. */
bool rtnc_phy_profile_qpsk(uint32_t symbol_rate_baud, float carrier_hz, rtnc_phy_profile_t *profile);

/** Build a fixed 48-kHz QPSK or 8PSK profile. */
bool rtnc_phy_profile_psk(rtnc_modulation_t modulation, uint32_t symbol_rate_baud, float carrier_hz, rtnc_phy_profile_t *profile);

/** Check profile invariants supported by the Phase 1 implementation. */
bool rtnc_phy_profile_is_valid(const rtnc_phy_profile_t *profile);

#endif
