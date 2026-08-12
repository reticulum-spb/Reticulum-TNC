#include "rtnc/modem.h"
#include "rtnc/wav.h"

#include <complex.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    static int16_t                pcm[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    static float                  audio[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    static rtnc_modem_workspace_t workspace;
    uint8_t                       payload[128U];
    uint8_t                       expected_payload[64U];
    uint8_t                       expected_frame[RTNC_FRAME_MAX_ENCODED_SIZE];
    uint8_t                       expected_protected[RTNC_MODEM_MAX_PROTECTED_BYTES] = { 0U };
    uint8_t                       expected_encoded[RTNC_MODEM_MAX_FEC_BYTES];
    rtnc_modem_t                  modem;
    rtnc_sync_metrics_t           metrics;
    FILE                         *stream;
    uint32_t                      sample_rate = 0U;
    size_t                        sample_count = 0U;
    size_t                        payload_length = 0U;
    size_t                        expected_frame_length = 0U;
    size_t                        expected_encoded_length = 0U;
    size_t                        hard_errors = 0U;
    size_t                        index;
    rtnc_modem_status_t           status;

    if (argc < 2 || argc > 4) {
        (void) fprintf(stderr, "usage: %s INPUT.wav [ACQUISITION_THRESHOLD "
                               "[TRAINING_THRESHOLD]]\n",
                       argv[0]);
        return 2;
    }
    stream = fopen(argv[1], "rb");
    if (stream == NULL) {
        (void) perror(argv[1]);
        return 1;
    }
    if (rtnc_wav_read_mono_s16(stream, &sample_rate, pcm, RTNC_MODEM_MAX_AUDIO_SAMPLES, &sample_count) !=
            RTNC_WAV_OK ||
        fclose(stream) != 0 || sample_rate != 48000U) {
        (void) fprintf(stderr, "expected mono S16 WAV at 48000 Hz\n");
        return 1;
    }
    for (index = 0U; index < sample_count; ++index) {
        audio[index] = (float) pcm[index] / 32768.0F;
    }
    for (index = 0U; index < sizeof(expected_payload); ++index) {
        expected_payload[index] = (uint8_t) (index * 37U + 0x29U);
    }
    if (!rtnc_modem_init_config(&modem, FEC_LDPC_ROBUST, 64U)) {
        return 1;
    }
    if (argc >= 3) {
        char       *end = NULL;
        const float threshold = strtof(argv[2], &end);
        if (end == argv[2] || *end != '\0' || threshold <= 0.0F ||
            threshold > 1.0F) {
            (void) fprintf(stderr, "invalid acquisition threshold\n");
            rtnc_modem_deinit(&modem);
            return 2;
        }
        modem.profile.acquisition_threshold = threshold;
    }
    if (argc == 4) {
        char       *end = NULL;
        const float threshold = strtof(argv[3], &end);
        if (end == argv[3] || *end != '\0' || threshold <= 0.0F ||
            threshold > 1.0F) {
            (void) fprintf(stderr, "invalid training threshold\n");
            rtnc_modem_deinit(&modem);
            return 2;
        }
        modem.profile.training_threshold = threshold;
    }
    status = rtnc_modem_rx_audio(&modem, audio, sample_count, payload, sizeof(payload), &payload_length, &metrics, &workspace);
    if (rtnc_frame_build(expected_payload, sizeof(expected_payload), expected_frame, sizeof(expected_frame), &expected_frame_length) == RTNC_FRAME_OK) {
        const size_t protected_length = RTNC_FRAME_HEADER_SIZE +
                                        sizeof(expected_payload) +
                                        RTNC_FRAME_CRC_SIZE;
        (void) memcpy(expected_protected, expected_frame, expected_frame_length);
        if (rtnc_fec_encode(FEC_LDPC_ROBUST, expected_protected, protected_length, expected_encoded, sizeof(expected_encoded), &expected_encoded_length) == RTNC_FEC_OK &&
            workspace.llr_count == expected_encoded_length * 8U) {
            for (index = 0U; index < workspace.llr_count; ++index) {
                const unsigned int expected =
                    ((unsigned int) expected_encoded[index / 8U] >>
                     (7U - (unsigned int) (index % 8U))) &
                    1U;
                const unsigned int received =
                    workspace.llr[index] < 0.0F ? 1U : 0U;
                if (expected != received) {
                    ++hard_errors;
                }
            }
        }
    }
    rtnc_modem_deinit(&modem);
    (void) printf("status=%d detected=%d payload_bytes=%zu acquisition=%.6f "
                  "training=%.6f timing_symbols=%.6f cfo_hz=%.3f "
                  "phase_radians=%.6f evm=%.6f effective_snr_db=%.3f "
                  "equalizer=%d "
                  "eq_training_error=%.6f fec_converged=%d fec_iterations=%u "
                  "hard_errors=%zu\n",
                  (int) status,
                  metrics.frame_detected ? 1 : 0,
                  payload_length,
                  (double) metrics.acquisition_correlation,
                  (double) metrics.training_correlation,
                  (double) metrics.timing_symbols,
                  (double) metrics.carrier_offset_hz,
                  (double) metrics.phase_radians,
                  (double) metrics.evm_rms,
                  (double) metrics.training_snr_db,
                  metrics.equalizer_used ? 1 : 0,
                  (double) metrics.equalizer_training_error,
                  workspace.fec_stats.converged ? 1 : 0,
                  workspace.fec_stats.iterations,
                  hard_errors);
    if (metrics.equalizer_used) {
        (void) printf("equalizer_taps=");
        for (index = 0U; index < RTNC_EQUALIZER_DIAGNOSTIC_TAPS; ++index) {
            (void) printf("%s%.6g%+.6gj", index == 0U ? "" : ",", (double) crealf(metrics.equalizer_taps[index]), (double) cimagf(metrics.equalizer_taps[index]));
        }
        (void) printf("\n");
    }
    if (status == RTNC_MODEM_OK) {
        (void) printf("payload=");
        for (index = 0U; index < payload_length; ++index) {
            (void) printf("%02x", (unsigned int) payload[index]);
        }
        (void) printf("\n");
    }
    return status == RTNC_MODEM_OK ? 0 : 1;
}
