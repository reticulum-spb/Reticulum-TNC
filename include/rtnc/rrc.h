#ifndef RTNC_RRC_H
#define RTNC_RRC_H

#include <complex.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    void    *interpolator;
    void    *matched_filter;
    uint16_t samples_per_symbol;
    uint16_t delay_symbols;
} rtnc_rrc_t;

/** Create a matched TX/RX root-raised-cosine pair outside the sample path. */
bool rtnc_rrc_init(rtnc_rrc_t *rrc, uint16_t samples_per_symbol, uint16_t delay_symbols, float rolloff);

void rtnc_rrc_deinit(rtnc_rrc_t *rrc);
void rtnc_rrc_reset(rtnc_rrc_t *rrc);

/** Produce exactly samples_per_symbol output samples for one input symbol. */
bool rtnc_rrc_interpolate(rtnc_rrc_t *rrc, float complex symbol, float complex *samples);

/** Process one input sample through the matched RRC filter. */
bool rtnc_rrc_match(rtnc_rrc_t *rrc, float complex sample, float complex *filtered);

#endif
