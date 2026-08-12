#include "rtnc/packet_queue.h"

#include <string.h>

static size_t queue_depth(size_t producer, size_t consumer, size_t slots) {
    return producer >= consumer ? producer - consumer
                                : slots - consumer + producer;
}

bool rtnc_packet_queue_init(rtnc_packet_queue_t *queue, size_t capacity, size_t mtu) {
    if (queue == NULL || capacity == 0U ||
        capacity > RTNC_PACKET_QUEUE_MAX_CAPACITY ||
        mtu < RTNC_LINK_MIN_MTU || mtu > RTNC_LINK_MAX_MTU) {
        return false;
    }
    (void) memset(queue, 0, sizeof(*queue));
    queue->mtu = mtu;
    queue->capacity = capacity;
    queue->slot_count = capacity + 1U;
    atomic_init(&queue->producer, 0U);
    atomic_init(&queue->consumer, 0U);
    atomic_init(&queue->maximum_depth, 0U);
    atomic_init(&queue->dropped_packets, 0U);
    return true;
}

bool rtnc_packet_queue_push(rtnc_packet_queue_t *queue, const uint8_t *packet, size_t length, uint64_t sequence) {
    size_t producer;
    size_t consumer;
    size_t next;
    size_t depth;
    size_t maximum;
    if (queue == NULL || packet == NULL || length == 0U ||
        length > queue->mtu || queue->slot_count < 2U) {
        return false;
    }
    producer = atomic_load_explicit(&queue->producer, memory_order_relaxed);
    consumer = atomic_load_explicit(&queue->consumer, memory_order_acquire);
    next = (producer + 1U) % queue->slot_count;
    if (next == consumer) {
        (void) atomic_fetch_add_explicit(&queue->dropped_packets, 1U, memory_order_relaxed);
        return false;
    }
    (void) memcpy(queue->slots[producer].bytes, packet, length);
    queue->slots[producer].length = length;
    queue->slots[producer].sequence = sequence;
    atomic_store_explicit(&queue->producer, next, memory_order_release);
    depth = queue_depth(next, consumer, queue->slot_count);
    maximum = atomic_load_explicit(&queue->maximum_depth, memory_order_relaxed);
    while (depth > maximum &&
           !atomic_compare_exchange_weak_explicit(
               &queue->maximum_depth,
               &maximum,
               depth,
               memory_order_relaxed,
               memory_order_relaxed
           )) {
    }
    return true;
}

bool rtnc_packet_queue_peek(const rtnc_packet_queue_t *queue, const rtnc_packet_slot_t **packet) {
    size_t consumer;
    size_t producer;
    if (queue == NULL || packet == NULL || queue->slot_count < 2U) {
        return false;
    }
    consumer = atomic_load_explicit(&queue->consumer, memory_order_relaxed);
    producer = atomic_load_explicit(&queue->producer, memory_order_acquire);
    if (consumer == producer) {
        return false;
    }
    *packet = &queue->slots[consumer];
    return true;
}

bool rtnc_packet_queue_discard(rtnc_packet_queue_t *queue) {
    size_t       consumer;
    const size_t producer =
        queue != NULL
            ? atomic_load_explicit(&queue->producer, memory_order_acquire)
            : 0U;
    if (queue == NULL || queue->slot_count < 2U) {
        return false;
    }
    consumer = atomic_load_explicit(&queue->consumer, memory_order_relaxed);
    if (consumer == producer) {
        return false;
    }
    atomic_store_explicit(&queue->consumer, (consumer + 1U) % queue->slot_count, memory_order_release);
    return true;
}

bool rtnc_packet_queue_pop(rtnc_packet_queue_t *queue, uint8_t *packet, size_t capacity, size_t *length, uint64_t *sequence) {
    const rtnc_packet_slot_t *slot;
    if (queue == NULL || packet == NULL || length == NULL ||
        !rtnc_packet_queue_peek(queue, &slot) || capacity < slot->length) {
        return false;
    }
    (void) memcpy(packet, slot->bytes, slot->length);
    *length = slot->length;
    if (sequence != NULL) {
        *sequence = slot->sequence;
    }
    return rtnc_packet_queue_discard(queue);
}

size_t rtnc_packet_queue_depth(const rtnc_packet_queue_t *queue) {
    size_t producer;
    size_t consumer;
    if (queue == NULL || queue->slot_count < 2U) {
        return 0U;
    }
    producer = atomic_load_explicit(&queue->producer, memory_order_acquire);
    consumer = atomic_load_explicit(&queue->consumer, memory_order_acquire);
    return queue_depth(producer, consumer, queue->slot_count);
}

size_t rtnc_packet_queue_maximum_depth(const rtnc_packet_queue_t *queue) {
    return queue != NULL
               ? atomic_load_explicit(&queue->maximum_depth, memory_order_relaxed)
               : 0U;
}

uint64_t rtnc_packet_queue_dropped(const rtnc_packet_queue_t *queue) {
    return queue != NULL
               ? (uint64_t) atomic_load_explicit(&queue->dropped_packets, memory_order_relaxed)
               : 0U;
}
