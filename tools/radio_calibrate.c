#define _POSIX_C_SOURCE 200809L

#include "rtnc/audio.h"
#include "rtnc/audio_ring.h"
#include "rtnc/platform_config.h"
#include "rtnc/ptt.h"
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
    TONE_COUNT = 8U,
    LEVEL_COUNT = 4U,
    RX_SECONDS = 28U,
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

static size_t cycle_sample_count(void) {
    const size_t milliseconds =
        PREFIX_MS + MARKER_MS + MARKER_GAP_MS +
        LEVEL_COUNT * (TONE_COUNT * (TONE_MS + GAP_MS) + LEVEL_GAP_MS);
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
    return offset <= count;
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

static int run_tx(const rtnc_platform_config_t *config, rtnc_audio_t *audio, rtnc_ptt_t *ptt) {
    const size_t           count = cycle_sample_count();
    const rtnc_tx_config_t calibration_tx = {
        .lead_ms = config->tx.lead_ms,
        .tail_ms = config->tx.tail_ms,
        .filter_gain = 1.0F,
        .response_eq_taps = { [RTNC_TX_EQ_TAP_COUNT / 2U] = 1.0F },
    };
    int16_t     *pcm = calloc(count, sizeof(*pcm));
    unsigned int cycle = 0U;
    int          result = 1;
    if (pcm == NULL || !build_cycle(pcm, count)) {
        goto done;
    }
    (void) signal(SIGINT, stop_running);
    (void) signal(SIGTERM, stop_running);
    (void) printf("calibration TX running; press Ctrl-C to stop\n");
    while (keep_running) {
        if (!rtnc_transmit_audio(audio, ptt, &calibration_tx, pcm, count)) {
            goto done;
        }
        cycle += 1U;
        (void) printf("cycle=%u transmit=ok playback_xruns=%llu\n", cycle, (unsigned long long) rtnc_audio_playback_xruns(audio));
        (void) fflush(stdout);
        sleep_ms(500U);
    }
    result = 0;
done:
    free(pcm);
    return result;
}

static int run_rx(const rtnc_platform_config_t *config, rtnc_audio_t *audio, rtnc_audio_ring_t *ring) {
    const size_t                     capacity = RX_SECONDS * SAMPLE_RATE;
    const rtnc_phy_profile_config_t *profile =
        rtnc_platform_selected_profile(config);
    int16_t           *samples = calloc(capacity, sizeof(*samples));
    rtnc_audio_block_t block;
    size_t             count = 0U;
    size_t             marker = 0U;
    size_t             required = capacity;
    size_t             marker_scan_offset = 0U;
    unsigned int       marker_consecutive = 0U;
    bool               marker_found = false;
    double             amplitudes[LEVEL_COUNT][TONE_COUNT] = { { 0.0 } };
    double             harmonic2_ratio[LEVEL_COUNT][TONE_COUNT] = { { 0.0 } };
    double             harmonic3_ratio[LEVEL_COUNT][TONE_COUNT] = { { 0.0 } };
    double             distortion[LEVEL_COUNT][TONE_COUNT] = { { 0.0 } };
    unsigned int       selected;
    float              eq_taps[RTNC_TX_EQ_TAP_COUNT];
    double             gain;
    unsigned int       level;
    unsigned int       tone;
    int                result = 1;
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
                    const size_t cycle_after_marker_ms =
                        MARKER_MS + MARKER_GAP_MS +
                        LEVEL_COUNT *
                            (TONE_COUNT * (TONE_MS + GAP_MS) + LEVEL_GAP_MS);
                    required = marker +
                               cycle_after_marker_ms * SAMPLE_RATE / 1000U;
                    (void) printf("marker detected; collecting sweep...\n");
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
        const size_t sweep_start =
            marker + (MARKER_MS + MARKER_GAP_MS) * SAMPLE_RATE / 1000U;
        const size_t analysis_skip = 40U * SAMPLE_RATE / 1000U;
        const size_t analysis_count = 160U * SAMPLE_RATE / 1000U;
        for (level = 0U; level < LEVEL_COUNT; ++level) {
            for (tone = 0U; tone < TONE_COUNT; ++tone) {
                const size_t tone_index = level * TONE_COUNT + tone;
                const size_t level_gaps = level * LEVEL_GAP_MS *
                                          SAMPLE_RATE / 1000U;
                const size_t offset =
                    sweep_start + tone_index * (TONE_MS + GAP_MS) * SAMPLE_RATE / 1000U +
                    level_gaps + analysis_skip;
                if (offset + analysis_count > count) {
                    (void) fprintf(stderr, "incomplete calibration cycle\n");
                    goto done;
                }
                amplitudes[level][tone] =
                    tone_amplitude(&samples[offset], analysis_count, tones[tone]);
                if (amplitudes[level][tone] > 0.0) {
                    const double harmonic2 =
                        tone_amplitude(&samples[offset], analysis_count, 2U * tones[tone]);
                    const double harmonic3 =
                        tone_amplitude(&samples[offset], analysis_count, 3U * tones[tone]);
                    harmonic2_ratio[level][tone] =
                        harmonic2 / amplitudes[level][tone];
                    harmonic3_ratio[level][tone] =
                        harmonic3 / amplitudes[level][tone];
                    distortion[level][tone] =
                        hypot(harmonic2, harmonic3) /
                        amplitudes[level][tone];
                }
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
    if (argc != 3 ||
        (strcmp(argv[2], "tx") != 0 && strcmp(argv[2], "rx") != 0) ||
        !rtnc_platform_config_load(argv[1], &config) ||
        config->audio.sample_rate_hz != SAMPLE_RATE ||
        !rtnc_audio_ring_init(&ring, RTNC_AUDIO_RING_MAX_CAPACITY) ||
        !rtnc_audio_init(&audio, &config->audio, &ring)) {
        (void) fprintf(stderr, "usage: %s CONFIG.yaml tx|rx\n", argv[0]);
        goto done;
    }
    if (strcmp(argv[2], "tx") == 0) {
        if (!rtnc_ptt_init(&ptt, &config->ptt)) {
            (void) fprintf(stderr, "PTT initialization failed\n");
            goto done;
        }
        result = run_tx(config, &audio, &ptt);
    } else {
        result = run_rx(config, &audio, &ring);
    }
done:
    rtnc_ptt_deinit(&ptt);
    rtnc_audio_deinit(&audio);
    rtnc_platform_config_free(config);
    return result;
}
