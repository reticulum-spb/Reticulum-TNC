#include "rtnc/equalizer.h"

#include <liquid/liquid.h>
#include <math.h>
#include <stddef.h>

bool rtnc_equalizer_init(rtnc_equalizer_t *equalizer, size_t tap_count, float forgetting_factor) {
    if (equalizer == NULL || tap_count < 3U || tap_count > 63U ||
        (tap_count & 1U) == 0U || forgetting_factor <= 0.0F ||
        forgetting_factor > 1.0F) {
        return false;
    }
    equalizer->equalizer = eqrls_cccf_create(NULL, (unsigned int) tap_count);
    if (equalizer->equalizer == NULL) {
        return false;
    }
    if (eqrls_cccf_set_bw((eqrls_cccf) equalizer->equalizer, forgetting_factor) !=
        LIQUID_OK) {
        rtnc_equalizer_deinit(equalizer);
        return false;
    }
    equalizer->tap_count = tap_count;
    return true;
}

void rtnc_equalizer_deinit(rtnc_equalizer_t *equalizer) {
    if (equalizer == NULL) {
        return;
    }
    if (equalizer->equalizer != NULL) {
        (void) eqrls_cccf_destroy((eqrls_cccf) equalizer->equalizer);
    }
    equalizer->equalizer = NULL;
    equalizer->tap_count = 0U;
}

void rtnc_equalizer_reset(rtnc_equalizer_t *equalizer) {
    if (equalizer != NULL && equalizer->equalizer != NULL) {
        (void) eqrls_cccf_reset((eqrls_cccf) equalizer->equalizer);
    }
}

bool rtnc_equalizer_execute(rtnc_equalizer_t *equalizer, const float complex samples[2], bool training, float complex desired, float complex *output, float *error_magnitude) {
    size_t index;
    if (equalizer == NULL || equalizer->equalizer == NULL || samples == NULL ||
        output == NULL || error_magnitude == NULL) {
        return false;
    }
    for (index = 0U; index < 2U; ++index) {
        if (eqrls_cccf_push((eqrls_cccf) equalizer->equalizer, samples[index]) !=
            LIQUID_OK) {
            return false;
        }
    }
    if (eqrls_cccf_execute((eqrls_cccf) equalizer->equalizer, output) !=
        LIQUID_OK) {
        return false;
    }
    *error_magnitude = cabsf(desired - *output);
    if (training &&
        eqrls_cccf_step((eqrls_cccf) equalizer->equalizer, desired, *output) !=
            LIQUID_OK) {
        return false;
    }
    return true;
}

bool rtnc_equalizer_copy_taps(const rtnc_equalizer_t *equalizer, float complex *taps, size_t capacity) {
    if (equalizer == NULL || equalizer->equalizer == NULL || taps == NULL ||
        capacity < equalizer->tap_count) {
        return false;
    }
    return eqrls_cccf_get_weights((eqrls_cccf) equalizer->equalizer, taps) ==
           LIQUID_OK;
}

bool rtnc_equalizer_adapt(rtnc_equalizer_t *equalizer, float complex desired, float complex output) {
    return equalizer != NULL && equalizer->equalizer != NULL &&
           eqrls_cccf_step((eqrls_cccf) equalizer->equalizer, desired, output) ==
               LIQUID_OK;
}
