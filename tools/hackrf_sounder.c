#include "rtnc/hackrf_tx.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum {
    AUDIO_RATE = 48000U,
    FIRST_TONE_HZ = 300U,
    LAST_TONE_HZ = 3300U,
    TONE_STEP_HZ = 100U,
    LEAD_MS = 500U,
    MARKER_MS = 500U,
    GAP_MS = 50U,
    TONE_MS = 150U,
    TAIL_MS = 500U,
};

static void add_tone(float *audio, size_t offset, size_t count, float frequency_hz, float amplitude) {
    const float  pi = 3.14159265358979323846F;
    const size_t ramp = AUDIO_RATE / 200U;
    size_t       index;
    for (index = 0U; index < count; ++index) {
        float envelope = 1.0F;
        if (index < ramp) {
            envelope = (float) index / (float) ramp;
        } else if (count - index <= ramp) {
            envelope = (float) (count - index - 1U) / (float) ramp;
        }
        audio[offset + index] =
            amplitude * envelope *
            sinf(2.0F * pi * frequency_hz * (float) index / (float) AUDIO_RATE);
    }
}

int main(int argc, char **argv) {
    const unsigned int tone_count =
        (LAST_TONE_HZ - FIRST_TONE_HZ) / TONE_STEP_HZ + 1U;
    const size_t total_ms = LEAD_MS + MARKER_MS + GAP_MS +
                            (size_t) tone_count * (TONE_MS + GAP_MS) + TAIL_MS;
    const size_t            sample_count = total_ms * AUDIO_RATE / 1000U;
    rtnc_hackrf_tx_config_t config;
    float                  *audio = calloc(sample_count, sizeof(*audio));
    uint64_t                iq_samples = 0U;
    size_t                  offset = (size_t) LEAD_MS * AUDIO_RATE / 1000U;
    unsigned int            frequency_hz;

    rtnc_hackrf_tx_default_config(&config);
    config.lead_ms = 100U;
    config.tail_ms = 100U;
    if (argc > 1) {
        config.frequency_hz = strtoull(argv[1], NULL, 10);
    }
    if (argc > 2) {
        config.txvga_gain_db = (uint8_t) strtoul(argv[2], NULL, 10);
    }
    if (argc > 3) {
        config.deviation_hz = strtof(argv[3], NULL);
    }
    if (argc > 4 || audio == NULL ||
        !rtnc_hackrf_tx_config_is_valid(&config)) {
        (void) fprintf(stderr, "usage: %s [FREQ_HZ [TXVGA_DB [DEVIATION_HZ]]]\n", argv[0]);
        free(audio);
        return 2;
    }
    add_tone(audio, offset, (size_t) MARKER_MS * AUDIO_RATE / 1000U, 1000.0F, 0.50F);
    offset += (size_t) (MARKER_MS + GAP_MS) * AUDIO_RATE / 1000U;
    for (frequency_hz = FIRST_TONE_HZ; frequency_hz <= LAST_TONE_HZ;
         frequency_hz += TONE_STEP_HZ) {
        add_tone(audio, offset, (size_t) TONE_MS * AUDIO_RATE / 1000U, (float) frequency_hz, 0.50F);
        offset += (size_t) (TONE_MS + GAP_MS) * AUDIO_RATE / 1000U;
    }
    (void) printf("sounder freq=%llu txvga=%u deviation=%.1f duration=%.3f "
                  "marker=1000Hz/500ms tones=300:100:3300Hz/150ms gap=50ms\n",
                  (unsigned long long) config.frequency_hz,
                  (unsigned int) config.txvga_gain_db,
                  (double) config.deviation_hz,
                  (double) sample_count / AUDIO_RATE);
    (void) fflush(stdout);
    if (!rtnc_hackrf_tx_audio(&config, audio, sample_count, &iq_samples)) {
        free(audio);
        return 1;
    }
    free(audio);
    (void) printf("transmit_complete iq_samples=%llu rf_duration=%.3f\n", (unsigned long long) iq_samples, (double) iq_samples / (double) config.iq_sample_rate_hz);
    return 0;
}
