#ifndef RTNC_TIMING_H
#define RTNC_TIMING_H

#include <complex.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    void *synchronizer;
} rtnc_timing_t;

/** Create an RRC matched-filter symbol synchronizer outside the sample path. */
bool rtnc_timing_init(rtnc_timing_t *timing, uint16_t samples_per_symbol, uint16_t delay_symbols, float rolloff, float loop_bandwidth);
void rtnc_timing_deinit(rtnc_timing_t *timing);
void rtnc_timing_reset(rtnc_timing_t *timing);
bool rtnc_timing_set_output_rate(rtnc_timing_t *timing, uint16_t samples_per_symbol);

/** Process one complex input sample, producing zero or more recovered symbols. */
bool rtnc_timing_execute(rtnc_timing_t *timing, float complex sample, float complex *symbols, size_t capacity, size_t *symbol_count);

/** Current fractional timing estimate in symbol periods. */
float rtnc_timing_offset(const rtnc_timing_t *timing);

#endif
