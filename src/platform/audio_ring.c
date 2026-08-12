#include "rtnc/audio_ring.h"

#include <string.h>

bool rtnc_audio_ring_init(rtnc_audio_ring_t *ring, size_t capacity) {
    if (ring == NULL || capacity == 0U ||
        capacity > RTNC_AUDIO_RING_MAX_CAPACITY) {
        return false;
    }
    ring->capacity = capacity;
    ring->slot_count = capacity + 1U;
    (void) memset(ring->blocks, 0, sizeof(ring->blocks));
    atomic_init(&ring->producer, 0U);
    atomic_init(&ring->consumer, 0U);
    atomic_init(&ring->maximum_depth, 0U);
    atomic_init(&ring->dropped_blocks, 0U);
    return true;
}

bool rtnc_audio_ring_push(rtnc_audio_ring_t *ring, const int16_t *samples, size_t count) {
    size_t producer;
    size_t next;
    if (ring == NULL || samples == NULL || count == 0U ||
        count > RTNC_AUDIO_BLOCK_SAMPLES) {
        return false;
    }
    producer = atomic_load_explicit(&ring->producer, memory_order_relaxed);
    next = (producer + 1U) % ring->slot_count;
    {
        const size_t consumer =
            atomic_load_explicit(&ring->consumer, memory_order_acquire);
        if (next == consumer) {
            (void) atomic_fetch_add_explicit(&ring->dropped_blocks, 1U, memory_order_relaxed);
            return false;
        }
        (void) memcpy(ring->blocks[producer].samples, samples, count * sizeof(samples[0]));
        ring->blocks[producer].count = count;
        atomic_store_explicit(&ring->producer, next, memory_order_release);
        {
            const size_t depth = next >= consumer
                                     ? next - consumer
                                     : ring->slot_count - consumer + next;
            size_t       maximum = atomic_load_explicit(&ring->maximum_depth, memory_order_relaxed);
            while (depth > maximum &&
                   !atomic_compare_exchange_weak_explicit(
                       &ring->maximum_depth,
                       &maximum,
                       depth,
                       memory_order_relaxed,
                       memory_order_relaxed
                   )) {
            }
        }
    }
    return true;
}

bool rtnc_audio_ring_pop(rtnc_audio_ring_t *ring, rtnc_audio_block_t *block) {
    size_t consumer;
    if (ring == NULL || block == NULL) {
        return false;
    }
    consumer = atomic_load_explicit(&ring->consumer, memory_order_relaxed);
    if (consumer ==
        atomic_load_explicit(&ring->producer, memory_order_acquire)) {
        return false;
    }
    *block = ring->blocks[consumer];
    atomic_store_explicit(&ring->consumer, (consumer + 1U) % ring->slot_count, memory_order_release);
    return true;
}

size_t rtnc_audio_ring_depth(const rtnc_audio_ring_t *ring) {
    const size_t producer =
        atomic_load_explicit(&ring->producer, memory_order_acquire);
    const size_t consumer =
        atomic_load_explicit(&ring->consumer, memory_order_acquire);
    return producer >= consumer ? producer - consumer
                                : ring->slot_count - consumer + producer;
}

size_t rtnc_audio_ring_maximum_depth(const rtnc_audio_ring_t *ring) {
    return atomic_load_explicit(&ring->maximum_depth, memory_order_relaxed);
}

uint64_t rtnc_audio_ring_dropped(const rtnc_audio_ring_t *ring) {
    return (uint64_t) atomic_load_explicit(&ring->dropped_blocks, memory_order_relaxed);
}
