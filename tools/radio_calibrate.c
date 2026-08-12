#define _POSIX_C_SOURCE 200809L

#include "rtnc/audio.h"
#include "rtnc/audio_ring.h"
#include "rtnc/platform_config.h"
#include "rtnc/ptt.h"
#include "rtnc/radio_response.h"
#include "rtnc/transmitter.h"
#include "rtnc/tx_eq.h"

#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    SAMPLE_RATE = 48000U,
    MARKER_HZ = 1000U,
    MARKER_MS = 800U,
    PREFIX_MS = 500U,
    MARKER_GAP_MS = 200U,
    TONE_MS = 250U,
    GAP_MS = 50U,
    LEVEL_GAP_MS = 300U,
    COHERENT_GAP_MS = 300U,
    COHERENT_MS = 1200U,
    COHERENT_SKIP_MS = 200U,
    COHERENT_ANALYSIS_MS = 800U,
    BATCH_GAP_MS = 500U,
    CALIBRATION_CYCLES = 3U,
    TONE_COUNT = 8U,
    COHERENT_TONE_COUNT = 12U,
    LEVEL_COUNT = 4U,
    RX_SECONDS = 60U,
};

static const unsigned int tones[TONE_COUNT] = {
    600U,
    900U,
    1200U,
    1500U,
    1800U,
    2100U,
    2400U,
    2700U,
};
static const unsigned int coherent_tones[COHERENT_TONE_COUNT] = {
    600U, 800U, 1000U, 1200U, 1400U, 1600U,
    1800U, 2000U, 2200U, 2400U, 2600U, 2800U,
};
static const int             levels[LEVEL_COUNT] = { 4000, 8000, 12000, 16000 };
static volatile sig_atomic_t keep_running = 1;

static void stop_running(int signal_number) {
    (void) signal_number;
    keep_running = 0;
}

static void sleep_ms(unsigned int milliseconds) {
    struct timespec delay = {
        .tv_sec = (time_t) (milliseconds / 1000U),
        .tv_nsec = (long) (milliseconds % 1000U) * 1000000L,
    };
    while (nanosleep(&delay, &delay) < 0) {
    }
}

static void add_tone(int16_t *pcm, size_t offset, size_t count, unsigned int frequency, int peak) {
    const double pi = 3.14159265358979323846;
    const size_t ramp = SAMPLE_RATE / 200U;
    size_t       index;
    for (index = 0U; index < count; ++index) {
        double envelope = 1.0;
        if (index < ramp) {
            envelope = (double) index / (double) ramp;
        } else if (count - index <= ramp) {
            envelope = (double) (count - index - 1U) / (double) ramp;
        }
        pcm[offset + index] =
            (int16_t) lrint((double) peak * envelope * sin(2.0 * pi * (double) frequency * (double) index / (double) SAMPLE_RATE));
    }
}

static double coherent_initial_phase(size_t tone) {
    const double pi = 3.14159265358979323846;
    return pi * (double) tone * (double) tone / (double) COHERENT_TONE_COUNT;
}

static void add_coherent_multitone(int16_t *pcm, size_t offset, size_t count, int peak) {
    const double pi = 3.14159265358979323846;
    const size_t ramp = SAMPLE_RATE / 100U;
    double       maximum = 0.0;
    size_t       index;
    unsigned int tone;
    for (index = 0U; index < count; ++index) {
        double value = 0.0;
        for (tone = 0U; tone < COHERENT_TONE_COUNT; ++tone) {
            value += sin(2.0 * pi * (double) coherent_tones[tone] * (double) index / (double) SAMPLE_RATE + coherent_initial_phase(tone));
        }
        maximum = fmax(maximum, fabs(value));
    }
    for (index = 0U; index < count; ++index) {
        double envelope = 1.0;
        double value = 0.0;
        if (index < ramp) {
            envelope = (double) index / (double) ramp;
        } else if (count - index <= ramp) {
            envelope = (double) (count - index - 1U) / (double) ramp;
        }
        for (tone = 0U; tone < COHERENT_TONE_COUNT; ++tone) {
            value += sin(2.0 * pi * (double) coherent_tones[tone] * (double) index / (double) SAMPLE_RATE + coherent_initial_phase(tone));
        }
        pcm[offset + index] =
            (int16_t) lrint((double) peak * envelope * value / maximum);
    }
}

static size_t cycle_sample_count(void) {
    const size_t milliseconds =
        PREFIX_MS + MARKER_MS + MARKER_GAP_MS +
        LEVEL_COUNT * (TONE_COUNT * (TONE_MS + GAP_MS) + LEVEL_GAP_MS) +
        COHERENT_GAP_MS + COHERENT_MS;
    return milliseconds * SAMPLE_RATE / 1000U;
}

static bool build_cycle(int16_t *pcm, size_t count) {
    size_t       offset = PREFIX_MS * SAMPLE_RATE / 1000U;
    unsigned int level;
    unsigned int tone;
    if (pcm == NULL || count != cycle_sample_count()) {
        return false;
    }
    add_tone(pcm, offset, MARKER_MS * SAMPLE_RATE / 1000U, MARKER_HZ, levels[0]);
    offset += (MARKER_MS + MARKER_GAP_MS) * SAMPLE_RATE / 1000U;
    for (level = 0U; level < LEVEL_COUNT; ++level) {
        for (tone = 0U; tone < TONE_COUNT; ++tone) {
            add_tone(pcm, offset, TONE_MS * SAMPLE_RATE / 1000U, tones[tone], levels[level]);
            offset += (TONE_MS + GAP_MS) * SAMPLE_RATE / 1000U;
        }
        offset += LEVEL_GAP_MS * SAMPLE_RATE / 1000U;
    }
    offset += COHERENT_GAP_MS * SAMPLE_RATE / 1000U;
    add_coherent_multitone(
        pcm,
        offset,
        COHERENT_MS * SAMPLE_RATE / 1000U,
        levels[1]
    );
    offset += COHERENT_MS * SAMPLE_RATE / 1000U;
    return offset <= count;
}

static size_t batch_sample_count(void) {
    return CALIBRATION_CYCLES * cycle_sample_count() +
           (CALIBRATION_CYCLES - 1U) * BATCH_GAP_MS * SAMPLE_RATE / 1000U;
}

static bool build_batch(int16_t *pcm, size_t count) {
    const size_t gap_samples = BATCH_GAP_MS * SAMPLE_RATE / 1000U;
    size_t       offset = 0U;
    unsigned int cycle;
    if (pcm == NULL || count != batch_sample_count()) {
        return false;
    }
    for (cycle = 0U; cycle < CALIBRATION_CYCLES; ++cycle) {
        if (!build_cycle(&pcm[offset], cycle_sample_count())) {
            return false;
        }
        offset += cycle_sample_count();
        if (cycle + 1U < CALIBRATION_CYCLES) {
            offset += gap_samples;
        }
    }
    return offset == count;
}

static double tone_amplitude(const int16_t *samples, size_t count, unsigned int frequency) {
    const double pi = 3.14159265358979323846;
    const double step = 2.0 * pi * (double) frequency / (double) SAMPLE_RATE;
    double       real_part = 0.0;
    double       imag_part = 0.0;
    size_t       index;
    for (index = 0U; index < count; ++index) {
        const double phase = step * (double) index;
        real_part += (double) samples[index] * cos(phase);
        imag_part -= (double) samples[index] * sin(phase);
    }
    return 2.0 * hypot(real_part, imag_part) / (double) count;
}

static double complex tone_coefficient(const int16_t *samples, size_t count, unsigned int frequency) {
    const double pi = 3.14159265358979323846;
    const double step = 2.0 * pi * (double) frequency / (double) SAMPLE_RATE;
    double       real_part = 0.0;
    double       imaginary_part = 0.0;
    size_t       index;
    for (index = 0U; index < count; ++index) {
        const double phase = step * (double) index;
        real_part += (double) samples[index] * cos(phase);
        imaginary_part -= (double) samples[index] * sin(phase);
    }
    return 2.0 * (real_part + I * imaginary_part) / (double) count;
}

static bool find_marker_incremental(const int16_t *samples, size_t count, size_t *scan_offset, unsigned int *consecutive, size_t *marker_start) {
    const size_t window = SAMPLE_RATE / 10U;
    const size_t step = window / 2U;
    if (samples == NULL || scan_offset == NULL || consecutive == NULL ||
        marker_start == NULL) {
        return false;
    }
    while (*scan_offset + window <= count) {
        const size_t offset = *scan_offset;
        const double marker = tone_amplitude(&samples[offset], window, MARKER_HZ);
        const double adjacent =
            tone_amplitude(&samples[offset], window, 700U) +
            tone_amplitude(&samples[offset], window, 1300U) + 1.0;
        if (marker > 100.0 && marker / adjacent > 4.0) {
            *consecutive += 1U;
            if (*consecutive == 5U) {
                *marker_start = offset - 4U * step;
                return true;
            }
        } else {
            *consecutive = 0U;
        }
        *scan_offset += step;
    }
    return false;
}

static unsigned int select_linear_level(
    double amplitudes[LEVEL_COUNT][TONE_COUNT],
    double distortion[LEVEL_COUNT][TONE_COUNT]
) {
    unsigned int selected =
        distortion[1][1] <= 0.25 ? 1U : 0U;
    unsigned int level;
    unsigned int tone;
    for (level = 2U; level < LEVEL_COUNT; ++level) {
        bool linear = true;
        if (selected == 0U) {
            break;
        }
        for (tone = 0U; tone < TONE_COUNT; ++tone) {
            const double reference =
                amplitudes[1][tone] / (double) levels[1];
            const double normalized =
                amplitudes[level][tone] / (double) levels[level];
            if (reference <= 0.0 || normalized < 0.85 * reference) {
                linear = false;
            }
            if (tones[tone] == 900U && distortion[level][tone] > 0.25) {
                linear = false;
            }
        }
        if (linear) {
            selected = level;
        }
    }
    return selected;
}

static bool analyze_coherent_response(
    const int16_t                   *samples,
    size_t                           count,
    size_t                           marker,
    const rtnc_phy_profile_config_t *profile,
    rtnc_radio_response_metrics_t   *response_metrics,
    double complex                   response[COHERENT_TONE_COUNT]
) {
    const double pi = 3.14159265358979323846;
    const size_t coherent_start =
        marker +
        (MARKER_MS + MARKER_GAP_MS +
         LEVEL_COUNT *
             (TONE_COUNT * (TONE_MS + GAP_MS) + LEVEL_GAP_MS) +
         COHERENT_GAP_MS) *
            SAMPLE_RATE / 1000U;
    const size_t analysis_start =
        coherent_start + COHERENT_SKIP_MS * SAMPLE_RATE / 1000U;
    const size_t analysis_count =
        COHERENT_ANALYSIS_MS * SAMPLE_RATE / 1000U;
    double         unwrapped[COHERENT_TONE_COUNT];
    const double   half_band =
        0.5 * (double) profile->symbol_rate_baud *
        (1.0 + (double) profile->rrc_rolloff);
    unsigned int tone;
    if (analysis_start + analysis_count > count) {
        return false;
    }
    for (tone = 0U; tone < COHERENT_TONE_COUNT; ++tone) {
        const double expected_phase =
            coherent_initial_phase(tone) +
            2.0 * pi * (double) coherent_tones[tone] *
                (double) (COHERENT_SKIP_MS * SAMPLE_RATE / 1000U) /
                (double) SAMPLE_RATE -
            0.5 * pi;
        response[tone] =
            tone_coefficient(
                &samples[analysis_start],
                analysis_count,
                coherent_tones[tone]
            ) *
            cexp(-I * expected_phase);
    }
    if (!rtnc_radio_response_analyze(
            coherent_tones,
            response,
            COHERENT_TONE_COUNT,
            (double) profile->carrier_hz - half_band,
            (double) profile->carrier_hz + half_band,
            response_metrics,
            unwrapped
        )) {
        return false;
    }
    (void) printf(
        "occupied_band_hz=%.1f..%.1f amplitude_ripple_db=%.3f "
        "relative_delay_ms=%.3f group_delay_ripple_ms=%.3f "
        "residual_phase_rms_deg=%.2f points=%zu\n",
        (double) profile->carrier_hz - half_band,
        (double) profile->carrier_hz + half_band,
        response_metrics->amplitude_ripple_db,
        response_metrics->relative_delay_ms,
        response_metrics->group_delay_ripple_ms,
        response_metrics->residual_phase_rms_degrees,
        response_metrics->points_used
    );
    for (tone = 0U; tone < COHERENT_TONE_COUNT; ++tone) {
        (void) printf(
            "phase tone=%uHz magnitude=%.1f unwrapped_deg=%.2f\n",
            coherent_tones[tone],
            cabs(response[tone]),
            unwrapped[tone] * 180.0 / pi
        );
    }
    (void) printf(
        "phase_recommendation=%s\n",
        response_metrics->group_delay_ripple_ms <=
                150.0 / (double) profile->symbol_rate_baud
            ? "linear-phase response; keep identity TX phase response"
            : "significant group-delay ripple; validate RX equalizer with replay before designing phase correction"
    );
    return true;
}

static double standard_deviation(const double *values, size_t count, double mean) {
    double square_sum = 0.0;
    size_t index;
    for (index = 0U; index < count; ++index) {
        const double difference = values[index] - mean;
        square_sum += difference * difference;
    }
    return sqrt(square_sum / (double) count);
}

static const char *suitability_name(rtnc_radio_suitability_t suitability) {
    static const char *const names[] = { "POOR", "MARGINAL", "GOOD" };
    return suitability <= RTNC_RADIO_SUITABILITY_GOOD
               ? names[(unsigned int) suitability]
               : "POOR";
}

static int run_tx(const rtnc_platform_config_t *config, rtnc_audio_t *audio, rtnc_ptt_t *ptt) {
    const size_t           count = batch_sample_count();
    const rtnc_tx_config_t calibration_tx = {
        .lead_ms = config->tx.lead_ms,
        .tail_ms = config->tx.tail_ms,
        .filter_gain = 1.0F,
        .response_eq_taps = { [RTNC_TX_EQ_TAP_COUNT / 2U] = 1.0F },
    };
    int16_t     *pcm = calloc(count, sizeof(*pcm));
    unsigned int cycle = 0U;
    int          result = 1;
    if (pcm == NULL || !build_batch(pcm, count)) {
        goto done;
    }
    (void) signal(SIGINT, stop_running);
    (void) signal(SIGTERM, stop_running);
    (void) printf("calibration TX running; press Ctrl-C to stop\n");
    while (keep_running) {
        if (!rtnc_transmit_audio(audio, ptt, &calibration_tx, pcm, count)) {
            goto done;
        }
        cycle += CALIBRATION_CYCLES;
        (void) printf("cycles=%u transmit=ok playback_xruns=%llu\n", cycle, (unsigned long long) rtnc_audio_playback_xruns(audio));
        (void) fflush(stdout);
        sleep_ms(500U);
    }
    result = 0;
done:
    free(pcm);
    return result;
}

static int run_rx(const rtnc_platform_config_t *config, rtnc_audio_t *audio, rtnc_audio_ring_t *ring, const char *report_filename) {
    const size_t                     capacity = RX_SECONDS * SAMPLE_RATE;
    const rtnc_phy_profile_config_t *profile =
        rtnc_platform_selected_profile(config);
    int16_t                      *samples = calloc(capacity, sizeof(*samples));
    rtnc_audio_block_t            block;
    size_t                        count = 0U;
    size_t                        marker = 0U;
    size_t                        required = capacity;
    size_t                        marker_scan_offset = 0U;
    unsigned int                  marker_consecutive = 0U;
    bool                          marker_found = false;
    double                        amplitudes[LEVEL_COUNT][TONE_COUNT] = { { 0.0 } };
    double                        harmonic2_ratio[LEVEL_COUNT][TONE_COUNT] = { { 0.0 } };
    double                        harmonic3_ratio[LEVEL_COUNT][TONE_COUNT] = { { 0.0 } };
    double                        distortion[LEVEL_COUNT][TONE_COUNT] = { { 0.0 } };
    rtnc_radio_response_metrics_t response_metrics[CALIBRATION_CYCLES];
    double complex                coherent_response[CALIBRATION_CYCLES][COHERENT_TONE_COUNT];
    unsigned int                  selected;
    float                         eq_taps[RTNC_TX_EQ_TAP_COUNT];
    double                        gain;
    unsigned int                  level;
    unsigned int                  tone;
    int                           result = 1;
    if (samples == NULL || profile == NULL ||
        !rtnc_audio_start_capture(audio)) {
        goto done;
    }
    (void) printf("calibration RX listening for one complete cycle...\n");
    while (count < capacity && (!marker_found || count < required)) {
        if (rtnc_audio_ring_pop(ring, &block)) {
            const size_t available = capacity - count;
            const size_t copy = block.count < available ? block.count : available;
            (void) memcpy(&samples[count], block.samples, copy * sizeof(samples[0]));
            count += copy;
            if (!marker_found) {
                marker_found = find_marker_incremental(
                    samples,
                    count,
                    &marker_scan_offset,
                    &marker_consecutive,
                    &marker
                );
                if (marker_found) {
                    required = marker + batch_sample_count() -
                               PREFIX_MS * SAMPLE_RATE / 1000U;
                    (void) printf("marker detected; collecting %u cycles...\n", CALIBRATION_CYCLES);
                    (void) fflush(stdout);
                }
            }
        } else {
            sleep_ms(1U);
        }
    }
    rtnc_audio_stop_capture(audio);
    if (!marker_found) {
        (void) fprintf(stderr, "calibration marker not found\n");
        goto done;
    }
    {
        const size_t analysis_skip = 40U * SAMPLE_RATE / 1000U;
        const size_t analysis_count = 160U * SAMPLE_RATE / 1000U;
        unsigned int cycle;
        for (cycle = 0U; cycle < CALIBRATION_CYCLES; ++cycle) {
            const size_t cycle_marker = marker + cycle *
                                                     (cycle_sample_count() + BATCH_GAP_MS * SAMPLE_RATE / 1000U);
            const size_t sweep_start = cycle_marker +
                                       (MARKER_MS + MARKER_GAP_MS) * SAMPLE_RATE / 1000U;
            for (level = 0U; level < LEVEL_COUNT; ++level) {
                for (tone = 0U; tone < TONE_COUNT; ++tone) {
                    const size_t tone_index = level * TONE_COUNT + tone;
                    const size_t level_gaps = level * LEVEL_GAP_MS * SAMPLE_RATE / 1000U;
                    const size_t offset = sweep_start + tone_index * (TONE_MS + GAP_MS) * SAMPLE_RATE / 1000U + level_gaps + analysis_skip;
                    double       amplitude;
                    if (offset + analysis_count > count) {
                        (void) fprintf(stderr, "incomplete calibration cycle\n");
                        goto done;
                    }
                    amplitude = tone_amplitude(&samples[offset], analysis_count, tones[tone]);
                    amplitudes[level][tone] += amplitude;
                    if (amplitude > 0.0) {
                        const double harmonic2 = tone_amplitude(&samples[offset], analysis_count, 2U * tones[tone]);
                        const double harmonic3 = tone_amplitude(&samples[offset], analysis_count, 3U * tones[tone]);
                        harmonic2_ratio[level][tone] += harmonic2 / amplitude;
                        harmonic3_ratio[level][tone] += harmonic3 / amplitude;
                        distortion[level][tone] += hypot(harmonic2, harmonic3) / amplitude;
                    }
                }
            }
            (void) printf("cycle=%u\n", cycle + 1U);
            if (!analyze_coherent_response(samples, count, cycle_marker, profile, &response_metrics[cycle], coherent_response[cycle])) {
                (void) fprintf(stderr, "complex response analysis failed\n");
                goto done;
            }
        }
        for (level = 0U; level < LEVEL_COUNT; ++level) {
            for (tone = 0U; tone < TONE_COUNT; ++tone) {
                amplitudes[level][tone] /= CALIBRATION_CYCLES;
                harmonic2_ratio[level][tone] /= CALIBRATION_CYCLES;
                harmonic3_ratio[level][tone] /= CALIBRATION_CYCLES;
                distortion[level][tone] /= CALIBRATION_CYCLES;
            }
        }
    }
    selected = select_linear_level(amplitudes, distortion);
    (void) printf("profile=%s marker_sample=%zu selected_pcm_peak=%d\n", config->modem.profile, marker, levels[selected]);
    for (tone = 0U; tone < TONE_COUNT; ++tone) {
        (void) printf("tone=%uHz amplitude=%.1f h2=%.3f h3=%.3f "
                      "distortion=%.3f\n",
                      tones[tone],
                      amplitudes[selected][tone],
                      harmonic2_ratio[selected][tone],
                      harmonic3_ratio[selected][tone],
                      distortion[selected][tone]);
    }
    (void) printf("capture_xruns=%llu dropped_blocks=%llu\n", (unsigned long long) rtnc_audio_capture_xruns(audio), (unsigned long long) rtnc_audio_ring_dropped(ring));
    (void) fflush(stdout);
    if (distortion[selected][1U] > 0.25) {
        (void) fprintf(
            stderr,
            "calibration rejected: 900 Hz distortion %.1f%% at minimum "
            "usable level PCM%d; inspect H2/H3 and capture integrity before "
            "changing hardware gain\n",
            100.0 * distortion[selected][1U],
            levels[selected]
        );
        goto done;
    }
    {
        double       amplitude_values[CALIBRATION_CYCLES];
        double       delay_values[CALIBRATION_CYCLES];
        double       phase_values[CALIBRATION_CYCLES];
        double       amplitude_mean = 0.0;
        double       delay_mean = 0.0;
        double       phase_mean = 0.0;
        unsigned int cycle;
        FILE        *report = NULL;
        for (cycle = 0U; cycle < CALIBRATION_CYCLES; ++cycle) {
            amplitude_values[cycle] = response_metrics[cycle].amplitude_ripple_db;
            delay_values[cycle] = response_metrics[cycle].group_delay_ripple_ms;
            phase_values[cycle] = response_metrics[cycle].residual_phase_rms_degrees;
            amplitude_mean += amplitude_values[cycle];
            delay_mean += delay_values[cycle];
            phase_mean += phase_values[cycle];
        }
        amplitude_mean /= CALIBRATION_CYCLES;
        delay_mean /= CALIBRATION_CYCLES;
        phase_mean /= CALIBRATION_CYCLES;
        {
            rtnc_radio_response_metrics_t mean_metrics = {
                .amplitude_ripple_db = amplitude_mean,
                .group_delay_ripple_ms = delay_mean,
                .residual_phase_rms_degrees = phase_mean,
            };
            (void) printf(
                "summary cycles=%u amplitude_ripple_db=%.3f stddev=%.3f "
                "group_delay_ripple_ms=%.3f stddev=%.3f "
                "residual_phase_rms_deg=%.2f stddev=%.2f\n",
                CALIBRATION_CYCLES,
                amplitude_mean,
                standard_deviation(amplitude_values, CALIBRATION_CYCLES, amplitude_mean),
                delay_mean,
                standard_deviation(delay_values, CALIBRATION_CYCLES, delay_mean),
                phase_mean,
                standard_deviation(phase_values, CALIBRATION_CYCLES, phase_mean)
            );
            (void) printf(
                "suitability qpsk=%s 8psk=%s 16psk=%s stability=%s\n",
                suitability_name(rtnc_radio_response_suitability(&mean_metrics, 2U, profile->symbol_rate_baud)),
                suitability_name(rtnc_radio_response_suitability(&mean_metrics, 3U, profile->symbol_rate_baud)),
                suitability_name(rtnc_radio_response_suitability(&mean_metrics, 4U, profile->symbol_rate_baud)),
                standard_deviation(phase_values, CALIBRATION_CYCLES, phase_mean) <= 1.0 &&
                        standard_deviation(delay_values, CALIBRATION_CYCLES, delay_mean) <= 0.05
                    ? "GOOD"
                    : "UNSTABLE"
            );
        }
        (void) printf("profile_suitability name modulation baud low_hz high_hz amplitude_db group_delay_ms phase_deg grade stability\n");
        for (cycle = 0U; cycle < config->profiles_count; ++cycle) {
            const rtnc_phy_profile_config_t *entry = &config->profiles[cycle];
            rtnc_phy_profile_t phy;
            rtnc_radio_response_metrics_t per_cycle[CALIBRATION_CYCLES];
            double unwrapped[COHERENT_TONE_COUNT];
            double amplitude_profile = 0.0;
            double delay_profile = 0.0;
            double phase_profile = 0.0;
            double delay_profile_values[CALIBRATION_CYCLES];
            double phase_profile_values[CALIBRATION_CYCLES];
            double half_band;
            unsigned int measurement;
            if (!rtnc_platform_phy_profile_named(config, entry->name, &phy)) {
                goto done;
            }
            half_band = 0.5 * (double) phy.symbol_rate_baud *
                        (1.0 + (double) phy.rrc_rolloff);
            for (measurement = 0U; measurement < CALIBRATION_CYCLES; ++measurement) {
                if (!rtnc_radio_response_analyze(coherent_tones, coherent_response[measurement], COHERENT_TONE_COUNT, (double) phy.carrier_hz - half_band, (double) phy.carrier_hz + half_band, &per_cycle[measurement], unwrapped)) {
                    (void) fprintf(stderr, "profile response analysis failed: %s\n", entry->name);
                    goto done;
                }
                amplitude_profile += per_cycle[measurement].amplitude_ripple_db;
                delay_profile += per_cycle[measurement].group_delay_ripple_ms;
                phase_profile += per_cycle[measurement].residual_phase_rms_degrees;
                delay_profile_values[measurement] = per_cycle[measurement].group_delay_ripple_ms;
                phase_profile_values[measurement] = per_cycle[measurement].residual_phase_rms_degrees;
            }
            amplitude_profile /= CALIBRATION_CYCLES;
            delay_profile /= CALIBRATION_CYCLES;
            phase_profile /= CALIBRATION_CYCLES;
            {
                const rtnc_radio_response_metrics_t mean_profile = {
                    .amplitude_ripple_db = amplitude_profile,
                    .group_delay_ripple_ms = delay_profile,
                    .residual_phase_rms_degrees = phase_profile,
                };
                const bool stable = standard_deviation(phase_profile_values, CALIBRATION_CYCLES, phase_profile) <= 1.0 &&
                                    standard_deviation(delay_profile_values, CALIBRATION_CYCLES, delay_profile) <= 0.05;
                (void) printf("profile_suitability %s %s %u %.1f %.1f %.3f %.3f %.2f %s %s\n", entry->name, entry->modulation, entry->symbol_rate_baud, (double) phy.carrier_hz - half_band, (double) phy.carrier_hz + half_band, amplitude_profile, delay_profile, phase_profile, suitability_name(rtnc_radio_response_suitability(&mean_profile, phy.bits_per_symbol, phy.symbol_rate_baud)), stable ? "GOOD" : "UNSTABLE");
            }
        }
        if (report_filename != NULL) {
            report = fopen(report_filename, "w");
            if (report == NULL) {
                (void) fprintf(stderr, "cannot create report: %s\n", report_filename);
                goto done;
            }
            (void) fprintf(report, "cycle,amplitude_ripple_db,relative_delay_ms,group_delay_ripple_ms,residual_phase_rms_deg\n");
            for (cycle = 0U; cycle < CALIBRATION_CYCLES; ++cycle) {
                (void) fprintf(report, "%u,%.9g,%.9g,%.9g,%.9g\n", cycle + 1U, response_metrics[cycle].amplitude_ripple_db, response_metrics[cycle].relative_delay_ms, response_metrics[cycle].group_delay_ripple_ms, response_metrics[cycle].residual_phase_rms_degrees);
            }
            (void) fprintf(report, "mean,%.9g,,%.9g,%.9g\n", amplitude_mean, delay_mean, phase_mean);
            (void) fprintf(report, "\nprofile,modulation,baud,low_hz,high_hz,amplitude_ripple_db,group_delay_ripple_ms,residual_phase_rms_deg,suitability,stability\n");
            for (cycle = 0U; cycle < config->profiles_count; ++cycle) {
                const rtnc_phy_profile_config_t *entry = &config->profiles[cycle];
                rtnc_phy_profile_t phy;
                rtnc_radio_response_metrics_t per_cycle;
                rtnc_radio_response_metrics_t mean_profile = { 0 };
                double unwrapped[COHERENT_TONE_COUNT];
                double delay_profile_values[CALIBRATION_CYCLES];
                double phase_profile_values[CALIBRATION_CYCLES];
                double half_band;
                unsigned int measurement;
                if (!rtnc_platform_phy_profile_named(config, entry->name, &phy)) goto done;
                half_band = 0.5 * phy.symbol_rate_baud * (1.0 + phy.rrc_rolloff);
                for (measurement = 0U; measurement < CALIBRATION_CYCLES; ++measurement) {
                    if (!rtnc_radio_response_analyze(coherent_tones, coherent_response[measurement], COHERENT_TONE_COUNT, phy.carrier_hz - half_band, phy.carrier_hz + half_band, &per_cycle, unwrapped)) goto done;
                    mean_profile.amplitude_ripple_db += per_cycle.amplitude_ripple_db / CALIBRATION_CYCLES;
                    mean_profile.group_delay_ripple_ms += per_cycle.group_delay_ripple_ms / CALIBRATION_CYCLES;
                    mean_profile.residual_phase_rms_degrees += per_cycle.residual_phase_rms_degrees / CALIBRATION_CYCLES;
                    delay_profile_values[measurement] = per_cycle.group_delay_ripple_ms;
                    phase_profile_values[measurement] = per_cycle.residual_phase_rms_degrees;
                }
                (void) fprintf(report, "%s,%s,%u,%.9g,%.9g,%.9g,%.9g,%.9g,%s,%s\n", entry->name, entry->modulation, entry->symbol_rate_baud, phy.carrier_hz - half_band, phy.carrier_hz + half_band, mean_profile.amplitude_ripple_db, mean_profile.group_delay_ripple_ms, mean_profile.residual_phase_rms_degrees, suitability_name(rtnc_radio_response_suitability(&mean_profile, phy.bits_per_symbol, phy.symbol_rate_baud)), standard_deviation(phase_profile_values, CALIBRATION_CYCLES, mean_profile.residual_phase_rms_degrees) <= 1.0 && standard_deviation(delay_profile_values, CALIBRATION_CYCLES, mean_profile.group_delay_ripple_ms) <= 0.05 ? "GOOD" : "UNSTABLE");
            }
            if (fclose(report) != 0) {
                (void) fprintf(stderr, "cannot finish report: %s\n", report_filename);
                goto done;
            }
            (void) printf("report=%s\n", report_filename);
        }
    }
    {
        const float half_band = 0.5F * (float) profile->symbol_rate_baud *
                                (1.0F + profile->rrc_rolloff);
        if (!rtnc_tx_eq_design(tones, amplitudes[selected], TONE_COUNT, profile->carrier_hz, profile->carrier_hz - half_band, profile->carrier_hz + half_band, eq_taps)) {
            (void) fprintf(stderr, "inverse-response FIR design failed\n");
            goto done;
        }
    }
    gain = (double) levels[selected] / 16000.0;
    {
        double       maximum_magnitude = 1.0;
        const double half_band =
            0.5 * (double) profile->symbol_rate_baud *
            (1.0 + (double) profile->rrc_rolloff);
        for (tone = 0U; tone < TONE_COUNT; ++tone) {
            const double magnitude =
                rtnc_tx_eq_magnitude(eq_taps, (double) tones[tone]);
            if ((double) tones[tone] >=
                    (double) profile->carrier_hz - half_band &&
                (double) tones[tone] <=
                    (double) profile->carrier_hz + half_band &&
                magnitude > maximum_magnitude) {
                maximum_magnitude = magnitude;
            }
        }
        gain /= maximum_magnitude;
    }
    (void) printf("recommended configuration:\n"
                  "tx:\n"
                  "  filter_gain: %.3f\n"
                  "  response_eq_taps: [",
                  gain);
    for (tone = 0U; tone < RTNC_TX_EQ_TAP_COUNT; ++tone) {
        (void) printf("%s%.8g", tone == 0U ? "" : ", ", (double) eq_taps[tone]);
    }
    (void) printf("]\n");
    result = 0;
done:
    free(samples);
    return result;
}

int main(int argc, char **argv) {
    rtnc_platform_config_t *config = NULL;
    rtnc_audio_ring_t       ring;
    rtnc_audio_t            audio = { 0 };
    rtnc_ptt_t              ptt = { 0 };
    int                     result = 1;
    if ((argc != 3 && argc != 4) ||
        (strcmp(argv[2], "tx") != 0 && strcmp(argv[2], "rx") != 0) ||
        (argc == 4 && strcmp(argv[2], "rx") != 0) ||
        !rtnc_platform_config_load(argv[1], &config) ||
        config->audio.sample_rate_hz != SAMPLE_RATE ||
        !rtnc_audio_ring_init(&ring, RTNC_AUDIO_RING_MAX_CAPACITY) ||
        !rtnc_audio_init(&audio, &config->audio, &ring)) {
        (void) fprintf(stderr, "usage: %s CONFIG.yaml tx|rx [REPORT.csv]\n", argv[0]);
        goto done;
    }
    if (strcmp(argv[2], "tx") == 0) {
        if (!rtnc_ptt_init(&ptt, &config->ptt)) {
            (void) fprintf(stderr, "PTT initialization failed\n");
            goto done;
        }
        result = run_tx(config, &audio, &ptt);
    } else {
        result = run_rx(config, &audio, &ring, argc == 4 ? argv[3] : NULL);
    }
done:
    rtnc_ptt_deinit(&ptt);
    rtnc_audio_deinit(&audio);
    rtnc_platform_config_free(config);
    return result;
}
