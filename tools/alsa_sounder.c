#include "rtnc/audio.h"
#include "rtnc/audio_ring.h"
#include "rtnc/platform_config.h"
#include "rtnc/ptt.h"
#include "rtnc/transmitter.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum {
    FIRST_TONE_HZ = 300U,
    LAST_TONE_HZ = 3300U,
    TONE_STEP_HZ = 100U,
    LEAD_MS = 500U,
    MARKER_MS = 500U,
    GAP_MS = 50U,
    TONE_MS = 150U,
    TAIL_MS = 500U,
};

static void add_tone(int16_t *pcm, size_t offset, size_t count, unsigned int sample_rate, float frequency_hz, int pcm_peak) {
    const float  pi = 3.14159265358979323846F;
    const size_t ramp = sample_rate / 200U;
    size_t       index;
    for (index = 0U; index < count; ++index) {
        float envelope = 1.0F;
        if (index < ramp) {
            envelope = (float) index / (float) ramp;
        } else if (count - index <= ramp) {
            envelope = (float) (count - index - 1U) / (float) ramp;
        }
        pcm[offset + index] =
            (int16_t) lrintf((float) pcm_peak * envelope * sinf(2.0F * pi * frequency_hz * (float) index / (float) sample_rate));
    }
}

int main(int argc, char **argv) {
    rtnc_platform_config_t *config = NULL;
    rtnc_audio_ring_t       capture_ring;
    rtnc_audio_t            audio = { 0 };
    rtnc_ptt_t              ptt = { 0 };
    int16_t                *pcm = NULL;
    unsigned int            tone_count;
    size_t                  total_ms;
    size_t                  sample_count;
    size_t                  offset;
    unsigned int            frequency_hz;
    int                     pcm_peak = 12000;
    unsigned int            calibration_hz = 0U;
    unsigned int            calibration_seconds = 0U;
    bool                    success = false;

    if (argc >= 3) {
        pcm_peak = (int) strtol(argv[2], NULL, 10);
    }
    if (argc >= 4) {
        calibration_hz = (unsigned int) strtoul(argv[3], NULL, 10);
    }
    if (argc >= 5) {
        calibration_seconds = (unsigned int) strtoul(argv[4], NULL, 10);
    }
    if (argc < 2 || argc > 5 || pcm_peak < 500 || pcm_peak > 16000 ||
        ((calibration_hz == 0U) != (calibration_seconds == 0U)) ||
        calibration_hz > 3500U ||
        (calibration_hz > 0U && calibration_hz < 200U) ||
        calibration_seconds > 120U ||
        !rtnc_platform_config_load(argv[1], &config)) {
        (void) fprintf(stderr, "usage: %s CONFIG.yaml [PCM_PEAK "
                               "[CAL_TONE_HZ CAL_SECONDS]]\n",
                       argv[0]);
        goto done;
    }
    tone_count = (LAST_TONE_HZ - FIRST_TONE_HZ) / TONE_STEP_HZ + 1U;
    total_ms = calibration_hz > 0U
                   ? (size_t) calibration_seconds * 1000U
                   : LEAD_MS + MARKER_MS + GAP_MS +
                         (size_t) tone_count * (TONE_MS + GAP_MS) + TAIL_MS;
    sample_count = total_ms * config->audio.sample_rate_hz / 1000U;
    pcm = calloc(sample_count, sizeof(*pcm));
    if (pcm == NULL) {
        goto done;
    }
    if (calibration_hz > 0U) {
        add_tone(pcm, 0U, sample_count, config->audio.sample_rate_hz, (float) calibration_hz, pcm_peak);
    } else {
        offset = (size_t) LEAD_MS * config->audio.sample_rate_hz / 1000U;
        add_tone(pcm, offset, (size_t) MARKER_MS * config->audio.sample_rate_hz / 1000U, config->audio.sample_rate_hz, 1000.0F, pcm_peak);
        offset += (size_t) (MARKER_MS + GAP_MS) *
                  config->audio.sample_rate_hz / 1000U;
        for (frequency_hz = FIRST_TONE_HZ; frequency_hz <= LAST_TONE_HZ;
             frequency_hz += TONE_STEP_HZ) {
            add_tone(pcm, offset, (size_t) TONE_MS * config->audio.sample_rate_hz / 1000U, config->audio.sample_rate_hz, (float) frequency_hz, pcm_peak);
            offset += (size_t) (TONE_MS + GAP_MS) *
                      config->audio.sample_rate_hz / 1000U;
        }
    }
    (void) rtnc_audio_ring_init(&capture_ring, 2U);
    if (!rtnc_audio_init(&audio, &config->audio, &capture_ring) ||
        !rtnc_ptt_init(&ptt, &config->ptt)) {
        (void) fprintf(stderr, "platform initialization failed\n");
        goto done;
    }
    (void) printf("sounder sample_rate=%u duration=%.3f peak=%d ", config->audio.sample_rate_hz, (double) sample_count / (double) config->audio.sample_rate_hz, pcm_peak);
    if (calibration_hz > 0U) {
        (void) printf("calibration_tone=%uHz\n", calibration_hz);
    } else {
        (void) printf("tones=300:100:3300Hz\n");
    }
    success = rtnc_transmit_audio(&audio, &ptt, &config->tx, pcm, sample_count);
    (void) printf("transmit=%s ptt=%s playback_xruns=%llu\n", success ? "ok" : "failed", rtnc_ptt_is_enabled(&ptt) ? "on" : "off", (unsigned long long) rtnc_audio_playback_xruns(&audio));

done:
    free(pcm);
    rtnc_ptt_deinit(&ptt);
    rtnc_audio_deinit(&audio);
    rtnc_platform_config_free(config);
    return success ? 0 : 1;
}
