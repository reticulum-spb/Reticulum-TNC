#include "rtnc/fec.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

int main(void) {
    const uint8_t           input[] = { 0x00U, 0x5aU, 0xa5U, 0xffU };
    uint8_t                 encoded[sizeof(input)];
    uint8_t                 decoded[sizeof(input)];
    float                   llr[sizeof(input) * 8U];
    size_t                  encoded_bytes = 0U;
    size_t                  decoded_bytes = 0U;
    size_t                  bit_index;
    rtnc_fec_decode_stats_t stats;

    assert(rtnc_fec_encoded_size(FEC_NONE, sizeof(input)) == sizeof(input));
    assert(rtnc_fec_encoded_size(FEC_LDPC_FAST, sizeof(input)) == 0U);
    assert(rtnc_fec_encode(FEC_NONE, input, sizeof(input), encoded, sizeof(encoded), &encoded_bytes) == RTNC_FEC_OK);
    assert(encoded_bytes == sizeof(input));
    for (bit_index = 0U; bit_index < encoded_bytes * 8U; ++bit_index) {
        const bool one =
            (((unsigned int) encoded[bit_index / 8U] >>
              (7U - (unsigned int) (bit_index % 8U))) &
             1U) != 0U;
        llr[bit_index] = one ? -0.75F : 0.75F;
    }
    assert(rtnc_fec_decode(FEC_NONE, llr, sizeof(llr) / sizeof(llr[0]), decoded, sizeof(decoded), &decoded_bytes, &stats) == RTNC_FEC_OK);
    assert(stats.converged);
    assert(stats.iterations == 0U);
    assert(decoded_bytes == sizeof(input));
    assert(memcmp(input, decoded, sizeof(input)) == 0);
    assert(rtnc_fec_decode(FEC_NONE, llr, 7U, decoded, sizeof(decoded), &decoded_bytes, &stats) == RTNC_FEC_INVALID_BLOCK);
    assert(rtnc_fec_encode(FEC_LDPC_FAST, input, sizeof(input), encoded, sizeof(encoded), &encoded_bytes) == RTNC_FEC_UNSUPPORTED_MODE);
    {
        const uint8_t robust_input[13] = {
            0x10U,
            0x21U,
            0x32U,
            0x43U,
            0x54U,
            0x65U,
            0x76U,
            0x87U,
            0x98U,
            0xa9U,
            0xbaU,
            0xcbU,
            0xdcU,
        };
        uint8_t robust_encoded[32];
        uint8_t robust_decoded[16];
        float   robust_llr[256];
        size_t  robust_encoded_bytes = 0U;
        size_t  robust_decoded_bytes = 0U;
        assert(rtnc_fec_encoded_size(FEC_LDPC_ROBUST, sizeof(robust_input)) == 32U);
        assert(rtnc_fec_encode(FEC_LDPC_ROBUST, robust_input, sizeof(robust_input), robust_encoded, sizeof(robust_encoded), &robust_encoded_bytes) == RTNC_FEC_OK);
        assert(robust_encoded_bytes == sizeof(robust_encoded));
        for (bit_index = 0U; bit_index < 256U; ++bit_index) {
            const bool one =
                (((unsigned int) robust_encoded[bit_index / 8U] >>
                  (7U - (unsigned int) (bit_index % 8U))) &
                 1U) != 0U;
            robust_llr[bit_index] = one ? -2.5F : 2.5F;
        }
        robust_llr[9] = -robust_llr[9] * 0.1F;
        robust_llr[193] = -robust_llr[193] * 0.1F;
        assert(rtnc_fec_decode(FEC_LDPC_ROBUST, robust_llr, 256U, robust_decoded, sizeof(robust_decoded), &robust_decoded_bytes, &stats) == RTNC_FEC_OK);
        assert(stats.converged);
        assert(robust_decoded_bytes == sizeof(robust_decoded));
        assert(memcmp(robust_input, robust_decoded, sizeof(robust_input)) == 0);
        assert(robust_decoded[13] == 0U);
        assert(robust_decoded[14] == 0U);
        assert(robust_decoded[15] == 0U);
    }
    {
        uint8_t normal_input[67];
        uint8_t normal_encoded[108];
        uint8_t normal_decoded[72];
        float   normal_llr[864];
        size_t  normal_encoded_bytes = 0U;
        size_t  normal_decoded_bytes = 0U;
        size_t  index;
        for (index = 0U; index < sizeof(normal_input); ++index) {
            normal_input[index] = (uint8_t) (index ^ 0x96U);
        }
        assert(rtnc_fec_encoded_size(FEC_LDPC_NORMAL, sizeof(normal_input)) == 108U);
        assert(rtnc_fec_encode(FEC_LDPC_NORMAL, normal_input, sizeof(normal_input), normal_encoded, sizeof(normal_encoded), &normal_encoded_bytes) == RTNC_FEC_OK);
        for (index = 0U; index < normal_encoded_bytes * 8U; ++index) {
            const bool one =
                (((unsigned int) normal_encoded[index / 8U] >>
                  (7U - (unsigned int) (index % 8U))) &
                 1U) != 0U;
            normal_llr[index] = one ? -3.0F : 3.0F;
        }
        normal_llr[33] = -normal_llr[33] * 0.1F;
        assert(rtnc_fec_decode(FEC_LDPC_NORMAL, normal_llr, normal_encoded_bytes * 8U, normal_decoded, sizeof(normal_decoded), &normal_decoded_bytes, &stats) == RTNC_FEC_OK);
        assert(normal_decoded_bytes == sizeof(normal_decoded));
        assert(memcmp(normal_input, normal_decoded, sizeof(normal_input)) == 0);
    }
    return 0;
}
