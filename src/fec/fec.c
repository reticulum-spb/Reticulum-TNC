#include "rtnc/fec.h"
#include "ldpc.h"
#include "codes/ccsds_tc128.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

enum {
    TC128_INFORMATION_BYTES = 8U,
    TC128_CODEWORD_BYTES = 16U,
    TC128_PUNCTURED_BYTES = 12U,
    TC128_MAX_BLOCKS = 20U,
};

static void weakest_blocks_first(const float *llr, size_t block_count, size_t bits_per_block, size_t order[TC128_MAX_BLOCKS]) {
    float  confidence[TC128_MAX_BLOCKS];
    size_t block;
    for (block = 0U; block < block_count; ++block) {
        size_t bit;
        float  sum = 0.0F;
        order[block] = block;
        for (bit = 0U; bit < bits_per_block; ++bit) {
            sum += fabsf(llr[block * bits_per_block + bit]);
        }
        confidence[block] = sum;
    }
    for (block = 1U; block < block_count; ++block) {
        const size_t candidate = order[block];
        size_t       position = block;
        while (position > 0U &&
               confidence[order[position - 1U]] > confidence[candidate]) {
            order[position] = order[position - 1U];
            --position;
        }
        order[position] = candidate;
    }
}

/*
 * TC128 systematic bits occupy positions 0..63. NORMAL transmits parity
 * positions 96..127 and treats positions 64..95 as erasures. This mask was
 * selected by a deterministic AWGN comparison against alternating and
 * contiguous alternatives.
 */

size_t rtnc_fec_encoded_size(fec_mode_t mode, size_t input_bytes) {
    if (mode == FEC_NONE) {
        return input_bytes;
    }
    if (mode == FEC_LDPC_ROBUST && input_bytes != 0U &&
        input_bytes <= (SIZE_MAX - 7U)) {
        return ((input_bytes + 7U) / 8U) * 16U;
    }
    if (mode == FEC_LDPC_NORMAL && input_bytes != 0U &&
        input_bytes <= (SIZE_MAX - 7U)) {
        return ((input_bytes + 7U) / 8U) * TC128_PUNCTURED_BYTES;
    }
    return 0U;
}

rtnc_fec_status_t rtnc_fec_encode(fec_mode_t mode, const uint8_t *input, size_t input_bytes, uint8_t *encoded, size_t capacity, size_t *encoded_bytes) {
    if (encoded == NULL || encoded_bytes == NULL ||
        (input == NULL && input_bytes != 0U)) {
        return RTNC_FEC_INVALID_ARGUMENT;
    }
    if (mode == FEC_LDPC_ROBUST) {
        const size_t required = rtnc_fec_encoded_size(mode, input_bytes);
        size_t       block;
        if (required == 0U) {
            return RTNC_FEC_INVALID_BLOCK;
        }
        if (capacity < required) {
            return RTNC_FEC_BUFFER_TOO_SMALL;
        }
        for (block = 0U; block < required / 16U; ++block) {
            uint8_t      information[8] = { 0U };
            const size_t offset = block * 8U;
            const size_t remaining = input_bytes - offset;
            const size_t copy_bytes = remaining < 8U ? remaining : 8U;
            (void) memcpy(information, &input[offset], copy_bytes);
            if (!rtnc_ldpc_encode_systematic(&ccsds_tc128, information, sizeof(information), &encoded[block * 16U], 16U)) {
                return RTNC_FEC_INVALID_BLOCK;
            }
        }
        *encoded_bytes = required;
        return RTNC_FEC_OK;
    }
    if (mode == FEC_LDPC_NORMAL) {
        const size_t required = rtnc_fec_encoded_size(mode, input_bytes);
        size_t       block;
        if (required == 0U) {
            return RTNC_FEC_INVALID_BLOCK;
        }
        if (capacity < required) {
            return RTNC_FEC_BUFFER_TOO_SMALL;
        }
        for (block = 0U; block < required / TC128_PUNCTURED_BYTES; ++block) {
            uint8_t      information[TC128_INFORMATION_BYTES] = { 0U };
            uint8_t      codeword[TC128_CODEWORD_BYTES] = { 0U };
            uint8_t     *punctured = &encoded[block * TC128_PUNCTURED_BYTES];
            const size_t offset = block * TC128_INFORMATION_BYTES;
            const size_t remaining = input_bytes - offset;
            const size_t copy_bytes =
                remaining < TC128_INFORMATION_BYTES
                    ? remaining
                    : TC128_INFORMATION_BYTES;
            (void) memcpy(information, &input[offset], copy_bytes);
            if (!rtnc_ldpc_encode_systematic(
                    &ccsds_tc128,
                    information,
                    sizeof(information),
                    codeword,
                    sizeof(codeword)
                )) {
                return RTNC_FEC_INVALID_BLOCK;
            }
            (void) memcpy(punctured, codeword, TC128_INFORMATION_BYTES);
            (void) memcpy(&punctured[TC128_INFORMATION_BYTES], &codeword[12], TC128_PUNCTURED_BYTES - TC128_INFORMATION_BYTES);
        }
        *encoded_bytes = required;
        return RTNC_FEC_OK;
    }
    if (mode != FEC_NONE) {
        return RTNC_FEC_UNSUPPORTED_MODE;
    }
    if (capacity < input_bytes) {
        return RTNC_FEC_BUFFER_TOO_SMALL;
    }
    if (input_bytes != 0U) {
        (void) memcpy(encoded, input, input_bytes);
    }
    *encoded_bytes = input_bytes;
    return RTNC_FEC_OK;
}

rtnc_fec_status_t rtnc_fec_decode(fec_mode_t mode, const float *llr, size_t llr_count, uint8_t *output, size_t capacity, size_t *output_bytes, rtnc_fec_decode_stats_t *stats) {
    size_t byte_count;
    size_t bit_index;
    if (llr == NULL || output == NULL || output_bytes == NULL || stats == NULL) {
        return RTNC_FEC_INVALID_ARGUMENT;
    }
    stats->converged = false;
    stats->iterations = 0U;
    stats->syndrome_weight = 0U;
    if (mode == FEC_LDPC_ROBUST) {
        const size_t block_count = llr_count / 128U;
        size_t       order[TC128_MAX_BLOCKS];
        size_t       order_index;
        unsigned int total_iterations = 0U;
        unsigned int total_syndrome = 0U;
        if (llr_count == 0U || (llr_count % 128U) != 0U ||
            block_count > TC128_MAX_BLOCKS) {
            return RTNC_FEC_INVALID_BLOCK;
        }
        if (capacity < block_count * 8U) {
            return RTNC_FEC_BUFFER_TOO_SMALL;
        }
        weakest_blocks_first(llr, block_count, 128U, order);
        for (order_index = 0U; order_index < block_count; ++order_index) {
            const size_t            block = order[order_index];
            float                   variable_to_check[512];
            float                   check_to_variable[512];
            float                   posterior[128];
            uint8_t                 decoded_bits[128];
            rtnc_fec_decode_stats_t block_stats;
            rtnc_ldpc_workspace_t   workspace = {
                  .variable_to_check = variable_to_check,
                  .check_to_variable = check_to_variable,
                  .posterior = posterior,
                  .edge_capacity = 512U,
                  .variable_capacity = 128U,
            };
            size_t bit;
            if (!rtnc_ldpc_decode_normalized_min_sum(
                    &ccsds_tc128,
                    &llr[block * 128U],
                    0.75F,
                    30U,
                    decoded_bits,
                    &workspace,
                    &block_stats
                )) {
                return RTNC_FEC_INVALID_BLOCK;
            }
            for (bit = 0U; bit < 64U; ++bit) {
                if ((bit % 8U) == 0U) {
                    output[block * 8U + bit / 8U] = 0U;
                }
                if (decoded_bits[bit] != 0U) {
                    output[block * 8U + bit / 8U] |=
                        (uint8_t) (1U << (7U - (unsigned int) (bit % 8U)));
                }
            }
            total_iterations += block_stats.iterations;
            total_syndrome += block_stats.syndrome_weight;
            if (!block_stats.converged) {
                *output_bytes = 0U;
                stats->iterations = total_iterations;
                stats->syndrome_weight = total_syndrome;
                return RTNC_FEC_DECODE_FAILURE;
            }
        }
        *output_bytes = block_count * 8U;
        stats->converged = true;
        stats->iterations = total_iterations;
        stats->syndrome_weight = total_syndrome;
        return RTNC_FEC_OK;
    }
    if (mode == FEC_LDPC_NORMAL) {
        const size_t block_count = llr_count / 96U;
        size_t       order[TC128_MAX_BLOCKS];
        size_t       order_index;
        unsigned int total_iterations = 0U;
        unsigned int total_syndrome = 0U;
        if (llr_count == 0U || (llr_count % 96U) != 0U ||
            block_count > TC128_MAX_BLOCKS) {
            return RTNC_FEC_INVALID_BLOCK;
        }
        if (capacity < block_count * TC128_INFORMATION_BYTES) {
            return RTNC_FEC_BUFFER_TOO_SMALL;
        }
        weakest_blocks_first(llr, block_count, 96U, order);
        for (order_index = 0U; order_index < block_count; ++order_index) {
            const size_t            block = order[order_index];
            float                   expanded_llr[128] = { 0.0F };
            float                   variable_to_check[512];
            float                   check_to_variable[512];
            float                   posterior[128];
            uint8_t                 decoded_bits[128];
            rtnc_fec_decode_stats_t block_stats;
            rtnc_ldpc_workspace_t   workspace = {
                  .variable_to_check = variable_to_check,
                  .check_to_variable = check_to_variable,
                  .posterior = posterior,
                  .edge_capacity = 512U,
                  .variable_capacity = 128U,
            };
            size_t bit;
            for (bit = 0U; bit < 64U; ++bit) {
                expanded_llr[bit] = llr[block * 96U + bit];
            }
            for (bit = 0U; bit < 32U; ++bit) {
                expanded_llr[96U + bit] =
                    llr[block * 96U + 64U + bit];
            }
            if (!rtnc_ldpc_decode_normalized_min_sum(
                    &ccsds_tc128,
                    expanded_llr,
                    0.75F,
                    30U,
                    decoded_bits,
                    &workspace,
                    &block_stats
                )) {
                return RTNC_FEC_INVALID_BLOCK;
            }
            for (bit = 0U; bit < 64U; ++bit) {
                if ((bit % 8U) == 0U) {
                    output[block * TC128_INFORMATION_BYTES + bit / 8U] = 0U;
                }
                if (decoded_bits[bit] != 0U) {
                    output[block * TC128_INFORMATION_BYTES + bit / 8U] |=
                        (uint8_t) (1U << (7U - (unsigned int) (bit % 8U)));
                }
            }
            total_iterations += block_stats.iterations;
            total_syndrome += block_stats.syndrome_weight;
            if (!block_stats.converged) {
                *output_bytes = 0U;
                stats->iterations = total_iterations;
                stats->syndrome_weight = total_syndrome;
                return RTNC_FEC_DECODE_FAILURE;
            }
        }
        *output_bytes = block_count * TC128_INFORMATION_BYTES;
        stats->converged = true;
        stats->iterations = total_iterations;
        stats->syndrome_weight = total_syndrome;
        return RTNC_FEC_OK;
    }
    if (mode != FEC_NONE) {
        return RTNC_FEC_UNSUPPORTED_MODE;
    }
    if ((llr_count % 8U) != 0U) {
        return RTNC_FEC_INVALID_BLOCK;
    }
    byte_count = llr_count / 8U;
    if (capacity < byte_count) {
        return RTNC_FEC_BUFFER_TOO_SMALL;
    }
    if (byte_count != 0U) {
        (void) memset(output, 0, byte_count);
    }
    for (bit_index = 0U; bit_index < llr_count; ++bit_index) {
        if (llr[bit_index] < 0.0F) {
            output[bit_index / 8U] |=
                (uint8_t) (1U << (7U - (unsigned int) (bit_index % 8U)));
        }
    }
    *output_bytes = byte_count;
    stats->converged = true;
    return RTNC_FEC_OK;
}
