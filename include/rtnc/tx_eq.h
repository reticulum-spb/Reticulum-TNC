#ifndef RTNC_TX_EQ_H
#define RTNC_TX_EQ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RTNC_TX_EQ_TAP_COUNT 17U

/** Design a symmetric 48-kHz inverse-response FIR from measured amplitudes. */
bool rtnc_tx_eq_design(const unsigned int *frequencies_hz, const double *amplitudes, size_t point_count, float carrier_hz, float low_hz, float high_hz, float taps[RTNC_TX_EQ_TAP_COUNT]);

/** Evaluate the magnitude of a real FIR at the given frequency. */
double rtnc_tx_eq_magnitude(const float taps[RTNC_TX_EQ_TAP_COUNT], double frequency_hz);

/** Evaluate the signed zero-phase gain of a symmetric FIR. */
double rtnc_tx_eq_zero_phase_gain(
    const float taps[RTNC_TX_EQ_TAP_COUNT],
    double      frequency_hz
);

/** Apply one sample of centered zero-phase FIR convolution. */
float rtnc_tx_eq_apply_sample(const float taps[RTNC_TX_EQ_TAP_COUNT], const int16_t *samples, size_t count, size_t index);

#endif
