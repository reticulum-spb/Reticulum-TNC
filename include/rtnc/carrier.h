#ifndef RTNC_CARRIER_H
#define RTNC_CARRIER_H

#include <complex.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    void    *tx_nco;
    void    *rx_nco;
    float    tx_radians_per_sample;
    float    rx_radians_per_sample;
    float    tx_initial_phase;
    uint32_t sample_rate_hz;
} rtnc_carrier_t;

/** Create matched audio up/down converters outside the sample path. */
bool rtnc_carrier_init(rtnc_carrier_t *carrier, uint32_t sample_rate_hz, float carrier_hz);
void rtnc_carrier_deinit(rtnc_carrier_t *carrier);
void rtnc_carrier_reset(rtnc_carrier_t *carrier);

/** Test/channel hook: apply a fixed TX carrier offset on subsequent frames. */
bool rtnc_carrier_set_tx_frequency_offset(rtnc_carrier_t *carrier, float offset_hz);

/** Test/channel hook: apply a fixed TX carrier phase on subsequent frames. */
bool rtnc_carrier_set_tx_phase(rtnc_carrier_t *carrier, float phase_radians);

/** Convert one analytic baseband sample to real audio. */
bool rtnc_carrier_upconvert(rtnc_carrier_t *carrier, float complex baseband, float *audio);

/** Convert one real audio sample to complex baseband (image remains unfiltered). */
bool rtnc_carrier_downconvert(rtnc_carrier_t *carrier, float audio, float complex *baseband);

#endif
