#include "rtnc/equalizer.h"

#include <assert.h>
#include <complex.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>

enum { TRAINING_SYMBOLS = 512U,
       TEST_SYMBOLS = 256U,
       TAP_COUNT = 17U };

static float complex symbol_at(size_t index) {
    const unsigned int value =
        (unsigned int) ((index * 29U + index / 7U + 1U) & 3U);
    const float real = (value & 1U) != 0U ? -1.0F : 1.0F;
    const float imag = (value & 2U) != 0U ? -1.0F : 1.0F;
    return (real + I * imag) * (float) M_SQRT1_2;
}

static void channel(size_t index, float complex output[2]) {
    const float complex current = symbol_at(index);
    const float complex previous = index == 0U ? 0.0F : symbol_at(index - 1U);
    const float complex previous_two =
        index < 2U ? 0.0F : symbol_at(index - 2U);
    output[0] = 0.70F * current + 0.62F * previous + 0.18F * previous_two;
    output[1] = 0.52F * current + 0.78F * previous - 0.12F * previous_two;
}

int main(void) {
    rtnc_equalizer_t equalizer = { 0 };
    float complex    taps[TAP_COUNT];
    float            training_error = 0.0F;
    float            test_error = 0.0F;
    size_t           index;

    assert(rtnc_equalizer_init(&equalizer, TAP_COUNT, 0.99F));
    rtnc_equalizer_reset(&equalizer);
    for (index = 0U; index < TRAINING_SYMBOLS; ++index) {
        float complex samples[2];
        float complex output;
        float         error;
        channel(index, samples);
        assert(rtnc_equalizer_execute(&equalizer, samples, true, symbol_at(index), &output, &error));
        if (index >= TRAINING_SYMBOLS - 64U) {
            training_error += error;
        }
    }
    for (index = TRAINING_SYMBOLS;
         index < TRAINING_SYMBOLS + TEST_SYMBOLS;
         ++index) {
        float complex samples[2];
        float complex output;
        float         error;
        channel(index, samples);
        assert(rtnc_equalizer_execute(&equalizer, samples, false, symbol_at(index), &output, &error));
        test_error += error;
    }
    training_error /= 64.0F;
    test_error /= (float) TEST_SYMBOLS;
    assert(rtnc_equalizer_copy_taps(&equalizer, taps, TAP_COUNT));
    (void) printf("training_error=%.6f test_error=%.6f center_tap=%.6f%+.6fi\n", (double) training_error, (double) test_error, (double) crealf(taps[TAP_COUNT / 2U]), (double) cimagf(taps[TAP_COUNT / 2U]));
    assert(training_error < 0.12F);
    assert(test_error < 0.12F);
    rtnc_equalizer_deinit(&equalizer);
    return 0;
}
