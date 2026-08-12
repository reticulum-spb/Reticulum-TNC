#ifndef RTNC_PACKET_QUEUE_H
#define RTNC_PACKET_QUEUE_H

#include "rtnc/fragmentation.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    RTNC_PACKET_QUEUE_MAX_CAPACITY = 8U,
    RTNC_PACKET_QUEUE_SLOTS = RTNC_PACKET_QUEUE_MAX_CAPACITY + 1U,
};

typedef struct {
    uint8_t  bytes[RTNC_LINK_MAX_MTU];
    size_t   length;
    uint64_t sequence;
} rtnc_packet_slot_t;

/** Fixed-storage SPSC queue for complete Reticulum packets. */
typedef struct {
    rtnc_packet_slot_t   slots[RTNC_PACKET_QUEUE_SLOTS];
    size_t               mtu;
    size_t               capacity;
    size_t               slot_count;
    atomic_size_t        producer;
    atomic_size_t        consumer;
    atomic_size_t        maximum_depth;
    atomic_uint_fast64_t dropped_packets;
} rtnc_packet_queue_t;

bool     rtnc_packet_queue_init(rtnc_packet_queue_t *queue, size_t capacity, size_t mtu);
bool     rtnc_packet_queue_push(rtnc_packet_queue_t *queue, const uint8_t *packet, size_t length, uint64_t sequence);
bool     rtnc_packet_queue_peek(const rtnc_packet_queue_t *queue, const rtnc_packet_slot_t **packet);
bool     rtnc_packet_queue_pop(rtnc_packet_queue_t *queue, uint8_t *packet, size_t capacity, size_t *length, uint64_t *sequence);
bool     rtnc_packet_queue_discard(rtnc_packet_queue_t *queue);
size_t   rtnc_packet_queue_depth(const rtnc_packet_queue_t *queue);
size_t   rtnc_packet_queue_maximum_depth(const rtnc_packet_queue_t *queue);
uint64_t rtnc_packet_queue_dropped(const rtnc_packet_queue_t *queue);

#endif
