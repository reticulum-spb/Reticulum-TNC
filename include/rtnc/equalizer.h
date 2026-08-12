#ifndef RTNC_EQUALIZER_H
#define RTNC_EQUALIZER_H

#include <complex.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    void  *equalizer;
    size_t tap_count;
} rtnc_equalizer_t;

/** Create a fixed-length complex RLS equalizer for a 2-samples/symbol input. */
bool rtnc_equalizer_init(rtnc_equalizer_t *equalizer, size_t tap_count, float forgetting_factor);
void rtnc_equalizer_deinit(rtnc_equalizer_t *equalizer);
void rtnc_equalizer_reset(rtnc_equalizer_t *equalizer);

/** Decimate one two-sample input pair and optionally train on a known symbol. */
bool rtnc_equalizer_execute(rtnc_equalizer_t *equalizer, const float complex samples[2], bool training, float complex desired, float complex *output, float *error_magnitude);

/** Apply one decision-directed update to the most recently executed sample. */
bool rtnc_equalizer_adapt(rtnc_equalizer_t *equalizer, float complex desired, float complex output);

/** Copy the current coefficients for receiver diagnostics. */
bool rtnc_equalizer_copy_taps(const rtnc_equalizer_t *equalizer, float complex *taps, size_t capacity);

#endif
