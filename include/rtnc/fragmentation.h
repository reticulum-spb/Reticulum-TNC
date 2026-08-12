#ifndef RTNC_FRAGMENTATION_H
#define RTNC_FRAGMENTATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    RTNC_FRAGMENT_HEADER_SIZE = 1U,
    RTNC_FRAGMENT_MAX_COUNT = 128U,
    RTNC_LINK_MIN_MTU = 500U,
    RTNC_LINK_MAX_MTU = 16256U,
};

typedef enum {
    RTNC_FRAGMENT_OK = 0,
    RTNC_FRAGMENT_INCOMPLETE,
    RTNC_FRAGMENT_INVALID_ARGUMENT,
    RTNC_FRAGMENT_INVALID_MTU,
    RTNC_FRAGMENT_INVALID_FRAME,
    RTNC_FRAGMENT_INVALID_SEQUENCE,
    RTNC_FRAGMENT_BUFFER_TOO_SMALL,
    RTNC_FRAGMENT_TIMEOUT,
} rtnc_fragment_status_t;

/** Return the number of independently protected radio frames required. */
size_t rtnc_fragment_count(size_t packet_length, size_t radio_payload_class);

/** Build fragment_index from one packet. Fragment zero is the start frame. */
rtnc_fragment_status_t rtnc_fragment_build(const uint8_t *packet, size_t packet_length, size_t radio_payload_class, size_t fragment_index, uint8_t *frame_payload, size_t frame_capacity, size_t *frame_length);

typedef struct {
    uint8_t *packet;
    size_t   mtu;
    size_t   radio_payload_class;
    size_t   length;
    uint8_t  remaining;
    uint64_t started_ms;
    uint32_t timeout_ms;
    bool     active;
} rtnc_reassembly_t;

/** Initialize caller-owned deterministic reassembly storage. */
rtnc_fragment_status_t rtnc_reassembly_init(rtnc_reassembly_t *reassembly, uint8_t *packet_buffer, size_t mtu, size_t radio_payload_class, uint32_t timeout_ms);
void                   rtnc_reassembly_reset(rtnc_reassembly_t *reassembly);

/**
 * Consume one CRC-valid radio payload in wire order. On COMPLETE, packet_length
 * describes the caller-owned packet buffer supplied at initialization.
 */
rtnc_fragment_status_t rtnc_reassembly_push(rtnc_reassembly_t *reassembly, const uint8_t *frame_payload, size_t frame_length, uint64_t now_ms, size_t *packet_length);

#endif
