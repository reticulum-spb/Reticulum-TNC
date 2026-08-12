#ifndef RTNC_DECODE_QUEUE_H
#define RTNC_DECODE_QUEUE_H

#include "rtnc/modem.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    RTNC_DECODE_QUEUE_MAX_CAPACITY = 8U,
    RTNC_DECODE_QUEUE_SLOTS = RTNC_DECODE_QUEUE_MAX_CAPACITY + 1U,
};

typedef struct {
    float    samples[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    size_t   count;
    uint64_t sequence;
    uint64_t enqueued_monotonic_ns;
} rtnc_decode_job_t;

/** Fixed-size SPSC queue between streaming DSP and the frame decoder. */
typedef struct {
    int16_t  samples[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    size_t   count;
    uint64_t sequence;
    uint64_t enqueued_monotonic_ns;
} rtnc_decode_slot_t;

/** Fixed-size SPSC queue with compact signed-16-bit audio storage. */
typedef struct {
    rtnc_decode_slot_t   slots[RTNC_DECODE_QUEUE_SLOTS];
    size_t               capacity;
    size_t               slot_count;
    atomic_size_t        producer;
    atomic_size_t        consumer;
    atomic_size_t        maximum_depth;
    atomic_uint_fast64_t dropped_jobs;
} rtnc_decode_queue_t;

bool rtnc_decode_queue_init(rtnc_decode_queue_t *queue, size_t capacity);
/** Enqueue normalized audio samples in the range -1..+1. */
bool     rtnc_decode_queue_push(rtnc_decode_queue_t *queue, const float *samples, size_t count, uint64_t sequence);
bool     rtnc_decode_queue_pop(rtnc_decode_queue_t *queue, rtnc_decode_job_t *job);
size_t   rtnc_decode_queue_depth(const rtnc_decode_queue_t *queue);
size_t   rtnc_decode_queue_maximum_depth(const rtnc_decode_queue_t *queue);
uint64_t rtnc_decode_queue_dropped(const rtnc_decode_queue_t *queue);

#endif
