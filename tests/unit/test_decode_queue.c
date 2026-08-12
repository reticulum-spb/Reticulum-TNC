#include "rtnc/decode_queue.h"

#include <assert.h>

int main(void) {
    static rtnc_decode_queue_t queue;
    static rtnc_decode_job_t   job;
    float                      first[3U] = { 0.1F, 0.2F, 0.3F };
    float                      second[2U] = { 0.4F, 0.5F };

    assert(!rtnc_decode_queue_init(&queue, 0U));
    assert(!rtnc_decode_queue_init(&queue, RTNC_DECODE_QUEUE_MAX_CAPACITY + 1U));
    assert(rtnc_decode_queue_init(&queue, 2U));
    assert(rtnc_decode_queue_depth(&queue) == 0U);
    assert(rtnc_decode_queue_push(&queue, first, 3U, 10U));
    assert(rtnc_decode_queue_push(&queue, second, 2U, 11U));
    assert(!rtnc_decode_queue_push(&queue, first, 3U, 12U));
    assert(rtnc_decode_queue_dropped(&queue) == 1U);
    assert(rtnc_decode_queue_depth(&queue) == 2U);
    assert(rtnc_decode_queue_maximum_depth(&queue) == 2U);
    assert(rtnc_decode_queue_pop(&queue, &job));
    assert(job.sequence == 10U && job.count == 3U);
    assert(job.enqueued_monotonic_ns > 0U);
    assert(job.samples[0] > 0.099F && job.samples[0] < 0.101F);
    assert(job.samples[2] > 0.299F && job.samples[2] < 0.301F);
    assert(rtnc_decode_queue_pop(&queue, &job));
    assert(job.sequence == 11U && job.count == 2U);
    assert(!rtnc_decode_queue_pop(&queue, &job));
    assert(rtnc_decode_queue_maximum_depth(&queue) == 2U);
    return 0;
}
