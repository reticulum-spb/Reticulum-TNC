#include "rtnc/tx_eq.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>

int main(void) {
    static const unsigned int frequencies[] = {
        600U,
        900U,
        1200U,
        1500U,
        1800U,
        2100U,
        2400U,
        2700U,
    };
    static const double measured[] = {
        294.3,
        741.7,
        1157.2,
        1067.2,
        911.3,
        753.9,
        601.5,
        368.9,
    };
    float   taps[RTNC_TX_EQ_TAP_COUNT];
    double  raw_min = 1.0e100;
    double  raw_max = 0.0;
    double  corrected_min = 1.0e100;
    double  corrected_max = 0.0;
    double  tap_absolute_sum = 0.0;
    size_t  point;
    int16_t impulse[65] = { 0 };
    assert(rtnc_tx_eq_design(frequencies, measured, sizeof(frequencies) / sizeof(frequencies[0]), 1650.0F, 900.0F, 2400.0F, taps));
    for (point = 0U; point < RTNC_TX_EQ_TAP_COUNT / 2U; ++point) {
        assert(fabsf(taps[point] - taps[RTNC_TX_EQ_TAP_COUNT - 1U - point]) < 1.0e-6F);
    }
    for (point = 0U; point < RTNC_TX_EQ_TAP_COUNT; ++point) {
        tap_absolute_sum += fabs((double) taps[point]);
    }
    assert(fabs(rtnc_tx_eq_magnitude(taps, 1650.0) - 1.0) < 1.0e-5);
    for (point = 1U; point <= 6U; ++point) {
        const double corrected =
            measured[point] *
            rtnc_tx_eq_magnitude(taps, (double) frequencies[point]);
        raw_min = fmin(raw_min, measured[point]);
        raw_max = fmax(raw_max, measured[point]);
        corrected_min = fmin(corrected_min, corrected);
        corrected_max = fmax(corrected_max, corrected);
    }
    for (point = 900U; point <= 2400U; point += 25U) {
        const double zero_phase =
            rtnc_tx_eq_zero_phase_gain(taps, (double) point);
        assert(zero_phase > 0.05);
        assert(fabs(zero_phase - rtnc_tx_eq_magnitude(taps, (double) point)) < 1.0e-6);
    }
    impulse[32] = 1000;
    for (point = 0U; point < RTNC_TX_EQ_TAP_COUNT; ++point) {
        const size_t output_index =
            32U + point - RTNC_TX_EQ_TAP_COUNT / 2U;
        const float expected = 1000.0F * taps[point];
        assert(fabsf(rtnc_tx_eq_apply_sample(taps, impulse, sizeof(impulse) / sizeof(impulse[0]), output_index) - expected) < 1.0e-3F);
    }
    (void) printf("raw_ripple_db=%.3f corrected_ripple_db=%.3f tap_l1=%.3f\n", 20.0 * log10(raw_max / raw_min), 20.0 * log10(corrected_max / corrected_min), tap_absolute_sum);
    assert(20.0 * log10(corrected_max / corrected_min) < 20.0 * log10(raw_max / raw_min));
    assert(20.0 * log10(corrected_max / corrected_min) < 4.0);
    assert(tap_absolute_sum < 8.0);
    return 0;
}
