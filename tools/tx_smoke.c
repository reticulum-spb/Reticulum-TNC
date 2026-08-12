#define _POSIX_C_SOURCE 200809L

#include "rtnc/audio.h"
#include "rtnc/audio_ring.h"
#include "rtnc/modem.h"
#include "rtnc/platform_config.h"
#include "rtnc/ptt.h"
#include "rtnc/transmitter.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Match Codec2 FSK_SCALE used by the deployed FreeDV-TNC baseline. */
enum { TX_PEAK = 16383 };

int main(int argc, char **argv) {
    rtnc_platform_config_t          *config = NULL;
    rtnc_audio_ring_t                capture_ring;
    rtnc_audio_t                     audio = { 0 };
    rtnc_ptt_t                       ptt = { 0 };
    rtnc_modem_t                     modem;
    rtnc_phy_profile_t               phy_profile;
    const rtnc_phy_profile_config_t *selected_profile = NULL;
    const char                      *profile_name = NULL;
    float                            waveform[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    int16_t                         *pcm = NULL;
    uint8_t                          payload[64];
    size_t                           sample_count = 0U;
    size_t                           index;
    bool                             modem_initialized = false;
    bool                             success = false;
    unsigned int                     frame_count = 1U;
    unsigned int                     frame;
    unsigned int                     transmitted = 0U;
    unsigned int                     gap_ms = 750U;
    size_t                           gap_samples;
    size_t                           series_samples = 0U;
    size_t                           series_capacity;

    if (argc < 2 || argc > 5) {
        (void) fprintf(stderr, "usage: %s CONFIG.yaml [COUNT [GAP_MS [PROFILE]]]\n", argv[0]);
        return 2;
    }
    if (argc >= 4) {
        gap_ms = (unsigned int) strtoul(argv[3], NULL, 10);
        if (gap_ms > 5000U) {
            (void) fprintf(stderr, "invalid gap\n");
            return 2;
        }
    }
    if (argc >= 3) {
        frame_count = (unsigned int) strtoul(argv[2], NULL, 10);
        if (frame_count == 0U || frame_count > 1000U) {
            (void) fprintf(stderr, "invalid frame count\n");
            return 2;
        }
    }
    if (argc == 5) {
        profile_name = argv[4];
    }
    if (!rtnc_platform_config_load(argv[1], &config)) {
        (void) fprintf(stderr, "invalid configuration: %s\n", argv[1]);
        goto done;
    }
    selected_profile = profile_name != NULL
                           ? rtnc_platform_profile_config_named(config, profile_name)
                           : rtnc_platform_selected_profile(config);
    if (selected_profile == NULL ||
        !rtnc_platform_phy_profile_named(config, selected_profile->name, &phy_profile) ||
        !rtnc_modem_init_profile(&modem, (fec_mode_t) selected_profile->fec_mode, selected_profile->payload_class_bytes, &phy_profile)) {
        (void) fprintf(stderr, "failed to initialize modem\n");
        goto done;
    }
    modem_initialized = true;
    gap_samples = (size_t) config->audio.sample_rate_hz * gap_ms / 1000U;
    series_capacity = frame_count * RTNC_MODEM_MAX_AUDIO_SAMPLES +
                      (frame_count - 1U) * gap_samples;
    pcm = calloc(series_capacity, sizeof(*pcm));
    if (pcm == NULL) {
        (void) fprintf(stderr, "series allocation failed\n");
        goto done;
    }
    (void) rtnc_audio_ring_init(&capture_ring, 2U);
    if (!rtnc_audio_init(&audio, &config->audio, &capture_ring)) {
        (void) fprintf(stderr, "failed to open ALSA\n");
        goto done;
    }
    if (!rtnc_ptt_init(&ptt, &config->ptt)) {
        (void) fprintf(stderr, "failed to initialize PTT\n");
        goto done;
    }
    for (frame = 0U; frame < frame_count; ++frame) {
        for (index = 0U; index < sizeof(payload); ++index) {
            payload[index] = (uint8_t) (0xa5U ^ index);
        }
        payload[0] = (uint8_t) (frame >> 24U);
        payload[1] = (uint8_t) (frame >> 16U);
        payload[2] = (uint8_t) (frame >> 8U);
        payload[3] = (uint8_t) frame;
        if (rtnc_modem_tx_audio(&modem, payload, sizeof(payload), waveform, RTNC_MODEM_MAX_AUDIO_SAMPLES, &sample_count) !=
            RTNC_MODEM_OK) {
            (void) fprintf(stderr, "frame %u waveform generation failed\n", frame);
            continue;
        }
        for (index = 0U; index < sample_count; ++index) {
            const float scaled = waveform[index] * (float) TX_PEAK;
            pcm[series_samples + index] =
                (int16_t) lrintf(fmaxf(-32767.0F, fminf(32767.0F, scaled)));
        }
        series_samples += sample_count;
        if (frame + 1U < frame_count) {
            series_samples += gap_samples;
        }
    }
    if (rtnc_transmit_audio(&audio, &ptt, &config->tx, pcm, series_samples)) {
        transmitted = frame_count;
    }
    success = transmitted == frame_count;
    (void) printf("transmit=%u/%u ptt=%s playback_xruns=%llu\n", transmitted, frame_count, rtnc_ptt_is_enabled(&ptt) ? "on" : "off", (unsigned long long) rtnc_audio_playback_xruns(&audio));

done:
    free(pcm);
    rtnc_ptt_deinit(&ptt);
    rtnc_audio_deinit(&audio);
    if (modem_initialized) {
        rtnc_modem_deinit(&modem);
    }
    rtnc_platform_config_free(config);
    return success ? 0 : 1;
}
