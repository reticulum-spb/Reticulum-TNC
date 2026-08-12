#include "rtnc/modem.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum {
    AUDIO_RATE = 48000U,
    IQ_RATE = 2400000U,
    IQ_PER_AUDIO = IQ_RATE / AUDIO_RATE,
    PAYLOAD_BYTES = 64U,
};

static bool write_audio_sample(FILE *stream, float audio, float deviation_hz, float *phase) {
    const float amplitude = 100.0F;
    const float increment =
        2.0F * (float) M_PI * deviation_hz * audio / (float) IQ_RATE;
    unsigned int index;
    for (index = 0U; index < IQ_PER_AUDIO; ++index) {
        const int8_t iq[2] = {
            (int8_t) lrintf(amplitude * cosf(*phase)),
            (int8_t) lrintf(amplitude * sinf(*phase)),
        };
        if (fwrite(iq, sizeof(iq[0]), 2U, stream) != 2U) {
            return false;
        }
        *phase += increment;
        if (*phase > (float) M_PI) {
            *phase -= 2.0F * (float) M_PI;
        } else if (*phase < -(float) M_PI) {
            *phase += 2.0F * (float) M_PI;
        }
    }
    return true;
}

static bool write_silence(FILE *stream, size_t samples, float deviation_hz, float *phase) {
    size_t index;
    for (index = 0U; index < samples; ++index) {
        if (!write_audio_sample(stream, 0.0F, deviation_hz, phase)) {
            return false;
        }
    }
    return true;
}

int main(int argc, char **argv) {
    static float modem_audio[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    static float transmit_audio[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    uint8_t      payload[PAYLOAD_BYTES];
    rtnc_modem_t modem;
    FILE        *stream;
    float        phase = 0.0F;
    float        deviation_hz = 2200.0F;
    float        preemphasis_us = 0.0F;
    float        peak = 0.0F;
    size_t       modem_samples = 0U;
    size_t       index;
    unsigned int repetition;
    unsigned int repetitions = 3U;
    bool         calibration_tone = true;

    if (argc < 2 || argc > 6) {
        (void) fprintf(stderr, "usage: %s OUTPUT.cs8 [DEVIATION_HZ [PREEMPHASIS_US "
                               "[REPETITIONS [CALIBRATION_TONE_0_OR_1]]]]\n",
                       argv[0]);
        return 2;
    }
    if (argc >= 3) {
        char *end = NULL;
        deviation_hz = strtof(argv[2], &end);
        if (end == argv[2] || *end != '\0' || deviation_hz < 500.0F ||
            deviation_hz > 4000.0F) {
            (void) fprintf(stderr, "invalid deviation\n");
            return 2;
        }
    }
    if (argc >= 4) {
        char *end = NULL;
        preemphasis_us = strtof(argv[3], &end);
        if (end == argv[3] || *end != '\0' || preemphasis_us < 0.0F ||
            preemphasis_us > 2000.0F) {
            (void) fprintf(stderr, "invalid pre-emphasis time constant\n");
            return 2;
        }
    }
    if (argc >= 5) {
        char               *end = NULL;
        const unsigned long parsed = strtoul(argv[4], &end, 10);
        if (end == argv[4] || *end != '\0' || parsed == 0UL || parsed > 50UL) {
            (void) fprintf(stderr, "invalid repetition count\n");
            return 2;
        }
        repetitions = (unsigned int) parsed;
    }
    if (argc == 6) {
        if (argv[5][0] == '0' && argv[5][1] == '\0') {
            calibration_tone = false;
        } else if (!(argv[5][0] == '1' && argv[5][1] == '\0')) {
            (void) fprintf(stderr, "invalid calibration tone flag\n");
            return 2;
        }
    }
    for (index = 0U; index < sizeof(payload); ++index) {
        payload[index] = (uint8_t) (index * 37U + 0x29U);
    }
    if (!rtnc_modem_init_config(&modem, FEC_LDPC_ROBUST, PAYLOAD_BYTES) ||
        rtnc_modem_tx_audio(&modem, payload, sizeof(payload), modem_audio, RTNC_MODEM_MAX_AUDIO_SAMPLES, &modem_samples) !=
            RTNC_MODEM_OK) {
        (void) fprintf(stderr, "could not generate modem waveform\n");
        return 1;
    }
    rtnc_modem_deinit(&modem);
    if (preemphasis_us > 0.0F) {
        const float tau_seconds = preemphasis_us * 1.0e-6F;
        const float coefficient =
            expf(-1.0F / ((float) AUDIO_RATE * tau_seconds));
        float previous = 0.0F;
        for (index = 0U; index < modem_samples; ++index) {
            transmit_audio[index] = modem_audio[index] - coefficient * previous;
            previous = modem_audio[index];
            peak = fmaxf(peak, fabsf(transmit_audio[index]));
        }
    } else {
        for (index = 0U; index < modem_samples; ++index) {
            transmit_audio[index] = modem_audio[index];
            peak = fmaxf(peak, fabsf(transmit_audio[index]));
        }
    }
    if (peak < 1.0e-6F) {
        return 1;
    }
    stream = fopen(argv[1], "wb");
    if (stream == NULL) {
        (void) perror(argv[1]);
        return 1;
    }
    if (!write_silence(stream, AUDIO_RATE / 4U, deviation_hz, &phase)) {
        (void) fclose(stream);
        return 1;
    }
    if (calibration_tone) {
        for (index = 0U; index < AUDIO_RATE / 2U; ++index) {
            const float tone =
                0.35F * sinf(2.0F * (float) M_PI * 1000.0F * (float) index / (float) AUDIO_RATE);
            if (!write_audio_sample(stream, tone, deviation_hz, &phase)) {
                (void) fclose(stream);
                return 1;
            }
        }
        if (!write_silence(stream, AUDIO_RATE / 4U, deviation_hz, &phase)) {
            (void) fclose(stream);
            return 1;
        }
    }
    for (repetition = 0U; repetition < repetitions; ++repetition) {
        for (index = 0U; index < modem_samples; ++index) {
            if (!write_audio_sample(stream, 0.70F * transmit_audio[index] / peak, deviation_hz, &phase)) {
                (void) fclose(stream);
                return 1;
            }
        }
        if (!write_silence(stream, AUDIO_RATE / 4U, deviation_hz, &phase)) {
            (void) fclose(stream);
            return 1;
        }
    }
    if (fclose(stream) != 0) {
        return 1;
    }
    (void) printf("iq_rate=%u deviation_hz=%.1f modem_peak_deviation_hz=%.1f "
                  "preemphasis_us=%.1f "
                  "modem_samples=%zu "
                  "modem_peak=%.6f payload_seed=37*x+0x29 repetitions=%u\n",
                  IQ_RATE,
                  (double) deviation_hz,
                  (double) (0.70F * deviation_hz),
                  (double) preemphasis_us,
                  modem_samples,
                  (double) peak,
                  repetitions);
    return 0;
}
