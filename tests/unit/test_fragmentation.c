#include "rtnc/fragmentation.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void fill_packet(uint8_t *packet, size_t length) {
    size_t index;
    for (index = 0U; index < length; ++index) {
        packet[index] = (uint8_t) (index * 37U + index / 251U);
    }
}

static void round_trip(size_t mtu, size_t packet_length, size_t payload_class) {
    static uint8_t    input[RTNC_LINK_MAX_MTU];
    static uint8_t    output[RTNC_LINK_MAX_MTU];
    uint8_t           frame[128U];
    rtnc_reassembly_t reassembly;
    size_t            fragments;
    size_t            index;
    size_t            completed_length = 0U;
    fill_packet(input, packet_length);
    assert(rtnc_reassembly_init(&reassembly, output, mtu, payload_class, 5000U) == RTNC_FRAGMENT_OK);
    fragments = rtnc_fragment_count(packet_length, payload_class);
    assert(fragments > 0U && fragments <= RTNC_FRAGMENT_MAX_COUNT);
    for (index = 0U; index < fragments; ++index) {
        size_t                       frame_length = 0U;
        const rtnc_fragment_status_t expected =
            index + 1U == fragments ? RTNC_FRAGMENT_OK
                                    : RTNC_FRAGMENT_INCOMPLETE;
        assert(rtnc_fragment_build(input, packet_length, payload_class, index, frame, sizeof(frame), &frame_length) == RTNC_FRAGMENT_OK);
        assert(rtnc_reassembly_push(&reassembly, frame, frame_length, 1000U + index, &completed_length) == expected);
    }
    assert(completed_length == packet_length);
    assert(memcmp(input, output, packet_length) == 0);
}

int main(void) {
    uint8_t           packet[700U];
    uint8_t           output[700U];
    uint8_t           first[64U];
    uint8_t           second[64U];
    uint8_t           third[64U];
    size_t            first_length = 0U;
    size_t            second_length = 0U;
    size_t            third_length = 0U;
    size_t            packet_length = 0U;
    rtnc_reassembly_t reassembly;

    round_trip(500U, 500U, 64U);
    round_trip(4096U, 4096U, 64U);
    round_trip(9000U, 9000U, 128U);
    round_trip(16256U, 16256U, 128U);
    assert(rtnc_fragment_count(16257U, 128U) == 129U);
    assert(rtnc_fragment_count(1U, 63U) == 0U);
    assert(rtnc_reassembly_init(&reassembly, output, 499U, 64U, 100U) == RTNC_FRAGMENT_INVALID_MTU);
    assert(rtnc_reassembly_init(&reassembly, output, 8065U, 64U, 100U) == RTNC_FRAGMENT_INVALID_MTU);

    fill_packet(packet, sizeof(packet));
    assert(rtnc_fragment_build(packet, sizeof(packet), 64U, 0U, first, sizeof(first), &first_length) == RTNC_FRAGMENT_OK);
    assert(rtnc_fragment_build(packet, sizeof(packet), 64U, 1U, second, sizeof(second), &second_length) == RTNC_FRAGMENT_OK);
    assert(rtnc_fragment_build(packet, sizeof(packet), 64U, 2U, third, sizeof(third), &third_length) == RTNC_FRAGMENT_OK);
    assert(rtnc_reassembly_init(&reassembly, output, 700U, 64U, 100U) == RTNC_FRAGMENT_OK);
    assert(rtnc_reassembly_push(&reassembly, first, first_length, 0U, &packet_length) == RTNC_FRAGMENT_INCOMPLETE);
    assert(rtnc_reassembly_push(&reassembly, third, third_length, 1U, &packet_length) == RTNC_FRAGMENT_INVALID_SEQUENCE);
    assert(rtnc_reassembly_push(&reassembly, second, second_length, 2U, &packet_length) == RTNC_FRAGMENT_INVALID_SEQUENCE);
    assert(rtnc_reassembly_push(&reassembly, first, first_length, 10U, &packet_length) == RTNC_FRAGMENT_INCOMPLETE);
    assert(rtnc_reassembly_push(&reassembly, second, second_length, 111U, &packet_length) == RTNC_FRAGMENT_TIMEOUT);
    assert(rtnc_reassembly_push(&reassembly, first, 1U, 200U, &packet_length) == RTNC_FRAGMENT_INVALID_FRAME);
    (void) printf("fragmentation MTU and sequence validation passed\n");
    return 0;
}
