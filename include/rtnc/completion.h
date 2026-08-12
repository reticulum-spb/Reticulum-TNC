#ifndef RTNC_COMPLETION_H
#define RTNC_COMPLETION_H

#include "rtnc/frame.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    RTNC_COMPLETION_MAX_CAPACITY = 16U,
};

typedef struct {
    uint64_t job_id;
    uint32_t generation;
    bool     success;
    uint8_t  payload[RTNC_FRAME_MAX_PAYLOAD];
    size_t   payload_length;
} rtnc_completion_t;

typedef struct {
    atomic_uchar      state;
    rtnc_completion_t completion;
} rtnc_completion_slot_t;

/** Bounded two-producer/single-consumer terminal-result reorder window. */
typedef struct {
    rtnc_completion_slot_t slots[RTNC_COMPLETION_MAX_CAPACITY];
    size_t                 capacity;
    atomic_uint_fast64_t   next_job_id;
    atomic_uint            generation;
    atomic_uint_fast64_t   rejected;
} rtnc_completion_coordinator_t;

bool rtnc_completion_init(rtnc_completion_coordinator_t *coordinator, size_t capacity, uint64_t first_job_id, uint32_t generation);

/** Reset a quiesced coordinator and invalidate results from the old epoch. */
void rtnc_completion_reset(rtnc_completion_coordinator_t *coordinator, uint64_t first_job_id, uint32_t generation);

/** Submit exactly one terminal result for a job from either decoder worker. */
bool rtnc_completion_submit(rtnc_completion_coordinator_t *coordinator, uint64_t job_id, uint32_t generation, bool success, const uint8_t *payload, size_t payload_length);

/** Pop only the next job in detection order. Failures are returned explicitly. */
bool rtnc_completion_pop(rtnc_completion_coordinator_t *coordinator, rtnc_completion_t *completion);

uint64_t rtnc_completion_rejected(
    const rtnc_completion_coordinator_t *coordinator
);

#endif
