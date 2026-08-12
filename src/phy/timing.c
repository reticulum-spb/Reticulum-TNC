#include "rtnc/timing.h"

#include <liquid/liquid.h>
#include <stddef.h>

bool rtnc_timing_init(rtnc_timing_t *timing, uint16_t samples_per_symbol, uint16_t delay_symbols, float rolloff, float loop_bandwidth) {
    if (timing == NULL || samples_per_symbol < 2U || delay_symbols == 0U ||
        rolloff <= 0.0F || rolloff > 1.0F || loop_bandwidth < 0.0F ||
        loop_bandwidth > 1.0F) {
        return false;
    }
    timing->synchronizer = symsync_crcf_create_rnyquist(
        LIQUID_FIRFILT_RRC,
        samples_per_symbol,
        delay_symbols,
        rolloff,
        32U
    );
    if (timing->synchronizer == NULL) {
        return false;
    }
    (void) symsync_crcf_set_output_rate((symsync_crcf) timing->synchronizer, 1U);
    (void) symsync_crcf_set_lf_bw((symsync_crcf) timing->synchronizer, loop_bandwidth);
    return true;
}

void rtnc_timing_deinit(rtnc_timing_t *timing) {
    if (timing == NULL) {
        return;
    }
    if (timing->synchronizer != NULL) {
        (void) symsync_crcf_destroy((symsync_crcf) timing->synchronizer);
    }
    timing->synchronizer = NULL;
}

void rtnc_timing_reset(rtnc_timing_t *timing) {
    if (timing != NULL && timing->synchronizer != NULL) {
        (void) symsync_crcf_reset((symsync_crcf) timing->synchronizer);
    }
}

bool rtnc_timing_set_output_rate(rtnc_timing_t *timing, uint16_t samples_per_symbol) {
    if (timing == NULL || timing->synchronizer == NULL ||
        samples_per_symbol == 0U || samples_per_symbol > 4U) {
        return false;
    }
    return symsync_crcf_set_output_rate(
               (symsync_crcf) timing->synchronizer,
               (unsigned int) samples_per_symbol
           ) == LIQUID_OK;
}

bool rtnc_timing_execute(rtnc_timing_t *timing, float complex sample, float complex *symbols, size_t capacity, size_t *symbol_count) {
    float complex temporary[4];
    unsigned int  produced = 0U;
    size_t        index;
    if (timing == NULL || timing->synchronizer == NULL || symbols == NULL ||
        symbol_count == NULL) {
        return false;
    }
    if (symsync_crcf_execute((symsync_crcf) timing->synchronizer, &sample, 1U, temporary, &produced) != LIQUID_OK ||
        produced > 4U || capacity < produced) {
        return false;
    }
    for (index = 0U; index < produced; ++index) {
        symbols[index] = temporary[index];
    }
    *symbol_count = produced;
    return true;
}

float rtnc_timing_offset(const rtnc_timing_t *timing) {
    if (timing == NULL || timing->synchronizer == NULL) {
        return 0.0F;
    }
    return symsync_crcf_get_tau((symsync_crcf) timing->synchronizer);
}
