#include "rtnc/phy.h"
#include "rtnc/psk.h"
#include "rtnc/rrc.h"
#include "rtnc/timing.h"

#include <assert.h>
#include <complex.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum {
    DATA_SYMBOLS = 160U,
    GUARD_SYMBOLS = 64U,
    UNKNOWN_OFFSET = 17U,
    MAX_INPUT = 13000U,
    MAX_OUTPUT = 320U,
};

static size_t resample(const float complex *input, size_t input_count, float complex *output, float ppm) {
    const double ratio = 1.0 + (double) ppm * 1.0e-6;
    double       position = 0.0;
    size_t       count = 0U;
    while (position + 1.0 < (double) input_count && count < MAX_INPUT) {
        const size_t lower = (size_t) position;
        const float  fraction = (float) (position - (double) lower);
        output[count] = input[lower] * (1.0F - fraction) +
                        input[lower + 1U] * fraction;
        ++count;
        position += ratio;
    }
    return count;
}

static void run_case(float ppm) {
    const rtnc_phy_profile_t profile = rtnc_phy_profile_qpsk_1200();
    float complex            reference[DATA_SYMBOLS];
    float complex            transmitted[MAX_INPUT] = { 0.0F };
    float complex            received[MAX_INPUT];
    float complex            recovered[MAX_OUTPUT];
    rtnc_psk_t               psk = { 0 };
    rtnc_rrc_t               rrc = { 0 };
    rtnc_timing_t            timing = { 0 };
    size_t                   write_index = UNKNOWN_OFFSET;
    size_t                   received_count;
    size_t                   recovered_count = 0U;
    size_t                   symbol;
    size_t                   sample;
    size_t                   best_start = 0U;
    float                    best_score = 0.0F;

    assert(rtnc_psk_init(&psk, RTNC_MODULATION_QPSK));
    assert(rtnc_rrc_init(&rrc, profile.samples_per_symbol, 7U, profile.rrc_rolloff));
    assert(rtnc_timing_init(&timing, profile.samples_per_symbol, 7U, profile.rrc_rolloff, 0.03F));
    for (symbol = 0U; symbol < GUARD_SYMBOLS + DATA_SYMBOLS + GUARD_SYMBOLS;
         ++symbol) {
        float complex mapped;
        uint32_t      state = (uint32_t) symbol + 1U;
        state ^= state << 13U;
        state ^= state >> 17U;
        state ^= state << 5U;
        assert(rtnc_psk_map(&psk, (uint8_t) (state & 3U), &mapped));
        if (symbol >= GUARD_SYMBOLS &&
            symbol < GUARD_SYMBOLS + DATA_SYMBOLS) {
            const size_t data_index = symbol - GUARD_SYMBOLS;
            reference[data_index] = mapped;
        }
        assert(rtnc_rrc_interpolate(&rrc, mapped, &transmitted[write_index]));
        write_index += profile.samples_per_symbol;
    }
    received_count = resample(transmitted, write_index, received, ppm);
    for (sample = 0U; sample < received_count; ++sample) {
        float complex output[4];
        size_t        produced = 0U;
        assert(rtnc_timing_execute(&timing, received[sample], output, 4U, &produced));
        assert(recovered_count + produced <= MAX_OUTPUT);
        for (symbol = 0U; symbol < produced; ++symbol) {
            recovered[recovered_count++] = output[symbol];
        }
    }
    for (sample = 0U; sample + DATA_SYMBOLS <= recovered_count; ++sample) {
        float complex correlation = 0.0F;
        float         energy = 0.0F;
        for (symbol = 0U; symbol < DATA_SYMBOLS; ++symbol) {
            correlation += conjf(reference[symbol]) * recovered[sample + symbol];
            energy += crealf(recovered[sample + symbol] * conjf(recovered[sample + symbol]));
        }
        if (energy > 0.0F) {
            const float score = cabsf(correlation) /
                                sqrtf((float) DATA_SYMBOLS * energy);
            if (score > best_score) {
                best_score = score;
                best_start = sample;
            }
        }
    }
    (void) printf("ppm=%.0f score=%.6f tau=%.6f symbols=%zu start=%zu\n", (double) ppm, (double) best_score, (double) rtnc_timing_offset(&timing), recovered_count, best_start);
    assert(best_score > 0.97F);
    rtnc_timing_deinit(&timing);
    rtnc_rrc_deinit(&rrc);
    rtnc_psk_deinit(&psk);
}

int main(void) {
    run_case(0.0F);
    run_case(-1000.0F);
    run_case(1000.0F);
    return 0;
}
