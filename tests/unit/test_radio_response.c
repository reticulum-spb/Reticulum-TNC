#include "rtnc/radio_response.h"

#include <assert.h>
#include <complex.h>
#include <math.h>
#include <stddef.h>

enum { SAMPLE_RATE = 48000U,
       WINDOW_SAMPLES = 38400U };

static double initial_phase(size_t tone, size_t count) {
    const double pi = 3.14159265358979323846;
    return pi * (double) tone * (double) tone / (double) count;
}

static double complex coefficient(
    const double *samples,
    size_t        count,
    unsigned int  frequency
) {
    const double   pi = 3.14159265358979323846;
    double complex value = 0.0;
    size_t         index;
    for (index = 0U; index < count; ++index) {
        value += samples[index] *
                 cexp(-I * 2.0 * pi * (double) frequency * (double) index / (double) SAMPLE_RATE);
    }
    return 2.0 * value / (double) count;
}

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
    const double                  pi = 3.14159265358979323846;
    const double                  delay_seconds = 0.0008;
    double complex                response[sizeof(frequencies) / sizeof(frequencies[0])];
    double                        phase[sizeof(frequencies) / sizeof(frequencies[0])];
    rtnc_radio_response_metrics_t metrics;
    size_t                        point;

    for (point = 0U; point < sizeof(frequencies) / sizeof(frequencies[0]); ++point) {
        const double amplitude = pow(10.0, (double) point * 0.5 / 20.0);
        response[point] = amplitude *
                          cexp(-I * 2.0 * pi * (double) frequencies[point] * delay_seconds);
    }
    assert(rtnc_radio_response_analyze(
        frequencies,
        response,
        sizeof(frequencies) / sizeof(frequencies[0]),
        900.0,
        2400.0,
        &metrics,
        phase
    ));
    assert(metrics.points_used == 6U);
    assert(fabs(metrics.relative_delay_ms - 0.8) < 1.0e-6);
    assert(metrics.group_delay_ripple_ms < 1.0e-6);
    assert(metrics.residual_phase_rms_degrees < 1.0e-6);
    assert(fabs(metrics.amplitude_ripple_db - 2.5) < 1.0e-6);

    for (point = 0U; point < sizeof(frequencies) / sizeof(frequencies[0]); ++point) {
        const double ripple = point == 3U ? 0.25 : 0.0;
        response[point] = cexp(I * (-2.0 * pi * (double) frequencies[point] * delay_seconds + ripple));
    }
    assert(rtnc_radio_response_analyze(
        frequencies,
        response,
        sizeof(frequencies) / sizeof(frequencies[0]),
        900.0,
        2400.0,
        &metrics,
        phase
    ));
    assert(metrics.group_delay_ripple_ms > 0.25);
    assert(metrics.residual_phase_rms_degrees > 4.0);
    metrics.residual_phase_rms_degrees = 7.5;
    metrics.group_delay_ripple_ms = 0.31;
    assert(rtnc_radio_response_suitability(&metrics, 1U, 750U) == RTNC_RADIO_SUITABILITY_GOOD);
    assert(rtnc_radio_response_suitability(&metrics, 1U, 1200U) == RTNC_RADIO_SUITABILITY_GOOD);
    assert(rtnc_radio_response_suitability(&metrics, 2U, 1200U) == RTNC_RADIO_SUITABILITY_GOOD);
    assert(rtnc_radio_response_suitability(&metrics, 3U, 1000U) == RTNC_RADIO_SUITABILITY_GOOD);
    assert(rtnc_radio_response_suitability(&metrics, 4U, 1000U) == RTNC_RADIO_SUITABILITY_POOR);

    {
        static double samples[WINDOW_SAMPLES];
        const size_t  skip_samples = 9600U;
        const size_t  delay_samples = 37U;
        size_t        index;
        for (index = 0U; index < WINDOW_SAMPLES; ++index) {
            for (point = 0U;
                 point < sizeof(frequencies) / sizeof(frequencies[0]);
                 ++point) {
                const double source_index =
                    (double) (index + skip_samples) - (double) delay_samples;
                samples[index] += sin(
                    2.0 * pi * (double) frequencies[point] * source_index /
                        (double) SAMPLE_RATE +
                    initial_phase(
                        point,
                        sizeof(frequencies) / sizeof(frequencies[0])
                    )
                );
            }
        }
        for (point = 0U;
             point < sizeof(frequencies) / sizeof(frequencies[0]);
             ++point) {
            const double expected_phase =
                initial_phase(
                    point,
                    sizeof(frequencies) / sizeof(frequencies[0])
                ) +
                2.0 * pi * (double) frequencies[point] *
                    (double) skip_samples / (double) SAMPLE_RATE -
                0.5 * pi;
            response[point] =
                coefficient(samples, WINDOW_SAMPLES, frequencies[point]) *
                cexp(-I * expected_phase);
        }
        assert(rtnc_radio_response_analyze(
            frequencies,
            response,
            sizeof(frequencies) / sizeof(frequencies[0]),
            900.0,
            2400.0,
            &metrics,
            phase
        ));
        assert(fabs(metrics.relative_delay_ms - 1000.0 * (double) delay_samples / (double) SAMPLE_RATE) < 1.0e-6);
        assert(metrics.amplitude_ripple_db < 1.0e-6);
        assert(metrics.group_delay_ripple_ms < 1.0e-6);
        assert(metrics.residual_phase_rms_degrees < 1.0e-6);
    }
    return 0;
}
