#include "rtnc/modem.h"

#include <assert.h>
#include <complex.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { SAMPLE_RATE = 48000U,
       POINTS = 8U,
       CHANNEL_TAPS = 512U,
       TRIALS = 20U,
       PAYLOAD_BYTES = 64U };

static const unsigned int frequencies[POINTS] = { 600U, 900U, 1200U, 1500U, 1800U, 2100U, 2400U, 2700U };
static const double       magnitudes[POINTS] = { 4079.4, 3447.5, 4402.3, 3538.7, 2453.5, 1901.1, 2316.4, 2598.5 };
static const double       phases_deg[POINTS] = { 12.05, -106.58, -180.91, -238.97, -289.25, -358.43, -398.65, -438.38 };

static double interpolate(const double values[POINTS], double frequency) {
    size_t point;
    if (frequency <= (double) frequencies[0])
        return values[0];
    for (point = 1U; point < POINTS; ++point) {
        if (frequency <= (double) frequencies[point]) {
            const double fraction = (frequency - (double) frequencies[point - 1U]) / (double) (frequencies[point] - frequencies[point - 1U]);
            return values[point - 1U] + fraction * (values[point] - values[point - 1U]);
        }
    }
    return values[POINTS - 1U];
}

static void build_channel(float taps[CHANNEL_TAPS]) {
    const double pi = 3.14159265358979323846;
    double       residual[POINTS];
    double       sf = 0.0, sp = 0.0, sff = 0.0, sfp = 0.0;
    double       slope, intercept;
    size_t       point, tap;
    for (point = 1U; point <= 6U; ++point) {
        const double phase = phases_deg[point] * pi / 180.0;
        sf += (double) frequencies[point];
        sp += phase;
        sff += (double) frequencies[point] * (double) frequencies[point];
        sfp += (double) frequencies[point] * phase;
    }
    slope = (6.0 * sfp - sf * sp) / (6.0 * sff - sf * sf);
    intercept = (sp - slope * sf) / 6.0;
    for (point = 0U; point < POINTS; ++point)
        residual[point] = phases_deg[point] * pi / 180.0 - intercept - slope * (double) frequencies[point];
    for (tap = 0U; tap < CHANNEL_TAPS; ++tap) {
        double value = 0.0;
        size_t bin;
        for (bin = 1U; bin < CHANNEL_TAPS / 2U; ++bin) {
            const double frequency = (double) bin * SAMPLE_RATE / CHANNEL_TAPS;
            double       edge = 1.0, magnitude, phase;
            if (frequency < 300.0 || frequency > 3000.0)
                continue;
            if (frequency < 600.0)
                edge = (frequency - 300.0) / 300.0;
            else if (frequency > 2700.0)
                edge = (3000.0 - frequency) / 300.0;
            magnitude = edge * interpolate(magnitudes, frequency) / magnitudes[3U];
            phase = interpolate(residual, frequency) - 2.0 * pi * frequency * 64.0 / SAMPLE_RATE;
            value += 2.0 * magnitude * cos(phase + 2.0 * pi * (double) bin * (double) tap / CHANNEL_TAPS);
        }
        taps[tap] = (float) (value / CHANNEL_TAPS);
    }
}

static size_t apply_channel(const float *input, size_t count, const float taps[CHANNEL_TAPS], float *output, size_t capacity) {
    const size_t output_count = count + CHANNEL_TAPS - 1U;
    float        peak = 0.0F;
    size_t       index;
    assert(output_count <= capacity);
    (void) memset(output, 0, output_count * sizeof(output[0]));
    for (index = 0U; index < count; ++index) {
        size_t tap;
        for (tap = 0U; tap < CHANNEL_TAPS; ++tap)
            output[index + tap] += input[index] * taps[tap];
    }
    for (index = 0U; index < output_count; ++index)
        peak = fmaxf(peak, fabsf(output[index]));
    assert(peak > 0.0F);
    for (index = 0U; index < output_count; ++index)
        output[index] *= 0.65F / peak;
    return output_count;
}

int main(void) {
    static float                  clean[RTNC_MODEM_MAX_AUDIO_SAMPLES], impaired[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    static rtnc_modem_workspace_t fast_workspace, full_workspace;
    float                         taps[CHANNEL_TAPS];
    rtnc_phy_profile_t            profile;
    rtnc_modem_t                  modem;
    unsigned int                  fast_successes = 0U, full_successes = 0U, equalizer_successes = 0U, trial;
    double                        fast_evm = 0.0, full_evm = 0.0;
    build_channel(taps);
    assert(rtnc_phy_profile_psk(RTNC_MODULATION_16PSK, 1000U, 1650.0F, &profile));
    assert(rtnc_modem_init_profile(&modem, FEC_LDPC_ROBUST, PAYLOAD_BYTES, &profile));
    for (trial = 0U; trial < TRIALS; ++trial) {
        uint8_t             payload[PAYLOAD_BYTES], decoded[PAYLOAD_BYTES];
        size_t              count = 0U, impaired_count, decoded_length = 0U, index;
        rtnc_sync_metrics_t fast_metrics = { 0 }, full_metrics = { 0 };
        rtnc_modem_status_t status;
        for (index = 0U; index < PAYLOAD_BYTES; ++index)
            payload[index] = (uint8_t) (index * 37U + trial * 19U + 0x29U);
        assert(rtnc_modem_tx_audio(&modem, payload, PAYLOAD_BYTES, clean, RTNC_MODEM_MAX_AUDIO_SAMPLES, &count) == RTNC_MODEM_OK);
        impaired_count = apply_channel(clean, count, taps, impaired, RTNC_MODEM_MAX_AUDIO_SAMPLES);
        status = rtnc_modem_rx_audio_fast(&modem, impaired, impaired_count, decoded, sizeof(decoded), &decoded_length, &fast_metrics, &fast_workspace);
        if (status == RTNC_MODEM_OK && decoded_length == PAYLOAD_BYTES && memcmp(decoded, payload, PAYLOAD_BYTES) == 0)
            ++fast_successes;
        if (fast_metrics.frame_detected)
            fast_evm += fast_metrics.evm_rms;
        decoded_length = 0U;
        status = rtnc_modem_rx_audio(&modem, impaired, impaired_count, decoded, sizeof(decoded), &decoded_length, &full_metrics, &full_workspace);
        if (status == RTNC_MODEM_OK && decoded_length == PAYLOAD_BYTES && memcmp(decoded, payload, PAYLOAD_BYTES) == 0) {
            ++full_successes;
            if (full_metrics.equalizer_used)
                ++equalizer_successes;
        }
        if (full_metrics.frame_detected)
            full_evm += full_metrics.evm_rms;
    }
    (void) printf("profile=16psk_1000 trials=%u fast_successes=%u full_successes=%u equalizer_successes=%u fast_evm=%.6f full_evm=%.6f\n", TRIALS, fast_successes, full_successes, equalizer_successes, fast_evm / TRIALS, full_evm / TRIALS);
    assert(fast_successes == 0U);
    assert(full_successes == TRIALS);
    assert(equalizer_successes == TRIALS);
    assert(fast_evm / TRIALS > 0.25);
    assert(full_evm / TRIALS < 0.07);
    rtnc_modem_deinit(&modem);
    return 0;
}
