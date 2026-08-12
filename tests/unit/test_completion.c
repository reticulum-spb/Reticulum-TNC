#include "rtnc/completion.h"
#include "rtnc/fragmentation.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    rtnc_completion_coordinator_t coordinator;
    rtnc_completion_t             completion;
    uint8_t                       payload0[] = { 0U, 1U, 2U };
    uint8_t                       payload1[] = { 3U, 4U };

    assert(rtnc_completion_init(&coordinator, 4U, 10U, 7U));
    assert(rtnc_completion_submit(&coordinator, 11U, 7U, true, payload1, sizeof(payload1)));
    assert(!rtnc_completion_pop(&coordinator, &completion));
    assert(rtnc_completion_submit(&coordinator, 10U, 7U, true, payload0, sizeof(payload0)));
    assert(rtnc_completion_pop(&coordinator, &completion));
    assert(completion.job_id == 10U && completion.success && completion.payload_length == sizeof(payload0) && memcmp(completion.payload, payload0, sizeof(payload0)) == 0);
    assert(rtnc_completion_pop(&coordinator, &completion));
    assert(completion.job_id == 11U && completion.success);
    assert(rtnc_completion_submit(&coordinator, 12U, 7U, false, NULL, 0U));
    assert(rtnc_completion_pop(&coordinator, &completion));
    assert(completion.job_id == 12U && !completion.success);
    assert(!rtnc_completion_submit(&coordinator, 12U, 7U, false, NULL, 0U));
    assert(!rtnc_completion_submit(&coordinator, 13U, 8U, false, NULL, 0U));
    assert(rtnc_completion_rejected(&coordinator) == 1U);
    rtnc_completion_reset(&coordinator, 20U, 8U);
    assert(!rtnc_completion_submit(&coordinator, 20U, 7U, false, NULL, 0U));
    assert(rtnc_completion_submit(&coordinator, 20U, 8U, false, NULL, 0U));
    assert(rtnc_completion_pop(&coordinator, &completion));
    assert(completion.job_id == 20U && completion.generation == 8U);

    {
        uint8_t           packet[500U];
        uint8_t           output[500U];
        uint8_t           fragments[8U][64U];
        size_t            lengths[8U];
        rtnc_reassembly_t reassembly;
        size_t            completed_length = 0U;
        size_t            index;
        for (index = 0U; index < sizeof(packet); ++index) {
            packet[index] = (uint8_t) (index * 29U);
        }
        for (index = 0U; index < 8U; ++index) {
            assert(rtnc_fragment_build(packet, sizeof(packet), 64U, index, fragments[index], sizeof(fragments[index]), &lengths[index]) == RTNC_FRAGMENT_OK);
        }
        assert(rtnc_completion_init(&coordinator, 10U, 0U, 1U));
        assert(rtnc_reassembly_init(&reassembly, output, sizeof(output), 64U, 1000U) == RTNC_FRAGMENT_OK);
        for (index = 0U; index < 8U; index += 2U) {
            assert(rtnc_completion_submit(&coordinator, index + 1U, 1U, true, fragments[index + 1U], lengths[index + 1U]));
            assert(rtnc_completion_submit(&coordinator, index, 1U, true, fragments[index], lengths[index]));
        }
        for (index = 0U; index < 8U; ++index) {
            const rtnc_fragment_status_t expected =
                index == 7U ? RTNC_FRAGMENT_OK : RTNC_FRAGMENT_INCOMPLETE;
            assert(rtnc_completion_pop(&coordinator, &completion));
            assert(completion.job_id == index && completion.success);
            assert(rtnc_reassembly_push(&reassembly, completion.payload, completion.payload_length, 100U + index, &completed_length) == expected);
        }
        assert(completed_length == sizeof(packet));
        assert(memcmp(packet, output, sizeof(packet)) == 0);
    }
    (void) printf("two-producer completion ordering passed\n");
    return 0;
}
