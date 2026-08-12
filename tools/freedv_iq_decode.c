#define _POSIX_C_SOURCE 200809L

#include <codec2/freedv_api.h>
#include <liquid/liquid.h>

#include <complex.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum {
    IQ_RATE = 2000000U,
    AUDIO_RATE = 48000U,
    FREEDV_RATE = 8000U,
    PCM_SCALE = 100000U,
    OUTPUT_PCM_SCALE = 30000U,
};

static short clamp_short(float value) {
    if (value > 32767.0F) {
        return 32767;
    }
    if (value < -32768.0F) {
        return -32768;
    }
    return (short) lrintf(value);
}

int main(int argc, char **argv) {
    const double           pi = 3.14159265358979323846;
    const char            *filename;
    double                 offset_hz = -70.0;
    unsigned int           output_rate = FREEDV_RATE;
    FILE                  *stream = NULL;
    FILE                  *audio_stream = NULL;
    bool                   close_input = true;
    struct freedv         *freedv = NULL;
    struct freedv_advanced advanced = {
        .interleave_frames = 0,
        .M = 4,
        .Rs = 500,
        .Fs = FREEDV_RATE,
        .first_tone = 1000,
        .tone_spacing = 600,
        .codename = "H_256_768_22",
    };
    msresamp_crcf        iq_resampler = NULL;
    msresamp_rrrf        audio_resampler = NULL;
    freqdem              demodulator = NULL;
    uint8_t              input[8192];
    liquid_float_complex iq[4096];
    liquid_float_complex channel[256];
    float                demodulated[256];
    float                audio_8k[64];
    short               *nin_buffer = NULL;
    unsigned char        frame[64];
    size_t               nin_count = 0U;
    size_t               frame_count = 0U;
    size_t               crc_count = 0U;
    size_t               audio_samples = 0U;
    double               audio_energy = 0.0;
    float                audio_peak = 0.0F;
    double               phase = 0.0;
    size_t               bytes_read;
    int                  result = 1;

    if (argc < 2 || argc > 5) {
        (void) fprintf(stderr, "usage: %s IQ_S8_FILE [OFFSET_HZ [AUDIO_S16_FILE "
                               "[8000|48000]]]\n",
                       argv[0]);
        return 2;
    }
    filename = argv[1];
    if (argc == 3) {
        offset_hz = strtod(argv[2], NULL);
    } else if (argc >= 4) {
        offset_hz = strtod(argv[2], NULL);
        audio_stream = fopen(argv[3], "wb");
        if (argc == 5) {
            output_rate = (unsigned int) strtoul(argv[4], NULL, 10);
        }
    }
    if (output_rate != FREEDV_RATE && output_rate != AUDIO_RATE) {
        (void) fprintf(stderr, "audio output rate must be 8000 or 48000\n");
        return 2;
    }
    if (filename[0] == '-' && filename[1] == '\0') {
        stream = stdin;
        close_input = false;
    } else {
        stream = fopen(filename, "rb");
    }
    freedv = freedv_open_advanced(FREEDV_MODE_FSK_LDPC, &advanced);
    iq_resampler = msresamp_crcf_create((float) AUDIO_RATE / (float) IQ_RATE, 80.0F);
    audio_resampler = msresamp_rrrf_create(
        (float) FREEDV_RATE / (float) AUDIO_RATE,
        80.0F
    );
    demodulator = freqdem_create(1.0F);
    if (stream == NULL || freedv == NULL || iq_resampler == NULL ||
        audio_resampler == NULL || demodulator == NULL) {
        (void) fprintf(stderr, "initialization failed\n");
        goto done;
    }
    freedv_set_frames_per_burst(freedv, 1);
    nin_buffer = calloc((size_t) freedv_get_n_max_modem_samples(freedv), sizeof(*nin_buffer));
    if (nin_buffer == NULL) {
        goto done;
    }
    while ((bytes_read = fread(input, 1U, sizeof(input), stream)) > 0U) {
        const size_t iq_count = bytes_read / 2U;
        unsigned int channel_count = 0U;
        unsigned int audio_count = 0U;
        size_t       index;
        for (index = 0U; index < iq_count; ++index) {
            const float                i = (float) (int8_t) input[2U * index] / 128.0F;
            const float                q = (float) (int8_t) input[2U * index + 1U] / 128.0F;
            const liquid_float_complex mixer =
                (float) cos(phase) + (float) sin(phase) * I;
            iq[index] = (i + q * I) * mixer;
            phase += -2.0 * pi * offset_hz / (double) IQ_RATE;
            if (phase > pi) {
                phase -= 2.0 * pi;
            } else if (phase < -pi) {
                phase += 2.0 * pi;
            }
        }
        if (msresamp_crcf_execute(iq_resampler, iq, (unsigned int) iq_count, channel, &channel_count) != LIQUID_OK ||
            channel_count > 256U ||
            freqdem_demodulate_block(demodulator, channel, channel_count, demodulated) != LIQUID_OK ||
            msresamp_rrrf_execute(audio_resampler, demodulated, channel_count, audio_8k, &audio_count) != LIQUID_OK ||
            audio_count > 64U) {
            (void) fprintf(stderr, "DSP failure\n");
            goto done;
        }
        if (audio_stream != NULL && output_rate == AUDIO_RATE) {
            for (index = 0U; index < channel_count; ++index) {
                const short sample =
                    clamp_short(demodulated[index] * (float) OUTPUT_PCM_SCALE);
                (void) fwrite(&sample, sizeof(sample), 1U, audio_stream);
            }
        }
        for (index = 0U; index < audio_count; ++index) {
            const size_t nin = (size_t) freedv_nin(freedv);
            const float  magnitude = fabsf(audio_8k[index]);
            audio_energy += (double) audio_8k[index] * audio_8k[index];
            audio_samples += 1U;
            if (magnitude > audio_peak) {
                audio_peak = magnitude;
            }
            const short sample =
                clamp_short(audio_8k[index] * (float) PCM_SCALE);
            if (audio_stream != NULL && output_rate == FREEDV_RATE) {
                const short output_sample = clamp_short(
                    audio_8k[index] * (float) OUTPUT_PCM_SCALE
                );
                (void) fwrite(&output_sample, sizeof(output_sample), 1U, audio_stream);
            }
            nin_buffer[nin_count++] = sample;
            if (nin_count == nin) {
                const int nbytes = freedv_rawdatarx(freedv, frame, nin_buffer);
                nin_count = 0U;
                if (nbytes > 0) {
                    const unsigned short expected =
                        (unsigned short) (((unsigned int) frame[nbytes - 2] << 8U) |
                                          frame[nbytes - 1]);
                    const unsigned short actual =
                        freedv_gen_crc16(frame, nbytes - 2);
                    ++frame_count;
                    if (actual == expected) {
                        ++crc_count;
                    }
                    (void) printf("frame=%zu bytes=%d header=0x%02x crc=%s\n", frame_count, nbytes, frame[0], actual == expected ? "ok" : "bad");
                }
            }
        }
    }
    (void) printf("finished frames=%zu crc_ok=%zu offset_hz=%.1f "
                  "audio_rms=%.6f audio_peak=%.6f\n",
                  frame_count,
                  crc_count,
                  offset_hz,
                  audio_samples > 0U ? sqrt(audio_energy / (double) audio_samples) : 0.0,
                  (double) audio_peak);
    result = 0;

done:
    free(nin_buffer);
    if (demodulator != NULL) {
        (void) freqdem_destroy(demodulator);
    }
    if (audio_resampler != NULL) {
        (void) msresamp_rrrf_destroy(audio_resampler);
    }
    if (iq_resampler != NULL) {
        (void) msresamp_crcf_destroy(iq_resampler);
    }
    if (freedv != NULL) {
        freedv_close(freedv);
    }
    if (stream != NULL && close_input) {
        (void) fclose(stream);
    }
    if (audio_stream != NULL) {
        (void) fclose(audio_stream);
    }
    return result;
}
