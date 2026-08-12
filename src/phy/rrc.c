#include "rtnc/rrc.h"

#include <liquid/liquid.h>
#include <math.h>
#include <stddef.h>

bool rtnc_rrc_init(rtnc_rrc_t *rrc, uint16_t samples_per_symbol, uint16_t delay_symbols, float rolloff) {
    if (rrc == NULL || samples_per_symbol < 2U || delay_symbols == 0U ||
        rolloff <= 0.0F || rolloff > 1.0F) {
        return false;
    }
    rrc->interpolator = firinterp_crcf_create_prototype(
        LIQUID_FIRFILT_RRC,
        samples_per_symbol,
        delay_symbols,
        rolloff,
        0.0F
    );
    rrc->matched_filter = firfilt_crcf_create_rnyquist(
        LIQUID_FIRFILT_RRC,
        samples_per_symbol,
        delay_symbols,
        rolloff,
        0.0F
    );
    rrc->samples_per_symbol = samples_per_symbol;
    rrc->delay_symbols = delay_symbols;
    if (rrc->interpolator == NULL || rrc->matched_filter == NULL) {
        rtnc_rrc_deinit(rrc);
        return false;
    }
    (void) firinterp_crcf_set_scale((firinterp_crcf) rrc->interpolator, sqrtf((float) samples_per_symbol));
    return true;
}

void rtnc_rrc_deinit(rtnc_rrc_t *rrc) {
    if (rrc == NULL) {
        return;
    }
    if (rrc->interpolator != NULL) {
        (void) firinterp_crcf_destroy((firinterp_crcf) rrc->interpolator);
    }
    if (rrc->matched_filter != NULL) {
        (void) firfilt_crcf_destroy((firfilt_crcf) rrc->matched_filter);
    }
    rrc->interpolator = NULL;
    rrc->matched_filter = NULL;
    rrc->samples_per_symbol = 0U;
    rrc->delay_symbols = 0U;
}

void rtnc_rrc_reset(rtnc_rrc_t *rrc) {
    if (rrc == NULL) {
        return;
    }
    if (rrc->interpolator != NULL) {
        (void) firinterp_crcf_reset((firinterp_crcf) rrc->interpolator);
    }
    if (rrc->matched_filter != NULL) {
        (void) firfilt_crcf_reset((firfilt_crcf) rrc->matched_filter);
    }
}

bool rtnc_rrc_interpolate(rtnc_rrc_t *rrc, float complex symbol, float complex *samples) {
    if (rrc == NULL || rrc->interpolator == NULL || samples == NULL) {
        return false;
    }
    return firinterp_crcf_execute((firinterp_crcf) rrc->interpolator, symbol, samples) == LIQUID_OK;
}

bool rtnc_rrc_match(rtnc_rrc_t *rrc, float complex sample, float complex *filtered) {
    if (rrc == NULL || rrc->matched_filter == NULL || filtered == NULL) {
        return false;
    }
    return firfilt_crcf_execute_one((firfilt_crcf) rrc->matched_filter, sample, filtered) == LIQUID_OK;
}
