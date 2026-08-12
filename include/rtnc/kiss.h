#ifndef RTNC_KISS_H
#define RTNC_KISS_H

#include "rtnc/fragmentation.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    RTNC_KISS_FEND = 0xc0U,
    RTNC_KISS_FESC = 0xdbU,
    RTNC_KISS_TFEND = 0xdcU,
    RTNC_KISS_TFESC = 0xddU,
    RTNC_KISS_DATA_COMMAND = 0x00U,
    RTNC_KISS_CMD_TXDELAY = 0x01U,
    RTNC_KISS_CMD_P = 0x02U,
    RTNC_KISS_CMD_SLOTTIME = 0x03U,
    RTNC_KISS_CMD_TXTAIL = 0x04U,
    RTNC_KISS_MAX_ENCODED = 2U * RTNC_LINK_MAX_MTU + 4U,
};

typedef struct {
    uint8_t frame[RTNC_LINK_MAX_MTU + 1U];
    size_t  length;
    bool    active;
    bool    escaped;
    bool    rejected;
} rtnc_kiss_parser_t;

typedef struct {
    uint8_t        port;
    uint8_t        command;
    const uint8_t *data;
    size_t         length;
} rtnc_kiss_frame_t;

void rtnc_kiss_parser_init(rtnc_kiss_parser_t *parser);

/**
 * Feed one TCP stream byte. Returns true only for a complete KISS data frame;
 * packet points into parser-owned storage until the next call.
 */
bool rtnc_kiss_parser_push(rtnc_kiss_parser_t *parser, uint8_t byte, const uint8_t **packet, size_t *packet_length);

/** Feed one byte and return any complete KISS frame, including commands. */
bool rtnc_kiss_parser_push_frame(rtnc_kiss_parser_t *parser, uint8_t byte, rtnc_kiss_frame_t *frame);

/** Encode one port-zero KISS data frame into caller-owned storage. */
bool rtnc_kiss_encode(const uint8_t *packet, size_t packet_length, uint8_t *encoded, size_t capacity, size_t *encoded_length);

#endif
