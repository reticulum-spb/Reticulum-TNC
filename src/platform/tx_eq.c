#include "rtnc/tx_eq.h"

#include <math.h>
#include <stddef.h>

enum { COEFFICIENTS = RTNC_TX_EQ_TAP_COUNT / 2U + 1U };

static bool solve(double matrix[COEFFICIENTS][COEFFICIENTS + 1U], double solution[COEFFICIENTS]) {
    size_t column;
    for (column = 0U; column < COEFFICIENTS; ++column) {
        size_t pivot = column;
        size_t row;
        for (row = column + 1U; row < COEFFICIENTS; ++row) {
            if (fabs(matrix[row][column]) > fabs(matrix[pivot][column])) {
                pivot = row;
            }
        }
        if (fabs(matrix[pivot][column]) < 1.0e-10) {
            return false;
        }
        if (pivot != column) {
            size_t item;
            for (item = column; item <= COEFFICIENTS; ++item) {
                const double temporary = matrix[column][item];
                matrix[column][item] = matrix[pivot][item];
                matrix[pivot][item] = temporary;
            }
        }
        {
            const double divisor = matrix[column][column];
            size_t       item;
            for (item = column; item <= COEFFICIENTS; ++item) {
                matrix[column][item] /= divisor;
            }
        }
        for (row = 0U; row < COEFFICIENTS; ++row) {
            if (row != column) {
                const double factor = matrix[row][column];
                size_t       item;
                for (item = column; item <= COEFFICIENTS; ++item) {
                    matrix[row][item] -= factor * matrix[column][item];
                }
            }
        }
    }
    for (column = 0U; column < COEFFICIENTS; ++column) {
        solution[column] = matrix[column][COEFFICIENTS];
    }
    return true;
}

static double interpolate(const unsigned int *frequencies_hz, const double *amplitudes, size_t count, double frequency_hz) {
    size_t point;
    if (frequency_hz <= (double) frequencies_hz[0]) {
        return amplitudes[0];
    }
    for (point = 1U; point < count; ++point) {
        if (frequency_hz <= (double) frequencies_hz[point]) {
            const double fraction =
                (frequency_hz - (double) frequencies_hz[point - 1U]) /
                (double) (frequencies_hz[point] - frequencies_hz[point - 1U]);
            return amplitudes[point - 1U] +
                   fraction * (amplitudes[point] - amplitudes[point - 1U]);
        }
    }
    return amplitudes[count - 1U];
}

bool rtnc_tx_eq_design(const unsigned int *frequencies_hz, const double *amplitudes, size_t point_count, float carrier_hz, float low_hz, float high_hz, float taps[RTNC_TX_EQ_TAP_COUNT]) {
    const double pi = 3.14159265358979323846;
    const double regularization = 0.03;
    double       normal[COEFFICIENTS][COEFFICIENTS + 1U] = { { 0.0 } };
    double       coefficients[COEFFICIENTS];
    double       reference;
    size_t       used_points = 0U;
    size_t       point;
    size_t       row;
    if (frequencies_hz == NULL || amplitudes == NULL || taps == NULL ||
        point_count < 3U || carrier_hz <= 0.0F || low_hz < 0.0F ||
        high_hz <= low_hz) {
        return false;
    }
    for (point = 0U; point < point_count; ++point) {
        if ((float) frequencies_hz[point] < low_hz ||
            (float) frequencies_hz[point] > high_hz) {
            continue;
        }
        if (amplitudes[point] <= 0.0 ||
            (point > 0U && frequencies_hz[point] <= frequencies_hz[point - 1U])) {
            return false;
        }
        ++used_points;
    }
    if (used_points < 3U) {
        return false;
    }
    reference = interpolate(frequencies_hz, amplitudes, point_count, (double) carrier_hz);
    for (point = 0U; point < point_count; ++point) {
        double       basis[COEFFICIENTS];
        double       desired = reference / amplitudes[point];
        size_t       column;
        const double omega = 2.0 * pi * (double) frequencies_hz[point] / 48000.0;
        if ((float) frequencies_hz[point] < low_hz ||
            (float) frequencies_hz[point] > high_hz) {
            continue;
        }
        if (desired > 2.5) {
            desired = 2.5;
        }
        basis[0] = 1.0;
        for (column = 1U; column < COEFFICIENTS; ++column) {
            basis[column] = 2.0 * cos(omega * (double) column);
        }
        for (row = 0U; row < COEFFICIENTS; ++row) {
            for (column = 0U; column < COEFFICIENTS; ++column) {
                normal[row][column] += basis[row] * basis[column];
            }
            normal[row][COEFFICIENTS] += basis[row] * desired;
        }
    }
    for (row = 0U; row < COEFFICIENTS; ++row) {
        normal[row][row] += regularization;
        normal[row][COEFFICIENTS] += row == 0U ? regularization : 0.0;
    }
    if (!solve(normal, coefficients)) {
        return false;
    }
    for (row = 0U; row < RTNC_TX_EQ_TAP_COUNT; ++row) {
        const size_t distance =
            row > RTNC_TX_EQ_TAP_COUNT / 2U
                ? row - RTNC_TX_EQ_TAP_COUNT / 2U
                : RTNC_TX_EQ_TAP_COUNT / 2U - row;
        taps[row] = (float) coefficients[distance];
    }
    {
        const double carrier_gain =
            rtnc_tx_eq_zero_phase_gain(taps, (double) carrier_hz);
        if (!isfinite(carrier_gain) || carrier_gain < 0.1) {
            return false;
        }
        for (row = 0U; row < RTNC_TX_EQ_TAP_COUNT; ++row) {
            taps[row] = (float) ((double) taps[row] / carrier_gain);
        }
    }
    /* Blend an aggressive inverse with the identity filter until it is safe
     * for both the fixed-point headroom policy and the whole occupied band.
     * Both filters have unit gain at the carrier, so blending preserves the
     * normalization and symmetry. */
    {
        float  designed[RTNC_TX_EQ_TAP_COUNT];
        double blend = 1.0;
        bool   acceptable = false;
        for (row = 0U; row < RTNC_TX_EQ_TAP_COUNT; ++row) {
            designed[row] = taps[row];
        }
        while (blend >= 0.0 && !acceptable) {
            double absolute_sum = 0.0;
            double frequency;
            for (row = 0U; row < RTNC_TX_EQ_TAP_COUNT; ++row) {
                const double identity =
                    row == RTNC_TX_EQ_TAP_COUNT / 2U ? 1.0 : 0.0;
                taps[row] =
                    (float) (identity + blend * ((double) designed[row] - identity));
                absolute_sum += fabs((double) taps[row]);
            }
            acceptable = absolute_sum <= 7.9;
            for (frequency = (double) low_hz;
                 acceptable && frequency <= (double) high_hz;
                 frequency += 100.0) {
                acceptable =
                    rtnc_tx_eq_zero_phase_gain(taps, frequency) > 0.05;
            }
            blend -= 0.05;
        }
        if (!acceptable) {
            return false;
        }
    }
    return true;
}

double rtnc_tx_eq_zero_phase_gain(
    const float taps[RTNC_TX_EQ_TAP_COUNT],
    double      frequency_hz
) {
    const double pi = 3.14159265358979323846;
    const double omega = 2.0 * pi * frequency_hz / 48000.0;
    const double center = (double) (RTNC_TX_EQ_TAP_COUNT / 2U);
    double       gain = 0.0;
    size_t       tap;
    if (taps == NULL || !isfinite(frequency_hz)) {
        return 0.0;
    }
    for (tap = 0U; tap < RTNC_TX_EQ_TAP_COUNT; ++tap) {
        gain += (double) taps[tap] *
                cos(omega * ((double) tap - center));
    }
    return gain;
}

float rtnc_tx_eq_apply_sample(const float taps[RTNC_TX_EQ_TAP_COUNT], const int16_t *samples, size_t count, size_t index) {
    const size_t center = RTNC_TX_EQ_TAP_COUNT / 2U;
    float        output = 0.0F;
    size_t       tap;
    if (taps == NULL || samples == NULL || index >= count) {
        return 0.0F;
    }
    for (tap = 0U; tap < RTNC_TX_EQ_TAP_COUNT; ++tap) {
        const size_t leading = center > tap ? center - tap : 0U;
        const size_t trailing = tap > center ? tap - center : 0U;
        if (index >= trailing && index + leading < count) {
            output += taps[tap] *
                      (float) samples[index + leading - trailing];
        }
    }
    return output;
}

double rtnc_tx_eq_magnitude(const float taps[RTNC_TX_EQ_TAP_COUNT], double frequency_hz) {
    const double pi = 3.14159265358979323846;
    const double omega = 2.0 * pi * frequency_hz / 48000.0;
    double       real_part = 0.0;
    double       imaginary_part = 0.0;
    size_t       tap;
    if (taps == NULL || !isfinite(frequency_hz)) {
        return 0.0;
    }
    for (tap = 0U; tap < RTNC_TX_EQ_TAP_COUNT; ++tap) {
        real_part += (double) taps[tap] * cos(omega * (double) tap);
        imaginary_part -= (double) taps[tap] * sin(omega * (double) tap);
    }
    return hypot(real_part, imaginary_part);
}
