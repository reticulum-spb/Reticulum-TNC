#include "rtnc/acquisition.h"

#include <math.h>
#include <string.h>

bool rtnc_acquisition_detector_init(
    rtnc_acquisition_detector_t *detector,
    const rtnc_phy_profile_t    *profile,
    const float complex         *acquisition_symbols,
    size_t                       stride_samples,
    float                        threshold
) {
    size_t index;
    size_t ring_length;
    if (detector == NULL || !rtnc_phy_profile_is_valid(profile) ||
        acquisition_symbols == NULL || stride_samples == 0U ||
        threshold <= 0.0F || threshold > 1.0F) {
        return false;
    }
    ring_length = RTNC_MODEM_ACQUISITION_SYMBOLS *
                  (size_t) profile->samples_per_symbol;
    if (ring_length > RTNC_MODEM_MAX_AUDIO_SAMPLES) {
        return false;
    }
    (void) memset(detector, 0, sizeof(*detector));
    detector->ring_length = ring_length;
    detector->stride_samples = stride_samples;
    detector->threshold = threshold;
    for (index = 0U; index < RTNC_MODEM_ACQUISITION_SYMBOLS; ++index) {
        detector->reference[index] = acquisition_symbols[index];
    }
    if (!rtnc_carrier_init(&detector->carrier, profile->sample_rate_hz, profile->carrier_hz) ||
        !rtnc_rrc_init(&detector->rrc, profile->samples_per_symbol, RTNC_MODEM_RRC_DELAY_SYMBOLS, profile->rrc_rolloff)) {
        rtnc_acquisition_detector_deinit(detector);
        return false;
    }
    return true;
}

void rtnc_acquisition_detector_deinit(rtnc_acquisition_detector_t *detector) {
    if (detector == NULL) {
        return;
    }
    rtnc_rrc_deinit(&detector->rrc);
    rtnc_carrier_deinit(&detector->carrier);
    detector->ring_length = 0U;
}

bool rtnc_acquisition_detector_process(rtnc_acquisition_detector_t *detector, float audio_sample, float *score) {
    float complex mixed;
    float complex filtered;
    float complex correlation = 0.0F;
    float         energy = 0.0F;
    size_t        symbol;
    if (detector == NULL || detector->ring_length == 0U || score == NULL ||
        !rtnc_carrier_downconvert(&detector->carrier, audio_sample, &mixed) ||
        !rtnc_rrc_match(&detector->rrc, mixed, &filtered)) {
        return false;
    }
    *score = 0.0F;
    detector->ring[detector->write_index] = filtered;
    detector->write_index =
        (detector->write_index + 1U) % detector->ring_length;
    if (detector->sample_count < detector->ring_length) {
        detector->sample_count += 1U;
    }
    detector->stride_count += 1U;
    if (detector->sample_count < detector->ring_length ||
        detector->stride_count < detector->stride_samples) {
        return false;
    }
    detector->stride_count = 0U;
    for (symbol = 0U; symbol < RTNC_MODEM_ACQUISITION_SYMBOLS; ++symbol) {
        const size_t source =
            (detector->write_index +
             symbol * (detector->ring_length /
                       RTNC_MODEM_ACQUISITION_SYMBOLS)) %
            detector->ring_length;
        const float complex received = detector->ring[source];
        correlation += conjf(detector->reference[symbol]) * received;
        energy += crealf(received * conjf(received));
    }
    if (energy > 0.0F) {
        *score = cabsf(correlation) /
                 sqrtf((float) RTNC_MODEM_ACQUISITION_SYMBOLS * energy);
    }
    if (*score > detector->best_score) {
        detector->best_score = *score;
    }
    return *score >= detector->threshold;
}
