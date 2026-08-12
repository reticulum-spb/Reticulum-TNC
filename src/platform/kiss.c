#include "rtnc/kiss.h"

#include <string.h>

void rtnc_kiss_parser_init(rtnc_kiss_parser_t *parser) {
    if (parser != NULL) {
        (void) memset(parser, 0, sizeof(*parser));
    }
}

bool rtnc_kiss_parser_push_frame(rtnc_kiss_parser_t *parser, uint8_t byte, rtnc_kiss_frame_t *frame) {
    if (parser == NULL || frame == NULL) {
        return false;
    }
    (void) memset(frame, 0, sizeof(*frame));
    if (byte == RTNC_KISS_FEND) {
        const bool complete = parser->active && !parser->rejected &&
                              parser->length > 0U;
        if (complete) {
            frame->port = parser->frame[0] >> 4U;
            frame->command = parser->frame[0] & 0x0fU;
            frame->data = &parser->frame[1];
            frame->length = parser->length - 1U;
        }
        parser->active = true;
        parser->escaped = false;
        parser->rejected = false;
        parser->length = 0U;
        return complete;
    }
    if (!parser->active || parser->rejected) {
        return false;
    }
    if (parser->escaped) {
        parser->escaped = false;
        if (byte == RTNC_KISS_TFEND) {
            byte = RTNC_KISS_FEND;
        } else if (byte == RTNC_KISS_TFESC) {
            byte = RTNC_KISS_FESC;
        } else {
            parser->rejected = true;
            return false;
        }
    } else if (byte == RTNC_KISS_FESC) {
        parser->escaped = true;
        return false;
    }
    if (parser->length >= sizeof(parser->frame)) {
        parser->rejected = true;
        return false;
    }
    parser->frame[parser->length++] = byte;
    return false;
}

bool rtnc_kiss_parser_push(rtnc_kiss_parser_t *parser, uint8_t byte, const uint8_t **packet, size_t *packet_length) {
    rtnc_kiss_frame_t frame;
    if (packet == NULL || packet_length == NULL) {
        return false;
    }
    *packet = NULL;
    *packet_length = 0U;
    if (!rtnc_kiss_parser_push_frame(parser, byte, &frame) ||
        frame.port != 0U || frame.command != RTNC_KISS_DATA_COMMAND ||
        frame.length == 0U) {
        return false;
    }
    *packet = frame.data;
    *packet_length = frame.length;
    return true;
}

bool rtnc_kiss_encode(const uint8_t *packet, size_t packet_length, uint8_t *encoded, size_t capacity, size_t *encoded_length) {
    size_t input;
    size_t output = 0U;
    if (packet == NULL || encoded == NULL || encoded_length == NULL ||
        packet_length == 0U || packet_length > RTNC_LINK_MAX_MTU ||
        capacity < 3U) {
        return false;
    }
    encoded[output++] = RTNC_KISS_FEND;
    encoded[output++] = RTNC_KISS_DATA_COMMAND;
    for (input = 0U; input < packet_length; ++input) {
        if (packet[input] == RTNC_KISS_FEND ||
            packet[input] == RTNC_KISS_FESC) {
            if (output + 2U > capacity) {
                return false;
            }
            encoded[output++] = RTNC_KISS_FESC;
            encoded[output++] = packet[input] == RTNC_KISS_FEND
                                    ? RTNC_KISS_TFEND
                                    : RTNC_KISS_TFESC;
        } else {
            if (output + 1U > capacity) {
                return false;
            }
            encoded[output++] = packet[input];
        }
    }
    if (output >= capacity) {
        return false;
    }
    encoded[output++] = RTNC_KISS_FEND;
    *encoded_length = output;
    return true;
}
