#ifndef RTNC_MODEM_H
#define RTNC_MODEM_H

#include "rtnc/carrier.h"
#include "rtnc/equalizer.h"
#include "rtnc/fec.h"
#include "rtnc/frame.h"
#include "rtnc/phy.h"
#include "rtnc/psk.h"
#include "rtnc/rrc.h"
#include "rtnc/timing.h"

#include <complex.h>
#include <stdbool.h>
#include <stddef.h>

enum {
    RTNC_MODEM_ACQUISITION_SYMBOLS = 32U,
    RTNC_MODEM_TIMING_WARMUP_SYMBOLS = 64U,
    RTNC_MODEM_TIMING_TRAINING_SYMBOLS = 32U,
    RTNC_MODEM_TRAINING_SYMBOLS = RTNC_MODEM_TIMING_WARMUP_SYMBOLS +
                                  RTNC_MODEM_TIMING_TRAINING_SYMBOLS,
    RTNC_MODEM_RRC_DELAY_SYMBOLS = 7U,
    RTNC_MODEM_TRAILING_SYMBOLS = 16U,
    RTNC_MODEM_EQUALIZER_TAPS = 9U,
    RTNC_MODEM_MAX_FEC_BYTES = 320U,
    RTNC_MODEM_MAX_PROTECTED_BYTES = 160U,
    RTNC_MODEM_MAX_AUDIO_SAMPLES = 90000U,
    RTNC_MODEM_MAX_LLR = RTNC_MODEM_MAX_FEC_BYTES * 8U,
    RTNC_MODEM_MAX_RECOVERED_SYMBOLS =
        RTNC_MODEM_MAX_AUDIO_SAMPLES / 20U + 64U,
};

typedef enum {
    RTNC_MODEM_OK = 0,
    RTNC_MODEM_INVALID_ARGUMENT,
    RTNC_MODEM_BUFFER_TOO_SMALL,
    RTNC_MODEM_NO_FRAME,
    RTNC_MODEM_TRUNCATED_FRAME,
    RTNC_MODEM_FRAME_REJECTED,
    RTNC_MODEM_DSP_ERROR,
} rtnc_modem_status_t;

typedef struct {
    rtnc_phy_profile_t profile;
    rtnc_psk_t         psk;
    rtnc_rrc_t         rrc;
    rtnc_carrier_t     carrier;
    rtnc_timing_t      timing;
    rtnc_equalizer_t   equalizer;
    fec_mode_t         fec_mode;
    uint8_t            payload_class_bytes;
    float complex      training[RTNC_MODEM_TRAINING_SYMBOLS];
} rtnc_modem_t;

/** Caller-owned offline RX scratch space; no allocation occurs per frame. */
typedef struct {
    float complex           filtered[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    float complex           recovered[RTNC_MODEM_MAX_RECOVERED_SYMBOLS];
    float                   llr[RTNC_MODEM_MAX_LLR];
    size_t                  llr_count;
    rtnc_fec_decode_stats_t fec_stats;
} rtnc_modem_workspace_t;

/** Deterministic bitrate/airtime data derived from one complete profile. */
typedef struct {
    uint32_t raw_bitrate_bps;
    uint32_t fec_bitrate_bps;
    uint32_t interface_bitrate_bps;
    size_t   frame_samples;
} rtnc_modem_rate_t;

/**
 * Calculate profile rates without creating LiquidDSP objects.
 *
 * interface_bitrate_bps excludes the one-byte fragmentation header and
 * includes fixed PHY/FEC overhead. It is a no-error, no-PTT-delay ceiling,
 * rounded down for use as a conservative Reticulum interface bitrate.
 */
bool rtnc_modem_profile_rate(const rtnc_phy_profile_t *profile, fec_mode_t fec_mode, uint8_t payload_class_bytes, rtnc_modem_rate_t *rate);

bool rtnc_modem_init(rtnc_modem_t *modem);
/** Initialize a fixed profile; payload class must be 64 or 128 bytes. */
bool rtnc_modem_init_config(rtnc_modem_t *modem, fec_mode_t fec_mode, uint8_t payload_class_bytes);
/** Initialize a modem with a fixed, preselected PHY profile. */
bool rtnc_modem_init_profile(rtnc_modem_t *modem, fec_mode_t fec_mode, uint8_t payload_class_bytes, const rtnc_phy_profile_t *profile);
void rtnc_modem_deinit(rtnc_modem_t *modem);

/** Audio samples required by a maximum-payload frame in this fixed profile. */
size_t rtnc_modem_frame_samples(const rtnc_modem_t *modem);

rtnc_modem_status_t rtnc_modem_tx_audio(rtnc_modem_t *modem, const uint8_t *payload, size_t payload_length, float *audio, size_t capacity, size_t *sample_count);

rtnc_modem_status_t rtnc_modem_rx_audio(rtnc_modem_t *modem, const float *audio, size_t sample_count, uint8_t *payload, size_t capacity, size_t *payload_length, rtnc_sync_metrics_t *metrics, rtnc_modem_workspace_t *workspace);

/**
 * Decode only through the ordinary 1-sample/symbol path.
 *
 * A detected frame that fails FEC is returned as RTNC_MODEM_FRAME_REJECTED;
 * the caller may enqueue the original audio for a lower-priority full decode.
 */
rtnc_modem_status_t rtnc_modem_rx_audio_fast(
    rtnc_modem_t           *modem,
    const float            *audio,
    size_t                  sample_count,
    uint8_t                *payload,
    size_t                  capacity,
    size_t                 *payload_length,
    rtnc_sync_metrics_t    *metrics,
    rtnc_modem_workspace_t *workspace
);

#endif
