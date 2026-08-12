#ifndef RTNC_FRAME_H
#define RTNC_FRAME_H

#include <stddef.h>
#include <stdint.h>

enum {
    RTNC_FRAME_MAX_PAYLOAD = 128U,
    RTNC_FRAME_HEADER_SIZE = 1U,
    RTNC_FRAME_CRC_SIZE = 2U,
    RTNC_FRAME_MAX_ENCODED_SIZE =
        RTNC_FRAME_HEADER_SIZE + RTNC_FRAME_MAX_PAYLOAD + RTNC_FRAME_CRC_SIZE,
};

typedef enum {
    RTNC_FRAME_OK = 0,
    RTNC_FRAME_INVALID_ARGUMENT,
    RTNC_FRAME_BUFFER_TOO_SMALL,
    RTNC_FRAME_INVALID_LENGTH,
    RTNC_FRAME_CRC_FAILURE,
} rtnc_frame_status_t;

/** CRC-16/CCITT-FALSE used by the experimental Phase 1 frame. */
uint16_t rtnc_crc16_ccitt_false(const uint8_t *data, size_t length);

/** Build one experimental, unfecoded frame into a caller-owned buffer. */
rtnc_frame_status_t rtnc_frame_build(const uint8_t *payload, size_t payload_length, uint8_t *encoded, size_t capacity, size_t *encoded_length);

/** Validate and copy one complete experimental frame. */
rtnc_frame_status_t rtnc_frame_parse(const uint8_t *encoded, size_t encoded_length, uint8_t *payload, size_t capacity, size_t *payload_length);

#endif
