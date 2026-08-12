#ifndef RTNC_FEC_H
#define RTNC_FEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    FEC_NONE = 0,
    FEC_LDPC_ROBUST,
    FEC_LDPC_NORMAL,
    FEC_LDPC_FAST,
} fec_mode_t;

typedef enum {
    RTNC_FEC_OK = 0,
    RTNC_FEC_INVALID_ARGUMENT,
    RTNC_FEC_BUFFER_TOO_SMALL,
    RTNC_FEC_UNSUPPORTED_MODE,
    RTNC_FEC_INVALID_BLOCK,
    RTNC_FEC_DECODE_FAILURE,
} rtnc_fec_status_t;

typedef struct {
    bool         converged;
    unsigned int iterations;
    unsigned int syndrome_weight;
} rtnc_fec_decode_stats_t;

/** Return encoded bytes required for a complete input block, or zero. */
size_t rtnc_fec_encoded_size(fec_mode_t mode, size_t input_bytes);

/** Encode one complete block into caller-owned storage. */
rtnc_fec_status_t rtnc_fec_encode(fec_mode_t mode, const uint8_t *input, size_t input_bytes, uint8_t *encoded, size_t capacity, size_t *encoded_bytes);

/**
 * Decode one complete block from signed soft bits. Positive means bit zero,
 * negative means bit one; magnitude expresses confidence.
 */
rtnc_fec_status_t rtnc_fec_decode(fec_mode_t mode, const float *llr, size_t llr_count, uint8_t *output, size_t capacity, size_t *output_bytes, rtnc_fec_decode_stats_t *stats);

#endif
