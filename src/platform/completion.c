#include "rtnc/completion.h"

#include <string.h>

enum {
    SLOT_EMPTY = 0U,
    SLOT_WRITING = 1U,
    SLOT_READY = 2U,
};

bool rtnc_completion_init(rtnc_completion_coordinator_t *coordinator, size_t capacity, uint64_t first_job_id, uint32_t generation) {
    size_t index;
    if (coordinator == NULL || capacity == 0U ||
        capacity > RTNC_COMPLETION_MAX_CAPACITY) {
        return false;
    }
    coordinator->capacity = capacity;
    for (index = 0U; index < RTNC_COMPLETION_MAX_CAPACITY; ++index) {
        atomic_init(&coordinator->slots[index].state, SLOT_EMPTY);
    }
    atomic_init(&coordinator->next_job_id, first_job_id);
    atomic_init(&coordinator->generation, generation);
    atomic_init(&coordinator->rejected, 0U);
    return true;
}

void rtnc_completion_reset(rtnc_completion_coordinator_t *coordinator, uint64_t first_job_id, uint32_t generation) {
    size_t index;
    if (coordinator == NULL) {
        return;
    }
    for (index = 0U; index < coordinator->capacity; ++index) {
        atomic_store_explicit(&coordinator->slots[index].state, SLOT_EMPTY, memory_order_relaxed);
    }
    atomic_store_explicit(&coordinator->next_job_id, first_job_id, memory_order_release);
    atomic_store_explicit(&coordinator->generation, generation, memory_order_release);
}

bool rtnc_completion_submit(rtnc_completion_coordinator_t *coordinator, uint64_t job_id, uint32_t generation, bool success, const uint8_t *payload, size_t payload_length) {
    uint64_t                next;
    size_t                  index;
    unsigned char           expected = SLOT_EMPTY;
    rtnc_completion_slot_t *slot;
    if (coordinator == NULL || (!success && payload_length != 0U) ||
        (success && (payload == NULL || payload_length == 0U ||
                     payload_length > RTNC_FRAME_MAX_PAYLOAD)) ||
        generation != atomic_load_explicit(&coordinator->generation, memory_order_acquire)) {
        return false;
    }
    next = atomic_load_explicit(&coordinator->next_job_id, memory_order_acquire);
    if (job_id < next || job_id - next >= coordinator->capacity) {
        (void) atomic_fetch_add_explicit(&coordinator->rejected, 1U, memory_order_relaxed);
        return false;
    }
    index = (size_t) (job_id % coordinator->capacity);
    slot = &coordinator->slots[index];
    if (!atomic_compare_exchange_strong_explicit(
            &slot->state,
            &expected,
            SLOT_WRITING,
            memory_order_acq_rel,
            memory_order_acquire
        )) {
        (void) atomic_fetch_add_explicit(&coordinator->rejected, 1U, memory_order_relaxed);
        return false;
    }
    slot->completion.job_id = job_id;
    slot->completion.generation = generation;
    slot->completion.success = success;
    slot->completion.payload_length = payload_length;
    if (success) {
        (void) memcpy(slot->completion.payload, payload, payload_length);
    }
    atomic_store_explicit(&slot->state, SLOT_READY, memory_order_release);
    return true;
}

bool rtnc_completion_pop(rtnc_completion_coordinator_t *coordinator, rtnc_completion_t *completion) {
    const uint64_t next =
        coordinator != NULL
            ? atomic_load_explicit(&coordinator->next_job_id, memory_order_relaxed)
            : 0U;
    rtnc_completion_slot_t *slot;
    if (coordinator == NULL || completion == NULL) {
        return false;
    }
    slot = &coordinator->slots[(size_t) (next % coordinator->capacity)];
    if (atomic_load_explicit(&slot->state, memory_order_acquire) != SLOT_READY ||
        slot->completion.job_id != next) {
        return false;
    }
    *completion = slot->completion;
    atomic_store_explicit(&slot->state, SLOT_EMPTY, memory_order_release);
    atomic_store_explicit(&coordinator->next_job_id, next + 1U, memory_order_release);
    return true;
}

uint64_t rtnc_completion_rejected(
    const rtnc_completion_coordinator_t *coordinator
) {
    return coordinator != NULL
               ? (uint64_t) atomic_load_explicit(&coordinator->rejected, memory_order_relaxed)
               : 0U;
}
