#include "../../src/fec/codes/ccsds_tc128.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

int main(void) {
    const uint8_t information[8] = { 0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U };
    const uint8_t expected_parity[8] = {
        0x34U,
        0x99U,
        0x98U,
        0x87U,
        0x94U,
        0xe1U,
        0x62U,
        0x56U,
    };
    uint8_t                 codeword[16];
    float                   llr[128];
    float                   variable_to_check[512];
    float                   check_to_variable[512];
    float                   posterior[128];
    uint8_t                 decoded[128];
    rtnc_fec_decode_stats_t stats;
    rtnc_ldpc_workspace_t   workspace = {
          .variable_to_check = variable_to_check,
          .check_to_variable = check_to_variable,
          .posterior = posterior,
          .edge_capacity = 512U,
          .variable_capacity = 128U,
    };
    size_t bit;

    assert(rtnc_ldpc_encode_systematic(&ccsds_tc128, information, sizeof(information), codeword, sizeof(codeword)));
    assert(memcmp(codeword, information, sizeof(information)) == 0);
    assert(memcmp(&codeword[8], expected_parity, sizeof(expected_parity)) == 0);
    for (bit = 0U; bit < 128U; ++bit) {
        const bool one = (((unsigned int) codeword[bit / 8U] >>
                           (7U - (unsigned int) (bit % 8U))) &
                          1U) != 0U;
        llr[bit] = one ? -3.0F : 3.0F;
    }
    llr[17] = -llr[17] * 0.1F;
    assert(rtnc_ldpc_decode_normalized_min_sum(
        &ccsds_tc128,
        llr,
        0.75F,
        20U,
        decoded,
        &workspace,
        &stats
    ));
    assert(stats.converged);
    for (bit = 0U; bit < 64U; ++bit) {
        const uint8_t expected =
            (uint8_t) (((unsigned int) information[bit / 8U] >>
                        (7U - (unsigned int) (bit % 8U))) &
                       1U);
        assert(decoded[bit] == expected);
    }
    return 0;
}
