#include "rtnc/fec.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { BLOCKS_PER_POINT = 400U };

static uint32_t random_state = 0x91e10da5U;

static uint32_t random_u32(void) {
    random_state = random_state * 1664525U + 1013904223U;
    return random_state;
}

static float uniform_open(void) {
    return ((float) (random_u32() >> 8U) + 0.5F) / 16777216.0F;
}

static float gaussian(void) {
    return sqrtf(-2.0F * logf(uniform_open())) *
           cosf(2.0F * (float) M_PI * uniform_open());
}

static bool packed_bit(const uint8_t *bytes, size_t bit) {
    return (((unsigned int) bytes[bit / 8U] >>
             (7U - (unsigned int) (bit % 8U))) &
            1U) != 0U;
}

int main(void) {
    static const float ebn0_db[] = { 0.0F, 2.0F, 4.0F, 6.0F };
    unsigned int       first_coded_failures = 0U;
    unsigned int       last_coded_failures = 0U;
    unsigned int       first_normal_failures = 0U;
    unsigned int       last_normal_failures = 0U;
    size_t             point;

    (void) printf("EbN0_dB,uncoded_FER,robust_FER,normal_FER,"
                  "uncoded_goodput,robust_goodput,normal_goodput\n");
    for (point = 0U; point < sizeof(ebn0_db) / sizeof(ebn0_db[0]); ++point) {
        const float  ebn0 = powf(10.0F, ebn0_db[point] / 10.0F);
        const float  uncoded_sigma = sqrtf(1.0F / (2.0F * ebn0));
        const float  coded_sigma = sqrtf(1.0F / ebn0);   /* rate 1/2 */
        const float  normal_sigma = sqrtf(0.75F / ebn0); /* rate 2/3 */
        unsigned int uncoded_failures = 0U;
        unsigned int coded_failures = 0U;
        unsigned int normal_failures = 0U;
        unsigned int block;
        random_state = 0x91e10da5U + (uint32_t) point;
        for (block = 0U; block < BLOCKS_PER_POINT; ++block) {
            uint8_t                 information[8];
            uint8_t                 encoded[16];
            uint8_t                 decoded[8];
            uint8_t                 normal_encoded[12];
            uint8_t                 normal_decoded[8];
            float                   llr[128];
            float                   normal_llr[96];
            size_t                  encoded_bytes = 0U;
            size_t                  decoded_bytes = 0U;
            rtnc_fec_decode_stats_t stats;
            bool                    uncoded_error = false;
            size_t                  index;
            for (index = 0U; index < sizeof(information); ++index) {
                information[index] = (uint8_t) (random_u32() >> 24U);
            }
            assert(rtnc_fec_encode(FEC_LDPC_ROBUST, information, sizeof(information), encoded, sizeof(encoded), &encoded_bytes) == RTNC_FEC_OK);
            assert(rtnc_fec_encode(FEC_LDPC_NORMAL, information, sizeof(information), normal_encoded, sizeof(normal_encoded), &encoded_bytes) == RTNC_FEC_OK);
            for (index = 0U; index < 64U; ++index) {
                const float symbol = packed_bit(information, index) ? -1.0F : 1.0F;
                const float received = symbol + uncoded_sigma * gaussian();
                if ((received < 0.0F) != packed_bit(information, index)) {
                    uncoded_error = true;
                }
            }
            uncoded_failures += uncoded_error ? 1U : 0U;
            for (index = 0U; index < 128U; ++index) {
                const float symbol = packed_bit(encoded, index) ? -1.0F : 1.0F;
                const float received = symbol + coded_sigma * gaussian();
                llr[index] = received;
            }
            if (rtnc_fec_decode(FEC_LDPC_ROBUST, llr, 128U, decoded, sizeof(decoded), &decoded_bytes, &stats) !=
                    RTNC_FEC_OK ||
                decoded_bytes != sizeof(decoded) ||
                memcmp(information, decoded, sizeof(information)) != 0) {
                ++coded_failures;
            }
            for (index = 0U; index < 96U; ++index) {
                const float symbol =
                    packed_bit(normal_encoded, index) ? -1.0F : 1.0F;
                normal_llr[index] = symbol + normal_sigma * gaussian();
            }
            if (rtnc_fec_decode(FEC_LDPC_NORMAL, normal_llr, 96U, normal_decoded, sizeof(normal_decoded), &decoded_bytes, &stats) != RTNC_FEC_OK ||
                decoded_bytes != sizeof(normal_decoded) ||
                memcmp(information, normal_decoded, sizeof(information)) != 0) {
                ++normal_failures;
            }
        }
        if (point == 0U) {
            first_coded_failures = coded_failures;
            first_normal_failures = normal_failures;
        }
        last_coded_failures = coded_failures;
        last_normal_failures = normal_failures;
        (void) printf("%.1f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n", (double) ebn0_db[point], (double) uncoded_failures / BLOCKS_PER_POINT, (double) coded_failures / BLOCKS_PER_POINT, (double) normal_failures / BLOCKS_PER_POINT, (double) (BLOCKS_PER_POINT - uncoded_failures) / BLOCKS_PER_POINT, 0.5 * (double) (BLOCKS_PER_POINT - coded_failures) / BLOCKS_PER_POINT, (2.0 / 3.0) * (double) (BLOCKS_PER_POINT - normal_failures) / BLOCKS_PER_POINT);
    }
    assert(first_coded_failures > last_coded_failures);
    assert(last_coded_failures < BLOCKS_PER_POINT / 20U);
    assert(first_normal_failures > last_normal_failures);
    assert(last_normal_failures < BLOCKS_PER_POINT / 20U);
    return 0;
}
