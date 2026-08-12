#include "rtnc/carrier.h"
#include "rtnc/phy.h"
#include "rtnc/psk.h"
#include "rtnc/rrc.h"

#include <assert.h>
#include <complex.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum {
    TRAINING_SYMBOLS = 32,
    FILTER_DELAY_SYMBOLS = 7,
    TRAILING_SYMBOLS = 16,
    UNKNOWN_SAMPLE_OFFSET = 31,
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
    float complex  reference[TRAINING_SYMBOLS];
    float          audio[MAX_SAMPLES] = { 0.0F };
    float complex  filtered[MAX_SAMPLES] = { 0.0F };
    float complex  shaped[40];
    rtnc_psk_t     psk = { 0 };
    rtnc_rrc_t     rrc = { 0 };
    rtnc_carrier_t carrier = { 0 };
    size_t         write_index = UNKNOWN_SAMPLE_OFFSET;
    size_t         sample_count;
    size_t         symbol_index;
    size_t         sample_index;
    size_t         best_start = 0U;
    float          best_score = 0.0F;

    assert(profile.samples_per_symbol == 40U);
    assert(rtnc_psk_init(&psk, RTNC_MODULATION_QPSK));
    assert(rtnc_rrc_init(&rrc, profile.samples_per_symbol, FILTER_DELAY_SYMBOLS, profile.rrc_rolloff));
    assert(rtnc_carrier_init(&carrier, profile.sample_rate_hz, profile.carrier_hz));

    /* Advance both oscillators through the unknown leading silence. */
    for (sample_index = 0U; sample_index < UNKNOWN_SAMPLE_OFFSET;
         ++sample_index) {
        float complex ignored;
        assert(rtnc_carrier_upconvert(&carrier, 0.0F, &audio[sample_index]));
        assert(rtnc_carrier_downconvert(&carrier, audio[sample_index], &ignored));
    }
    for (symbol_index = 0U;
         symbol_index < TRAINING_SYMBOLS + TRAILING_SYMBOLS;
         ++symbol_index) {
        const float complex symbol = symbol_index < TRAINING_SYMBOLS
                                         ? (assert(rtnc_psk_map(
                                                &psk,
                                                training[symbol_index],
                                                &reference[symbol_index]
                                            )),
                                            reference[symbol_index])
                                         : 0.0F;
        assert(rtnc_rrc_interpolate(&rrc, symbol, shaped));
        for (sample_index = 0U; sample_index < profile.samples_per_symbol;
             ++sample_index) {
            assert(rtnc_carrier_upconvert(&carrier, shaped[sample_index], &audio[write_index]));
            ++write_index;
        }
    }
    sample_count = write_index;

    rtnc_carrier_reset(&carrier);
    for (sample_index = 0U; sample_index < sample_count; ++sample_index) {
        float complex baseband;
        assert(rtnc_carrier_downconvert(&carrier, audio[sample_index], &baseband));
        assert(rtnc_rrc_match(&rrc, baseband, &filtered[sample_index]));
    }

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
    if (best_score <= 0.995F) {
        (void) fprintf(stderr, "audio acquisition score=%f start=%zu\n", (double) best_score, best_start);
    }
    assert(best_score > 0.995F);
    assert(best_start == UNKNOWN_SAMPLE_OFFSET + 2U * FILTER_DELAY_SYMBOLS * profile.samples_per_symbol);
    for (symbol_index = 0U; symbol_index < TRAINING_SYMBOLS; ++symbol_index) {
        uint8_t recovered;
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

    rtnc_carrier_deinit(&carrier);
    rtnc_rrc_deinit(&rrc);
    rtnc_psk_deinit(&psk);
    return 0;
}
