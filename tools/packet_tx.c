#include "rtnc/audio.h"
#include "rtnc/audio_ring.h"
#include "rtnc/fragmentation.h"
#include "rtnc/modem.h"
#include "rtnc/platform_config.h"
#include "rtnc/ptt.h"
#include "rtnc/transmitter.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum { TX_PEAK = 16383 };

int main(int argc, char **argv) {
    rtnc_platform_config_t          *config = NULL;
    const rtnc_phy_profile_config_t *profile_config;
    rtnc_phy_profile_t               phy_profile;
    rtnc_modem_t                     modem;
    rtnc_audio_ring_t                capture_ring;
    rtnc_audio_t                     audio = { 0 };
    rtnc_ptt_t                       ptt = { 0 };
    float                            waveform[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    uint8_t                          fragment[RTNC_FRAME_MAX_PAYLOAD];
    uint8_t                         *packet = NULL;
    int16_t                         *series = NULL;
    const char                      *profile_name = NULL;
    unsigned int                     packet_count = 1U;
    unsigned int                     gap_ms = 0U;
    size_t                           packet_bytes = 500U;
    size_t                           fragments;
    size_t                           radio_frames;
    size_t                           gap_samples;
    size_t                           capacity;
    size_t                           used = 0U;
    long                             drop_fragment = -1L;
    size_t                           transmitted_frames = 0U;
    unsigned int                     packet_index;
    bool                             modem_initialized = false;
    bool                             success = false;

    if (argc < 2 || argc > 7) {
        (void) fprintf(stderr, "usage: %s CONFIG [PACKETS [GAP_MS [PROFILE "
                               "[PACKET_BYTES [DROP_FIRST_PACKET_FRAGMENT]]]]]\n",
                       argv[0]);
        return 2;
    }
    if (argc >= 3) {
        packet_count = (unsigned int) strtoul(argv[2], NULL, 10);
    }
    if (argc >= 4) {
        gap_ms = (unsigned int) strtoul(argv[3], NULL, 10);
    }
    if (argc >= 5) {
        profile_name = argv[4];
    }
    if (argc >= 6) {
        packet_bytes = (size_t) strtoul(argv[5], NULL, 10);
    }
    if (argc >= 7) {
        drop_fragment = strtol(argv[6], NULL, 10);
    }
    if (packet_count == 0U || packet_count > 100U || gap_ms > 5000U ||
        !rtnc_platform_config_load(argv[1], &config) ||
        packet_bytes < RTNC_LINK_MIN_MTU || packet_bytes > config->link.mtu) {
        (void) fprintf(stderr, "invalid packet TX configuration\n");
        goto done;
    }
    profile_config = profile_name != NULL
                         ? rtnc_platform_profile_config_named(config, profile_name)
                         : rtnc_platform_selected_profile(config);
    if (profile_config == NULL ||
        !rtnc_platform_phy_profile_named(config, profile_config->name, &phy_profile) ||
        !rtnc_modem_init_profile(&modem, (fec_mode_t) profile_config->fec_mode, profile_config->payload_class_bytes, &phy_profile)) {
        (void) fprintf(stderr, "modem initialization failed\n");
        goto done;
    }
    modem_initialized = true;
    fragments = rtnc_fragment_count(packet_bytes, profile_config->payload_class_bytes);
    if (fragments == 0U || fragments > RTNC_FRAGMENT_MAX_COUNT) {
        (void) fprintf(stderr, "packet MTU exceeds selected radio profile\n");
        goto done;
    }
    if (drop_fragment >= 0L && (size_t) drop_fragment >= fragments) {
        (void) fprintf(stderr, "invalid fragment selected for drop\n");
        goto done;
    }
    radio_frames = (size_t) packet_count * fragments;
    gap_samples = (size_t) config->audio.sample_rate_hz * gap_ms / 1000U;
    if (radio_frames > SIZE_MAX / RTNC_MODEM_MAX_AUDIO_SAMPLES) {
        goto done;
    }
    capacity = radio_frames * RTNC_MODEM_MAX_AUDIO_SAMPLES +
               (radio_frames - 1U) * gap_samples;
    packet = calloc(packet_bytes, sizeof(*packet));
    series = calloc(capacity, sizeof(*series));
    if (packet == NULL || series == NULL ||
        !rtnc_audio_ring_init(&capture_ring, 2U) ||
        !rtnc_audio_init(&audio, &config->audio, &capture_ring) ||
        !rtnc_ptt_init(&ptt, &config->ptt)) {
        (void) fprintf(stderr, "platform initialization failed\n");
        goto done;
    }
    for (packet_index = 0U; packet_index < packet_count; ++packet_index) {
        size_t index;
        size_t fragment_index;
        for (index = 0U; index < packet_bytes; ++index) {
            packet[index] = (uint8_t) (index * 37U + packet_index);
        }
        packet[0] = (uint8_t) (packet_index >> 24U);
        packet[1] = (uint8_t) (packet_index >> 16U);
        packet[2] = (uint8_t) (packet_index >> 8U);
        packet[3] = (uint8_t) packet_index;
        for (fragment_index = 0U; fragment_index < fragments;
             ++fragment_index) {
            size_t fragment_length = 0U;
            size_t waveform_count = 0U;
            if (rtnc_fragment_build(
                    packet,
                    packet_bytes,
                    profile_config->payload_class_bytes,
                    fragment_index,
                    fragment,
                    sizeof(fragment),
                    &fragment_length
                ) != RTNC_FRAGMENT_OK ||
                rtnc_modem_tx_audio(&modem, fragment, fragment_length, waveform, RTNC_MODEM_MAX_AUDIO_SAMPLES, &waveform_count) != RTNC_MODEM_OK) {
                (void) fprintf(stderr, "fragment waveform generation failed\n");
                goto done;
            }
            if (packet_index == 0U && drop_fragment >= 0L &&
                fragment_index == (size_t) drop_fragment) {
                continue;
            }
            for (index = 0U; index < waveform_count; ++index) {
                const float scaled = waveform[index] * (float) TX_PEAK;
                series[used + index] = (int16_t) lrintf(
                    fmaxf(-32767.0F, fminf(32767.0F, scaled))
                );
            }
            used += waveform_count;
            transmitted_frames += 1U;
            if (packet_index + 1U < packet_count ||
                fragment_index + 1U < fragments) {
                used += gap_samples;
            }
        }
    }
    success = rtnc_transmit_audio(&audio, &ptt, &config->tx, series, used);
    (void) printf("packets=%u packet_bytes=%zu radio_frames=%zu/%zu fragments=%zu "
                  "dropped_fragment=%ld "
                  "samples=%zu transmit=%s ptt=%s playback_xruns=%llu\n",
                  packet_count,
                  packet_bytes,
                  transmitted_frames,
                  radio_frames,
                  fragments,
                  drop_fragment,
                  used,
                  success ? "ok" : "failed",
                  rtnc_ptt_is_enabled(&ptt) ? "on" : "off",
                  (unsigned long long) rtnc_audio_playback_xruns(&audio));

done:
    free(series);
    free(packet);
    rtnc_ptt_deinit(&ptt);
    rtnc_audio_deinit(&audio);
    if (modem_initialized) {
        rtnc_modem_deinit(&modem);
    }
    rtnc_platform_config_free(config);
    return success ? 0 : 1;
}
