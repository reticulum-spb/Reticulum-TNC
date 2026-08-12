#include "ldpc.h"

#include <math.h>
#include <stddef.h>

static uint8_t parity8(uint8_t value) {
    value ^= value >> 4U;
    value &= 0x0fU;
    return (uint8_t) ((0x6996U >> (unsigned int) value) & 1U);
}

bool rtnc_ldpc_encode_systematic(const rtnc_ldpc_code_t *code, const uint8_t *information, size_t information_bytes, uint8_t *codeword, size_t codeword_bytes) {
    size_t       index;
    const size_t required_information = code != NULL
                                            ? (size_t) code->information_count / 8U
                                            : 0U;
    const size_t required_codeword =
        code != NULL ? (size_t) code->variable_count / 8U : 0U;
    if (code == NULL || information == NULL || codeword == NULL ||
        code->parity_masks == NULL || code->information_count == 0U ||
        (code->information_count % 8U) != 0U ||
        (code->variable_count % 8U) != 0U ||
        information_bytes != required_information ||
        codeword_bytes < required_codeword) {
        return false;
    }
    for (index = 0U; index < information_bytes; ++index) {
        codeword[index] = information[index];
    }
    for (index = 0U;
         index < (size_t) (code->variable_count - code->information_count);
         ++index) {
        uint8_t      bit = 0U;
        size_t       information_index;
        const size_t output_bit = (size_t) code->information_count + index;
        for (information_index = 0U;
             information_index < required_information;
             ++information_index) {
            bit ^= parity8((uint8_t) (information[information_index] &
                                      code->parity_masks[index * required_information + information_index]));
        }
        if ((output_bit % 8U) == 0U) {
            codeword[output_bit / 8U] = 0U;
        }
        if (bit != 0U) {
            codeword[output_bit / 8U] |=
                (uint8_t) (1U << (7U - (unsigned int) (output_bit % 8U)));
        }
    }
    return true;
}

static unsigned int update_hard_bits(const rtnc_ldpc_code_t *code, const float *posterior, uint8_t *bits) {
    unsigned int syndrome_weight = 0U;
    uint16_t     variable;
    uint16_t     check;
    for (variable = 0U; variable < code->variable_count; ++variable) {
        bits[variable] = posterior[variable] < 0.0F ? 1U : 0U;
    }
    for (check = 0U; check < code->check_count; ++check) {
        uint8_t  parity = 0U;
        uint16_t edge;
        for (edge = code->check_offsets[check];
             edge < code->check_offsets[check + 1U];
             ++edge) {
            parity ^= bits[code->edge_variables[edge]];
        }
        syndrome_weight += parity != 0U ? 1U : 0U;
    }
    return syndrome_weight;
}

bool rtnc_ldpc_decode_normalized_min_sum(
    const rtnc_ldpc_code_t  *code,
    const float             *channel_llr,
    float                    normalization,
    unsigned int             max_iterations,
    uint8_t                 *decoded_bits,
    rtnc_ldpc_workspace_t   *workspace,
    rtnc_fec_decode_stats_t *stats
) {
    uint16_t     edge;
    unsigned int iteration;
    if (code == NULL || channel_llr == NULL || decoded_bits == NULL ||
        workspace == NULL || stats == NULL ||
        workspace->variable_to_check == NULL ||
        workspace->check_to_variable == NULL || workspace->posterior == NULL ||
        workspace->edge_capacity < code->edge_count ||
        workspace->variable_capacity < code->variable_count ||
        normalization <= 0.0F || normalization > 1.0F || max_iterations == 0U) {
        return false;
    }
    stats->converged = false;
    stats->iterations = 0U;
    stats->syndrome_weight = 0U;
    for (edge = 0U; edge < code->edge_count; ++edge) {
        workspace->variable_to_check[edge] =
            channel_llr[code->edge_variables[edge]];
        workspace->check_to_variable[edge] = 0.0F;
    }
    for (iteration = 1U; iteration <= max_iterations; ++iteration) {
        uint16_t check;
        uint16_t variable;
        for (check = 0U; check < code->check_count; ++check) {
            uint16_t target;
            for (target = code->check_offsets[check];
                 target < code->check_offsets[check + 1U];
                 ++target) {
                float    minimum = INFINITY;
                bool     negative = false;
                uint16_t other;
                for (other = code->check_offsets[check];
                     other < code->check_offsets[check + 1U];
                     ++other) {
                    float message;
                    if (other == target) {
                        continue;
                    }
                    message = workspace->variable_to_check[other];
                    negative ^= message < 0.0F;
                    if (fabsf(message) < minimum) {
                        minimum = fabsf(message);
                    }
                }
                workspace->check_to_variable[target] =
                    (negative ? -normalization : normalization) * minimum;
            }
        }
        for (variable = 0U; variable < code->variable_count; ++variable) {
            float posterior = channel_llr[variable];
            for (edge = 0U; edge < code->edge_count; ++edge) {
                if (code->edge_variables[edge] == variable) {
                    posterior += workspace->check_to_variable[edge];
                }
            }
            workspace->posterior[variable] = posterior;
        }
        stats->syndrome_weight =
            update_hard_bits(code, workspace->posterior, decoded_bits);
        stats->iterations = iteration;
        if (stats->syndrome_weight == 0U) {
            stats->converged = true;
            return true;
        }
        for (edge = 0U; edge < code->edge_count; ++edge) {
            workspace->variable_to_check[edge] =
                workspace->posterior[code->edge_variables[edge]] -
                workspace->check_to_variable[edge];
        }
    }
    return true;
}
