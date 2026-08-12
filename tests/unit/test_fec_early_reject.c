#include "rtnc/fec.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

enum {
    BLOCKS = 9U,
    ROBUST_BITS = 128U,
    OUTPUT_BYTES = BLOCKS * 8U,
};

int main(void) {
    float                   llr[BLOCKS * ROBUST_BITS];
    uint8_t                 output[OUTPUT_BYTES];
    size_t                  output_bytes = OUTPUT_BYTES;
    rtnc_fec_decode_stats_t stats;
    uint32_t                state = 0x13579bdfU;
    size_t                  bit;

    /* Eight strong all-zero codewords plus one deliberately weak random
     * block. The weak block is decoded first and must terminate the complete
     * frame as soon as its 30-iteration budget is exhausted. */
    for (bit = 0U; bit < sizeof(llr) / sizeof(llr[0]); ++bit) {
        llr[bit] = 4.0F;
    }
    for (bit = 4U * ROBUST_BITS; bit < 5U * ROBUST_BITS; ++bit) {
        state ^= state << 13U;
        state ^= state >> 17U;
        state ^= state << 5U;
        llr[bit] = (state & 1U) != 0U ? -0.10F : 0.10F;
    }
    assert(rtnc_fec_decode(FEC_LDPC_ROBUST, llr, sizeof(llr) / sizeof(llr[0]), output, sizeof(output), &output_bytes, &stats) == RTNC_FEC_DECODE_FAILURE);
    assert(!stats.converged);
    assert(stats.iterations == 30U);
    assert(stats.syndrome_weight != 0U);
    assert(output_bytes == 0U);
    return 0;
}
