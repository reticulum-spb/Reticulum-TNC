#include "../../src/fec/codes/ldpc_test_7_4.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

static void encode(uint8_t data, uint8_t bits[7]) {
    bits[0] = (uint8_t) ((data >> 3U) & 1U);
    bits[1] = (uint8_t) ((data >> 2U) & 1U);
    bits[2] = (uint8_t) ((data >> 1U) & 1U);
    bits[3] = (uint8_t) (data & 1U);
    bits[4] = (uint8_t) (bits[0] ^ bits[1] ^ bits[3]);
    bits[5] = (uint8_t) (bits[0] ^ bits[2] ^ bits[3]);
    bits[6] = (uint8_t) (bits[1] ^ bits[2] ^ bits[3]);
}

int main(void) {
    uint8_t data;
    for (data = 0U; data < 16U; ++data) {
        uint8_t      transmitted[7];
        unsigned int error_position;
        encode(data, transmitted);
        for (error_position = 0U; error_position < 7U; ++error_position) {
            float                   channel_llr[7];
            float                   variable_to_check[12];
            float                   check_to_variable[12];
            float                   posterior[7];
            uint8_t                 decoded[7];
            rtnc_fec_decode_stats_t stats;
            rtnc_ldpc_workspace_t   workspace = {
                  .variable_to_check = variable_to_check,
                  .check_to_variable = check_to_variable,
                  .posterior = posterior,
                  .edge_capacity = 12U,
                  .variable_capacity = 7U,
            };
            size_t bit;
            for (bit = 0U; bit < 7U; ++bit) {
                channel_llr[bit] = transmitted[bit] != 0U ? -2.0F : 2.0F;
            }
            channel_llr[error_position] =
                transmitted[error_position] != 0U ? 0.4F : -0.4F;
            assert(rtnc_ldpc_decode_normalized_min_sum(
                &ldpc_test_7_4,
                channel_llr,
                0.75F,
                8U,
                decoded,
                &workspace,
                &stats
            ));
            assert(stats.converged);
            assert(stats.syndrome_weight == 0U);
            for (bit = 0U; bit < 4U; ++bit) {
                assert(decoded[bit] == transmitted[bit]);
            }
        }
    }
    {
        const float             contradictory[7] = { -100.0F, 100.0F, 100.0F, 100.0F, 100.0F, 100.0F, 100.0F };
        float                   variable_to_check[12];
        float                   check_to_variable[12];
        float                   posterior[7];
        uint8_t                 decoded[7];
        rtnc_fec_decode_stats_t stats;
        rtnc_ldpc_workspace_t   workspace = {
              .variable_to_check = variable_to_check,
              .check_to_variable = check_to_variable,
              .posterior = posterior,
              .edge_capacity = 12U,
              .variable_capacity = 7U,
        };
        assert(rtnc_ldpc_decode_normalized_min_sum(
            &ldpc_test_7_4,
            contradictory,
            0.01F,
            1U,
            decoded,
            &workspace,
            &stats
        ));
        assert(!stats.converged);
        assert(stats.iterations == 1U);
        assert(stats.syndrome_weight != 0U);
    }
    return 0;
}
