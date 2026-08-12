#ifndef RTNC_WAV_H
#define RTNC_WAV_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
    RTNC_WAV_OK = 0,
    RTNC_WAV_INVALID_ARGUMENT,
    RTNC_WAV_IO_ERROR,
    RTNC_WAV_INVALID_FORMAT,
    RTNC_WAV_BUFFER_TOO_SMALL,
} rtnc_wav_status_t;

/** Write mono signed 16-bit PCM samples to an already-open stream. */
rtnc_wav_status_t rtnc_wav_write_mono_s16(FILE *stream, uint32_t sample_rate_hz, const int16_t *samples, size_t sample_count);

/** Read mono signed 16-bit PCM into a caller-owned fixed buffer. */
rtnc_wav_status_t rtnc_wav_read_mono_s16(FILE *stream, uint32_t *sample_rate_hz, int16_t *samples, size_t capacity, size_t *sample_count);

#endif
