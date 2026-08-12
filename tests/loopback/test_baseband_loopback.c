#include "rtnc/phy.h"
#include "rtnc/psk.h"
#include "rtnc/rrc.h"

#include <assert.h>
#include <complex.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

enum {
    TRAINING_SYMBOLS = 32,
    FILTER_DELAY_SYMBOLS = 7,
    TRAILING_SYMBOLS = 16,
    UNKNOWN_SAMPLE_OFFSET = 23,
    MAX_SAMPLES = 4096,
};

int main(void) {
    const rtnc_phy_profile_t profile = rtnc_phy_profile_qpsk_1200();
    const uint8_t            training[TRAINING_SYMBOLS] = {
        0U,
        3U,
        1U,
        1U,
        2U,
        0U,
        3U,
        2U,
        1U,
        0U,
        2U,
        3U,
        3U,
        0U,
        1U,
        2U,
        2U,
        1U,
        3U,
        0U,
        1U,
        3U,
        2U,
        2U,
        0U,
        1U,
        0U,
        3U,
        2U,
        3U,
        1U,
        0U,
    };
    float complex reference[TRAINING_SYMBOLS];
    float complex channel[MAX_SAMPLES] = { 0.0F };
    float complex filtered[MAX_SAMPLES] = { 0.0F };
    rtnc_psk_t    psk = { 0 };
    rtnc_rrc_t    rrc = { 0 };
    size_t        write_index = UNKNOWN_SAMPLE_OFFSET;
    size_t        sample_count;
    size_t        symbol_index;
    size_t        sample_index;
    size_t        best_start = 0U;
    float         best_score = 0.0F;

    assert(rtnc_psk_init(&psk, RTNC_MODULATION_QPSK));
    assert(rtnc_rrc_init(&rrc, profile.samples_per_symbol, FILTER_DELAY_SYMBOLS, profile.rrc_rolloff));

    for (symbol_index = 0U; symbol_index < TRAINING_SYMBOLS; ++symbol_index) {
        assert(rtnc_psk_map(&psk, training[symbol_index], &reference[symbol_index]));
        assert(write_index + profile.samples_per_symbol <= MAX_SAMPLES);
        assert(rtnc_rrc_interpolate(&rrc, reference[symbol_index], &channel[write_index]));
        write_index += profile.samples_per_symbol;
    }
    for (symbol_index = 0U; symbol_index < TRAILING_SYMBOLS; ++symbol_index) {
        assert(rtnc_rrc_interpolate(&rrc, 0.0F, &channel[write_index]));
        write_index += profile.samples_per_symbol;
    }
    sample_count = write_index;
    for (sample_index = 0U; sample_index < sample_count; ++sample_index) {
        assert(rtnc_rrc_match(&rrc, channel[sample_index], &filtered[sample_index]));
    }

    /* Exhaustive training correlation: no external knowledge of the offset. */
    for (sample_index = 0U;
         sample_index + (TRAINING_SYMBOLS - 1U) * profile.samples_per_symbol <
         sample_count;
         ++sample_index) {
        float complex correlation = 0.0F;
        float         received_energy = 0.0F;
        for (symbol_index = 0U; symbol_index < TRAINING_SYMBOLS; ++symbol_index) {
            const float complex received =
                filtered[sample_index + symbol_index * profile.samples_per_symbol];
            correlation += conjf(reference[symbol_index]) * received;
            received_energy += crealf(received * conjf(received));
        }
        if (received_energy > 0.0F) {
            const float score = cabsf(correlation) /
                                sqrtf((float) TRAINING_SYMBOLS * received_energy);
            if (score > best_score) {
                best_score = score;
                best_start = sample_index;
            }
        }
    }

    assert(best_score > 0.999F);
    assert(best_start == UNKNOWN_SAMPLE_OFFSET + 2U * FILTER_DELAY_SYMBOLS * profile.samples_per_symbol);
    for (symbol_index = 0U; symbol_index < TRAINING_SYMBOLS; ++symbol_index) {
        uint8_t recovered = 0xffU;
        float   llr[3];
        float   evm;
        assert(rtnc_psk_demap_soft(
            &psk,
            filtered[best_start + symbol_index * profile.samples_per_symbol],
            &recovered,
            llr,
            &evm
        ));
        assert(recovered == training[symbol_index]);
    }

    rtnc_rrc_deinit(&rrc);
    rtnc_psk_deinit(&psk);
    return 0;
}
