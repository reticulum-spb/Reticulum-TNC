#include "rtnc/psk.h"

#include <assert.h>
#include <complex.h>
#include <stdio.h>

int main(void) {
    rtnc_psk_t   psk = { 0 };
    unsigned int value;
    assert(rtnc_psk_init(&psk, RTNC_MODULATION_BPSK));
    for (value = 0U; value < 2U; ++value) {
        float complex sample;
        uint8_t       hard = 99U;
        float         llr[4] = { 0.0F };
        float         evm = 0.0F;
        assert(rtnc_psk_map(&psk, (uint8_t) value, &sample));
        assert(rtnc_psk_demap_soft(&psk, sample, &hard, llr, &evm));
        (void) printf("value=%u hard=%u llr=%.6f evm=%.6f\n", value, (unsigned int) hard, (double) llr[0], (double) evm);
        assert(hard == value);
        assert((value == 0U && llr[0] > 0.0F) || (value == 1U && llr[0] < 0.0F));
    }
    rtnc_psk_deinit(&psk);
    return 0;
}
