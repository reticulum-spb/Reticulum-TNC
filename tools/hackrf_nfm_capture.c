#define _POSIX_C_SOURCE 200809L

#include <liquid/liquid.h>

#include <complex.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum { IQ_RATE = 2000000U,
       AUDIO_RATE = 48000U };

static int16_t pcm_sample(float value, float scale, uint64_t *clipped) {
    const float scaled = value * scale;
    if (scaled >= 32767.0F) {
        *clipped += 1U;
        return 32767;
    }
    if (scaled <= -32768.0F) {
        *clipped += 1U;
        return -32768;
    }
    return (int16_t) lrintf(scaled);
}

int main(int argc, char **argv) {
    const double         pi = 3.14159265358979323846;
    const char          *input_name;
    double               offset_hz = -70.0;
    float                pcm_scale = 30000.0F;
    float                dc_hz = 30.0F;
    FILE                *input = NULL;
    FILE                *output = NULL;
    bool                 close_input = true;
    msresamp_crcf        resampler = NULL;
    freqdem              demodulator = NULL;
    uint8_t              bytes[8192];
    liquid_float_complex iq[4096];
    liquid_float_complex channel[256];
    float                demodulated[256];
    double               phase = 0.0;
    float                previous_input = 0.0F;
    float                previous_output = 0.0F;
    float                dc_coefficient;
    uint64_t             sample_count = 0U;
    uint64_t             clipped = 0U;
    double               energy = 0.0;
    float                peak = 0.0F;
    size_t               bytes_read;
    int                  result = 1;

    if (argc < 4 || argc > 6) {
        (void) fprintf(stderr, "usage: %s IQ_S8_FILE OFFSET_HZ OUTPUT_S16 "
                               "[PCM_SCALE [DC_BLOCK_HZ]]\n",
                       argv[0]);
        return 2;
    }
    input_name = argv[1];
    offset_hz = strtod(argv[2], NULL);
    if (argc >= 5) {
        pcm_scale = strtof(argv[4], NULL);
    }
    if (argc == 6) {
        dc_hz = strtof(argv[5], NULL);
    }
    if (!isfinite(offset_hz) || offset_hz < -100000.0 ||
        offset_hz > 100000.0 || !isfinite(pcm_scale) || pcm_scale < 1000.0F ||
        pcm_scale > 1000000.0F || !isfinite(dc_hz) || dc_hz < 0.0F ||
        dc_hz > 1000.0F) {
        (void) fprintf(stderr, "invalid capture parameter\n");
        return 2;
    }
    if (input_name[0] == '-' && input_name[1] == '\0') {
        input = stdin;
        close_input = false;
    } else {
        input = fopen(input_name, "rb");
    }
    output = fopen(argv[3], "wb");
    resampler = msresamp_crcf_create((float) AUDIO_RATE / (float) IQ_RATE, 80.0F);
    demodulator = freqdem_create(1.0F);
    dc_coefficient =
        dc_hz > 0.0F
            ? expf(-2.0F * (float) pi * dc_hz / (float) AUDIO_RATE)
            : 0.0F;
    if (input == NULL || output == NULL || resampler == NULL ||
        demodulator == NULL) {
        (void) fprintf(stderr, "capture initialization failed\n");
        goto done;
    }
    while ((bytes_read = fread(bytes, 1U, sizeof(bytes), input)) > 0U) {
        const size_t iq_count = bytes_read / 2U;
        unsigned int channel_count = 0U;
        size_t       index;
        for (index = 0U; index < iq_count; ++index) {
            const float                i = (float) (int8_t) bytes[2U * index] / 128.0F;
            const float                q = (float) (int8_t) bytes[2U * index + 1U] / 128.0F;
            const liquid_float_complex mixer =
                (float) cos(phase) + (float) sin(phase) * I;
            iq[index] = (i + q * I) * mixer;
            phase -= 2.0 * pi * offset_hz / (double) IQ_RATE;
            if (phase > pi) {
                phase -= 2.0 * pi;
            } else if (phase < -pi) {
                phase += 2.0 * pi;
            }
        }
        if (msresamp_crcf_execute(resampler, iq, (unsigned int) iq_count, channel, &channel_count) != LIQUID_OK ||
            channel_count > 256U ||
            freqdem_demodulate_block(demodulator, channel, channel_count, demodulated) != LIQUID_OK) {
            (void) fprintf(stderr, "capture DSP failure\n");
            goto done;
        }
        for (index = 0U; index < channel_count; ++index) {
            float   sample = demodulated[index];
            int16_t pcm;
            if (dc_hz > 0.0F) {
                const float filtered = sample - previous_input +
                                       dc_coefficient * previous_output;
                previous_input = sample;
                previous_output = filtered;
                sample = filtered;
            }
            pcm = pcm_sample(sample, pcm_scale, &clipped);
            if (fwrite(&pcm, sizeof(pcm), 1U, output) != 1U) {
                (void) fprintf(stderr, "capture output failure\n");
                goto done;
            }
            energy += (double) sample * (double) sample;
            peak = fmaxf(peak, fabsf(sample));
            sample_count += 1U;
        }
    }
    (void) printf("samples=%llu duration=%.3f offset_hz=%.1f dc_hz=%.1f "
                  "pcm_scale=%.1f rms=%.7f peak=%.7f "
                  "deviation_rms_hz=%.1f deviation_peak_hz=%.1f clipped=%llu\n",
                  (unsigned long long) sample_count,
                  (double) sample_count / (double) AUDIO_RATE,
                  offset_hz,
                  (double) dc_hz,
                  (double) pcm_scale,
                  sample_count > 0U ? sqrt(energy / (double) sample_count) : 0.0,
                  (double) peak,
                  sample_count > 0U ? sqrt(energy / (double) sample_count) * AUDIO_RATE / (2.0 * pi) : 0.0,
                  (double) peak * AUDIO_RATE / (2.0 * pi),
                  (unsigned long long) clipped);
    result = 0;

done:
    if (demodulator != NULL) {
        (void) freqdem_destroy(demodulator);
    }
    if (resampler != NULL) {
        (void) msresamp_crcf_destroy(resampler);
    }
    if (output != NULL) {
        (void) fclose(output);
    }
    if (input != NULL && close_input) {
        (void) fclose(input);
    }
    return result;
}
