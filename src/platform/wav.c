#include "rtnc/wav.h"

#include <limits.h>
#include <stdbool.h>
#include <string.h>

static bool write_u16le(FILE *stream, uint16_t value) {
    const unsigned char bytes[2] = {
        (unsigned char) (value & 0xffU),
        (unsigned char) ((value >> 8U) & 0xffU),
    };
    return fwrite(bytes, 1U, sizeof(bytes), stream) == sizeof(bytes);
}

static bool write_u32le(FILE *stream, uint32_t value) {
    const unsigned char bytes[4] = {
        (unsigned char) (value & 0xffU),
        (unsigned char) ((value >> 8U) & 0xffU),
        (unsigned char) ((value >> 16U) & 0xffU),
        (unsigned char) ((value >> 24U) & 0xffU),
    };
    return fwrite(bytes, 1U, sizeof(bytes), stream) == sizeof(bytes);
}

static bool read_u16le(FILE *stream, uint16_t *value) {
    unsigned char bytes[2];
    if (fread(bytes, 1U, sizeof(bytes), stream) != sizeof(bytes)) {
        return false;
    }
    *value = (uint16_t) ((uint16_t) bytes[0] | ((uint16_t) bytes[1] << 8U));
    return true;
}

static bool read_u32le(FILE *stream, uint32_t *value) {
    unsigned char bytes[4];
    if (fread(bytes, 1U, sizeof(bytes), stream) != sizeof(bytes)) {
        return false;
    }
    *value = (uint32_t) bytes[0] | ((uint32_t) bytes[1] << 8U) |
             ((uint32_t) bytes[2] << 16U) | ((uint32_t) bytes[3] << 24U);
    return true;
}

rtnc_wav_status_t rtnc_wav_write_mono_s16(FILE *stream, uint32_t sample_rate_hz, const int16_t *samples, size_t sample_count) {
    size_t   index;
    uint32_t data_bytes;
    if (stream == NULL || sample_rate_hz == 0U ||
        (samples == NULL && sample_count != 0U) ||
        sample_count > ((size_t) UINT32_MAX / 2U)) {
        return RTNC_WAV_INVALID_ARGUMENT;
    }
    data_bytes = (uint32_t) (sample_count * 2U);
    if (fwrite("RIFF", 1U, 4U, stream) != 4U ||
        !write_u32le(stream, 36U + data_bytes) ||
        fwrite("WAVEfmt ", 1U, 8U, stream) != 8U ||
        !write_u32le(stream, 16U) || !write_u16le(stream, 1U) ||
        !write_u16le(stream, 1U) || !write_u32le(stream, sample_rate_hz) ||
        sample_rate_hz > (UINT32_MAX / 2U) ||
        !write_u32le(stream, sample_rate_hz * 2U) ||
        !write_u16le(stream, 2U) || !write_u16le(stream, 16U) ||
        fwrite("data", 1U, 4U, stream) != 4U ||
        !write_u32le(stream, data_bytes)) {
        return RTNC_WAV_IO_ERROR;
    }
    for (index = 0U; index < sample_count; ++index) {
        if (!write_u16le(stream, (uint16_t) samples[index])) {
            return RTNC_WAV_IO_ERROR;
        }
    }
    return RTNC_WAV_OK;
}

rtnc_wav_status_t rtnc_wav_read_mono_s16(FILE *stream, uint32_t *sample_rate_hz, int16_t *samples, size_t capacity, size_t *sample_count) {
    unsigned char id[4];
    uint32_t      ignored_size;
    uint32_t      fmt_size;
    uint32_t      data_size;
    uint16_t      format;
    uint16_t      channels;
    uint16_t      block_align;
    uint16_t      bits_per_sample;
    size_t        index;
    if (stream == NULL || sample_rate_hz == NULL || sample_count == NULL ||
        (samples == NULL && capacity != 0U)) {
        return RTNC_WAV_INVALID_ARGUMENT;
    }
    if (fread(id, 1U, 4U, stream) != 4U || memcmp(id, "RIFF", 4U) != 0 ||
        !read_u32le(stream, &ignored_size) ||
        fread(id, 1U, 4U, stream) != 4U || memcmp(id, "WAVE", 4U) != 0 ||
        fread(id, 1U, 4U, stream) != 4U || memcmp(id, "fmt ", 4U) != 0 ||
        !read_u32le(stream, &fmt_size) || fmt_size != 16U ||
        !read_u16le(stream, &format) || !read_u16le(stream, &channels) ||
        !read_u32le(stream, sample_rate_hz) ||
        !read_u32le(stream, &ignored_size) || !read_u16le(stream, &block_align) ||
        !read_u16le(stream, &bits_per_sample) ||
        fread(id, 1U, 4U, stream) != 4U || memcmp(id, "data", 4U) != 0 ||
        !read_u32le(stream, &data_size)) {
        return RTNC_WAV_INVALID_FORMAT;
    }
    if (format != 1U || channels != 1U || block_align != 2U ||
        bits_per_sample != 16U || (data_size % 2U) != 0U) {
        return RTNC_WAV_INVALID_FORMAT;
    }
    *sample_count = (size_t) data_size / 2U;
    if (*sample_count > capacity) {
        return RTNC_WAV_BUFFER_TOO_SMALL;
    }
    for (index = 0U; index < *sample_count; ++index) {
        uint16_t value;
        if (!read_u16le(stream, &value)) {
            return RTNC_WAV_IO_ERROR;
        }
        samples[index] = (int16_t) value;
    }
    return RTNC_WAV_OK;
}
