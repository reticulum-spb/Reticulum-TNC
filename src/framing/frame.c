#include "rtnc/frame.h"

#include <string.h>

enum {
    HEADER_LENGTH_OFFSET = 0U,
};

uint16_t rtnc_crc16_ccitt_false(const uint8_t *data, size_t length) {
    uint16_t crc = 0xffffU;
    size_t   index;
    if (data == NULL && length != 0U) {
        return 0U;
    }
    for (index = 0U; index < length; ++index) {
        unsigned int bit;
        crc ^= (uint16_t) ((uint16_t) data[index] << 8U);
        for (bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 0x8000U) != 0U
                      ? (uint16_t) ((uint16_t) (crc << 1U) ^ 0x1021U)
                      : (uint16_t) (crc << 1U);
        }
    }
    return crc;
}

rtnc_frame_status_t rtnc_frame_build(const uint8_t *payload, size_t payload_length, uint8_t *encoded, size_t capacity, size_t *encoded_length) {
    size_t   required;
    uint16_t crc;
    if (encoded == NULL || encoded_length == NULL ||
        (payload == NULL && payload_length != 0U)) {
        return RTNC_FRAME_INVALID_ARGUMENT;
    }
    if (payload_length == 0U || payload_length > RTNC_FRAME_MAX_PAYLOAD) {
        return RTNC_FRAME_INVALID_LENGTH;
    }
    required = RTNC_FRAME_HEADER_SIZE + payload_length + RTNC_FRAME_CRC_SIZE;
    if (capacity < required) {
        return RTNC_FRAME_BUFFER_TOO_SMALL;
    }
    encoded[HEADER_LENGTH_OFFSET] = (uint8_t) (payload_length - 1U);
    if (payload_length != 0U) {
        (void) memcpy(&encoded[RTNC_FRAME_HEADER_SIZE], payload, payload_length);
    }
    crc = rtnc_crc16_ccitt_false(encoded, RTNC_FRAME_HEADER_SIZE + payload_length);
    encoded[required - 2U] = (uint8_t) (crc >> 8U);
    encoded[required - 1U] = (uint8_t) (crc & 0xffU);
    *encoded_length = required;
    return RTNC_FRAME_OK;
}

rtnc_frame_status_t rtnc_frame_parse(const uint8_t *encoded, size_t encoded_length, uint8_t *payload, size_t capacity, size_t *payload_length) {
    size_t   declared_length;
    size_t   required;
    uint16_t expected_crc;
    uint16_t received_crc;
    if (encoded == NULL || payload_length == NULL ||
        (payload == NULL && capacity != 0U)) {
        return RTNC_FRAME_INVALID_ARGUMENT;
    }
    if (encoded_length < RTNC_FRAME_HEADER_SIZE + RTNC_FRAME_CRC_SIZE) {
        return RTNC_FRAME_INVALID_LENGTH;
    }
    declared_length = (size_t) encoded[HEADER_LENGTH_OFFSET] + 1U;
    if (declared_length > RTNC_FRAME_MAX_PAYLOAD) {
        return RTNC_FRAME_INVALID_LENGTH;
    }
    required = RTNC_FRAME_HEADER_SIZE + declared_length + RTNC_FRAME_CRC_SIZE;
    if (encoded_length != required) {
        return RTNC_FRAME_INVALID_LENGTH;
    }
    if (capacity < declared_length) {
        return RTNC_FRAME_BUFFER_TOO_SMALL;
    }
    expected_crc = rtnc_crc16_ccitt_false(encoded, RTNC_FRAME_HEADER_SIZE + declared_length);
    received_crc = (uint16_t) (((uint16_t) encoded[required - 2U] << 8U) |
                               (uint16_t) encoded[required - 1U]);
    if (received_crc != expected_crc) {
        return RTNC_FRAME_CRC_FAILURE;
    }
    if (declared_length != 0U) {
        (void) memcpy(payload, &encoded[RTNC_FRAME_HEADER_SIZE], declared_length);
    }
    *payload_length = declared_length;
    return RTNC_FRAME_OK;
}
