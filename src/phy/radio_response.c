#include "rtnc/radio_response.h"

#include <float.h>
#include <math.h>

bool rtnc_radio_response_analyze(
    const unsigned int            *frequencies_hz,
    const double complex          *response,
    size_t                         point_count,
    double                         low_hz,
    double                         high_hz,
    rtnc_radio_response_metrics_t *metrics,
    double                        *unwrapped_phase_radians
) {
    const double pi = 3.14159265358979323846;
    double       minimum_amplitude = DBL_MAX;
    double       maximum_amplitude = 0.0;
    double       sum_frequency = 0.0;
    double       sum_phase = 0.0;
    double       sum_frequency_square = 0.0;
    double       sum_frequency_phase = 0.0;
    double       previous_phase = 0.0;
    double       previous_increment = 0.0;
    double       slope;
    double       intercept;
    double       residual_square_sum = 0.0;
    double       minimum_delay = DBL_MAX;
    double       maximum_delay = -DBL_MAX;
    size_t       first = point_count;
    size_t       last = point_count;
    size_t       used = 0U;
    size_t       point;

    if (frequencies_hz == NULL || response == NULL || metrics == NULL ||
        unwrapped_phase_radians == NULL || point_count < 3U || low_hz < 0.0 ||
        high_hz <= low_hz) {
        return false;
    }
    for (point = 0U; point < point_count; ++point) {
        double       phase;
        const double amplitude = cabs(response[point]);
        if ((point > 0U && frequencies_hz[point] <= frequencies_hz[point - 1U]) ||
            !isfinite(amplitude) || amplitude <= 0.0) {
            return false;
        }
        phase = carg(response[point]);
        if (point == 1U) {
            while (phase - previous_phase > pi) phase -= 2.0 * pi;
            while (phase - previous_phase < -pi) phase += 2.0 * pi;
            previous_increment = phase - previous_phase;
        } else if (point > 1U) {
            const double predicted = previous_phase + previous_increment;
            while (phase - predicted > pi) phase -= 2.0 * pi;
            while (phase - predicted < -pi) phase += 2.0 * pi;
            previous_increment = phase - previous_phase;
        }
        previous_phase = phase;
        unwrapped_phase_radians[point] = phase;
        if ((double) frequencies_hz[point] < low_hz ||
            (double) frequencies_hz[point] > high_hz) {
            continue;
        }
        if (first == point_count) {
            first = point;
        }
        last = point;
        minimum_amplitude = fmin(minimum_amplitude, amplitude);
        maximum_amplitude = fmax(maximum_amplitude, amplitude);
        sum_frequency += (double) frequencies_hz[point];
        sum_phase += phase;
        sum_frequency_square += (double) frequencies_hz[point] *
                                (double) frequencies_hz[point];
        sum_frequency_phase += (double) frequencies_hz[point] * phase;
        ++used;
    }
    if (used < 3U || first == point_count || last <= first) {
        return false;
    }
    {
        const double denominator = (double) used * sum_frequency_square -
                                   sum_frequency * sum_frequency;
        if (fabs(denominator) < 1.0e-12) {
            return false;
        }
        slope = ((double) used * sum_frequency_phase -
                 sum_frequency * sum_phase) /
                denominator;
        intercept = (sum_phase - slope * sum_frequency) / (double) used;
    }
    for (point = first; point <= last; ++point) {
        const double frequency = (double) frequencies_hz[point];
        const double phase = unwrapped_phase_radians[point];
        const double residual = phase - (intercept + slope * frequency);
        residual_square_sum += residual * residual;
        if (point > first) {
            const double previous = unwrapped_phase_radians[point - 1U];
            const double delay = -(phase - previous) /
                                 (2.0 * pi *
                                  (double) (frequencies_hz[point] -
                                            frequencies_hz[point - 1U]));
            minimum_delay = fmin(minimum_delay, delay);
            maximum_delay = fmax(maximum_delay, delay);
        }
    }
    metrics->amplitude_ripple_db =
        20.0 * log10(maximum_amplitude / minimum_amplitude);
    metrics->relative_delay_ms = -1000.0 * slope / (2.0 * pi);
    metrics->group_delay_ripple_ms = 1000.0 * (maximum_delay - minimum_delay);
    metrics->residual_phase_rms_degrees =
        sqrt(residual_square_sum / (double) used) * 180.0 / pi;
    metrics->points_used = used;
    return isfinite(metrics->amplitude_ripple_db) &&
           isfinite(metrics->relative_delay_ms) &&
           isfinite(metrics->group_delay_ripple_ms) &&
           isfinite(metrics->residual_phase_rms_degrees);
}

rtnc_radio_suitability_t rtnc_radio_response_suitability(
    const rtnc_radio_response_metrics_t *metrics,
    unsigned int                         bits_per_symbol,
    unsigned int                         symbol_rate_baud
) {
    double phase_good;
    double delay_good;
    if (metrics == NULL || symbol_rate_baud == 0U) {
        return RTNC_RADIO_SUITABILITY_POOR;
    }
    if (bits_per_symbol == 1U) {
        phase_good = 30.0;
        delay_good = 500.0 / (double) symbol_rate_baud;
    } else if (bits_per_symbol == 2U) {
        phase_good = 15.0;
        delay_good = 600.0 / (double) symbol_rate_baud;
    } else if (bits_per_symbol == 3U) {
        phase_good = 8.0;
        delay_good = 350.0 / (double) symbol_rate_baud;
    } else if (bits_per_symbol == 4U) {
        phase_good = 3.0;
        delay_good = 150.0 / (double) symbol_rate_baud;
    } else {
        return RTNC_RADIO_SUITABILITY_POOR;
    }
    if (metrics->residual_phase_rms_degrees <= phase_good &&
        metrics->group_delay_ripple_ms <= delay_good) {
        return RTNC_RADIO_SUITABILITY_GOOD;
    }
    if (metrics->residual_phase_rms_degrees <= 2.0 * phase_good &&
        metrics->group_delay_ripple_ms <= 2.0 * delay_good) {
        return RTNC_RADIO_SUITABILITY_MARGINAL;
    }
    return RTNC_RADIO_SUITABILITY_POOR;
}
