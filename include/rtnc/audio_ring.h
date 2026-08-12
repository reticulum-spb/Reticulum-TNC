#ifndef RTNC_AUDIO_RING_H
#define RTNC_AUDIO_RING_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    RTNC_AUDIO_BLOCK_SAMPLES = 480U,
    RTNC_AUDIO_RING_MAX_CAPACITY = 32U,
    RTNC_AUDIO_RING_SLOTS = RTNC_AUDIO_RING_MAX_CAPACITY + 1U,
};

typedef struct {
    int16_t samples[RTNC_AUDIO_BLOCK_SAMPLES];
    size_t  count;
} rtnc_audio_block_t;

/** Fixed-size single-producer/single-consumer audio queue. */
typedef struct {
    rtnc_audio_block_t   blocks[RTNC_AUDIO_RING_SLOTS];
    size_t               capacity;
    size_t               slot_count;
    atomic_size_t        producer;
    atomic_size_t        consumer;
    atomic_size_t        maximum_depth;
    atomic_uint_fast64_t dropped_blocks;
} rtnc_audio_ring_t;

bool rtnc_audio_ring_init(rtnc_audio_ring_t *ring, size_t capacity);

/** Enqueue one block; only the producer thread may call this function. */
bool rtnc_audio_ring_push(rtnc_audio_ring_t *ring, const int16_t *samples, size_t count);

/** Dequeue one block; only the consumer thread may call this function. */
bool rtnc_audio_ring_pop(rtnc_audio_ring_t *ring, rtnc_audio_block_t *block);

size_t   rtnc_audio_ring_depth(const rtnc_audio_ring_t *ring);
size_t   rtnc_audio_ring_maximum_depth(const rtnc_audio_ring_t *ring);
uint64_t rtnc_audio_ring_dropped(const rtnc_audio_ring_t *ring);

#endif
