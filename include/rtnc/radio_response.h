#ifndef RTNC_RADIO_RESPONSE_H
#define RTNC_RADIO_RESPONSE_H

#include <complex.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    double amplitude_ripple_db;
    double relative_delay_ms;
    double group_delay_ripple_ms;
    double residual_phase_rms_degrees;
    size_t points_used;
} rtnc_radio_response_metrics_t;

typedef enum {
    RTNC_RADIO_SUITABILITY_POOR = 0,
    RTNC_RADIO_SUITABILITY_MARGINAL,
    RTNC_RADIO_SUITABILITY_GOOD,
} rtnc_radio_suitability_t;

/**
 * Analyze a complex end-to-end audio response over an occupied band.
 *
 * An arbitrary capture offset contributes only constant and linear phase.
 * The returned relative delay includes that unknown offset; group-delay ripple
 * and residual phase describe the channel departure from linear phase.
 */
bool rtnc_radio_response_analyze(
    const unsigned int            *frequencies_hz,
    const double complex          *response,
    size_t                         point_count,
    double                         low_hz,
    double                         high_hz,
    rtnc_radio_response_metrics_t *metrics,
    double                        *unwrapped_phase_radians
);

/** Conservative phase/group-delay screening; packet goodput remains final. */
rtnc_radio_suitability_t rtnc_radio_response_suitability(
    const rtnc_radio_response_metrics_t *metrics,
    unsigned int                         bits_per_symbol,
    unsigned int                         symbol_rate_baud
);

#endif
