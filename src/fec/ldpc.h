#ifndef RTNC_INTERNAL_LDPC_H
#define RTNC_INTERNAL_LDPC_H

#include "rtnc/fec.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t        variable_count;
    uint16_t        information_count;
    uint16_t        check_count;
    uint16_t        edge_count;
    const uint16_t *check_offsets;
    const uint16_t *edge_variables;
    const uint8_t  *parity_masks;
} rtnc_ldpc_code_t;

typedef struct {
    float *variable_to_check;
    float *check_to_variable;
    float *posterior;
    size_t edge_capacity;
    size_t variable_capacity;
} rtnc_ldpc_workspace_t;

bool rtnc_ldpc_decode_normalized_min_sum(
    const rtnc_ldpc_code_t  *code,
    const float             *channel_llr,
    float                    normalization,
    unsigned int             max_iterations,
    uint8_t                 *decoded_bits,
    rtnc_ldpc_workspace_t   *workspace,
    rtnc_fec_decode_stats_t *stats
);

/** Encode a byte-aligned systematic code from generated parity masks. */
bool rtnc_ldpc_encode_systematic(const rtnc_ldpc_code_t *code, const uint8_t *information, size_t information_bytes, uint8_t *codeword, size_t codeword_bytes);

#endif
