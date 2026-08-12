#include "rtnc/packet_queue.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

int main(void) {
    static rtnc_packet_queue_t queue;
    uint8_t                    first[500U];
    uint8_t                    second[500U];
    uint8_t                    output[500U];
    size_t                     length = 0U;
    uint64_t                   sequence = 0U;
    size_t                     index;
    for (index = 0U; index < sizeof(first); ++index) {
        first[index] = (uint8_t) index;
        second[index] = (uint8_t) (index + 17U);
    }
    assert(rtnc_packet_queue_init(&queue, 2U, 500U));
    assert(rtnc_packet_queue_push(&queue, first, sizeof(first), 11U));
    assert(rtnc_packet_queue_push(&queue, second, sizeof(second), 12U));
    assert(!rtnc_packet_queue_push(&queue, first, sizeof(first), 13U));
    assert(rtnc_packet_queue_depth(&queue) == 2U);
    assert(rtnc_packet_queue_maximum_depth(&queue) == 2U);
    assert(rtnc_packet_queue_dropped(&queue) == 1U);
    assert(rtnc_packet_queue_pop(&queue, output, sizeof(output), &length, &sequence));
    assert(length == sizeof(first));
    assert(sequence == 11U);
    assert(memcmp(output, first, sizeof(first)) == 0);
    assert(rtnc_packet_queue_pop(&queue, output, sizeof(output), &length, &sequence));
    assert(sequence == 12U);
    assert(memcmp(output, second, sizeof(second)) == 0);
    assert(rtnc_packet_queue_depth(&queue) == 0U);
    return 0;
}
