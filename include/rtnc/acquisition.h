#ifndef RTNC_ACQUISITION_H
#define RTNC_ACQUISITION_H

#include "rtnc/carrier.h"
#include "rtnc/modem.h"
#include "rtnc/rrc.h"

#include <complex.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    rtnc_carrier_t carrier;
    rtnc_rrc_t     rrc;
    float complex  ring[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    float complex  reference[RTNC_MODEM_ACQUISITION_SYMBOLS];
    size_t         ring_length;
    size_t         write_index;
    size_t         sample_count;
    size_t         stride_count;
    size_t         stride_samples;
    float          threshold;
    float          best_score;
} rtnc_acquisition_detector_t;

bool rtnc_acquisition_detector_init(
    rtnc_acquisition_detector_t *detector,
    const rtnc_phy_profile_t    *profile,
    const float complex         *acquisition_symbols,
    size_t                       stride_samples,
    float                        threshold
);
void rtnc_acquisition_detector_deinit(rtnc_acquisition_detector_t *detector);

/** Consume one real audio sample and report a normalized preamble hit. */
bool rtnc_acquisition_detector_process(rtnc_acquisition_detector_t *detector, float audio_sample, float *score);

#endif
