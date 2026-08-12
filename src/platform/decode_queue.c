#define _POSIX_C_SOURCE 200809L

#include "rtnc/decode_queue.h"

#include <string.h>
#include <time.h>

static uint64_t monotonic_nanoseconds(void) {
    struct timespec value = { 0 };
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
        return 0U;
    }
    return (uint64_t) value.tv_sec * UINT64_C(1000000000) +
           (uint64_t) value.tv_nsec;
}

bool rtnc_decode_queue_init(rtnc_decode_queue_t *queue, size_t capacity) {
    if (queue == NULL || capacity == 0U ||
        capacity > RTNC_DECODE_QUEUE_MAX_CAPACITY) {
        return false;
    }
    queue->capacity = capacity;
    queue->slot_count = capacity + 1U;
    atomic_init(&queue->producer, 0U);
    atomic_init(&queue->consumer, 0U);
    atomic_init(&queue->maximum_depth, 0U);
    atomic_init(&queue->dropped_jobs, 0U);
    return true;
}

bool rtnc_decode_queue_push(rtnc_decode_queue_t *queue, const float *samples, size_t count, uint64_t sequence) {
    size_t producer;
    size_t next;
    size_t consumer;
    size_t index;
    if (queue == NULL || samples == NULL || count == 0U ||
        count > RTNC_MODEM_MAX_AUDIO_SAMPLES) {
        return false;
    }
    producer = atomic_load_explicit(&queue->producer, memory_order_relaxed);
    next = (producer + 1U) % queue->slot_count;
    consumer = atomic_load_explicit(&queue->consumer, memory_order_acquire);
    if (next == consumer) {
        (void) atomic_fetch_add_explicit(&queue->dropped_jobs, 1U, memory_order_relaxed);
        return false;
    }
    for (index = 0U; index < count; ++index) {
        const float bounded = samples[index] < -1.0F
                                  ? -1.0F
                                  : (samples[index] > 1.0F ? 1.0F
                                                           : samples[index]);
        const float scaled = bounded * 32767.0F;
        queue->slots[producer].samples[index] =
            (int16_t) (scaled + (scaled >= 0.0F ? 0.5F : -0.5F));
    }
    queue->slots[producer].count = count;
    queue->slots[producer].sequence = sequence;
    queue->slots[producer].enqueued_monotonic_ns = monotonic_nanoseconds();
    atomic_store_explicit(&queue->producer, next, memory_order_release);
    {
        const size_t depth = next >= consumer
                                 ? next - consumer
                                 : queue->slot_count - consumer + next;
        size_t       maximum = atomic_load_explicit(&queue->maximum_depth, memory_order_relaxed);
        while (depth > maximum &&
               !atomic_compare_exchange_weak_explicit(
                   &queue->maximum_depth,
                   &maximum,
                   depth,
                   memory_order_relaxed,
                   memory_order_relaxed
               )) {
        }
    }
    return true;
}

bool rtnc_decode_queue_pop(rtnc_decode_queue_t *queue, rtnc_decode_job_t *job) {
    size_t consumer;
    size_t producer;
    size_t index;
    if (queue == NULL || job == NULL) {
        return false;
    }
    consumer = atomic_load_explicit(&queue->consumer, memory_order_relaxed);
    producer = atomic_load_explicit(&queue->producer, memory_order_acquire);
    if (consumer == producer) {
        return false;
    }
    job->count = queue->slots[consumer].count;
    job->sequence = queue->slots[consumer].sequence;
    job->enqueued_monotonic_ns =
        queue->slots[consumer].enqueued_monotonic_ns;
    for (index = 0U; index < job->count; ++index) {
        job->samples[index] =
            (float) queue->slots[consumer].samples[index] / 32767.0F;
    }
    atomic_store_explicit(&queue->consumer, (consumer + 1U) % queue->slot_count, memory_order_release);
    return true;
}

size_t rtnc_decode_queue_depth(const rtnc_decode_queue_t *queue) {
    const size_t producer = atomic_load_explicit(&queue->producer, memory_order_acquire);
    const size_t consumer = atomic_load_explicit(&queue->consumer, memory_order_acquire);
    return producer >= consumer ? producer - consumer
                                : queue->slot_count - consumer + producer;
}

size_t rtnc_decode_queue_maximum_depth(const rtnc_decode_queue_t *queue) {
    return atomic_load_explicit(&queue->maximum_depth, memory_order_relaxed);
}

uint64_t rtnc_decode_queue_dropped(const rtnc_decode_queue_t *queue) {
    return (uint64_t) atomic_load_explicit(&queue->dropped_jobs, memory_order_relaxed);
}
