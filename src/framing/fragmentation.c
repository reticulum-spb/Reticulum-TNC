#include "rtnc/fragmentation.h"

#include <string.h>

enum {
    CONTINUATION_FLAG = 0x80U,
    REMAINING_MASK = 0x7fU,
};

static bool valid_payload_class(size_t radio_payload_class) {
    return radio_payload_class == 64U || radio_payload_class == 128U;
}

size_t rtnc_fragment_count(size_t packet_length, size_t radio_payload_class) {
    size_t data_capacity;
    if (packet_length == 0U || !valid_payload_class(radio_payload_class)) {
        return 0U;
    }
    data_capacity = radio_payload_class - RTNC_FRAGMENT_HEADER_SIZE;
    return (packet_length + data_capacity - 1U) / data_capacity;
}

rtnc_fragment_status_t rtnc_fragment_build(const uint8_t *packet, size_t packet_length, size_t radio_payload_class, size_t fragment_index, uint8_t *frame_payload, size_t frame_capacity, size_t *frame_length) {
    size_t count;
    size_t data_capacity;
    size_t offset;
    size_t data_length;
    size_t remaining;
    if (packet == NULL || frame_payload == NULL || frame_length == NULL) {
        return RTNC_FRAGMENT_INVALID_ARGUMENT;
    }
    count = rtnc_fragment_count(packet_length, radio_payload_class);
    if (count == 0U || count > RTNC_FRAGMENT_MAX_COUNT ||
        fragment_index >= count) {
        return RTNC_FRAGMENT_INVALID_MTU;
    }
    data_capacity = radio_payload_class - RTNC_FRAGMENT_HEADER_SIZE;
    offset = fragment_index * data_capacity;
    data_length = packet_length - offset;
    if (data_length > data_capacity) {
        data_length = data_capacity;
    }
    if (frame_capacity < data_length + RTNC_FRAGMENT_HEADER_SIZE) {
        return RTNC_FRAGMENT_BUFFER_TOO_SMALL;
    }
    remaining = count - fragment_index - 1U;
    frame_payload[0] = (uint8_t) remaining;
    if (fragment_index != 0U) {
        frame_payload[0] |= CONTINUATION_FLAG;
    }
    (void) memcpy(&frame_payload[RTNC_FRAGMENT_HEADER_SIZE], &packet[offset], data_length);
    *frame_length = data_length + RTNC_FRAGMENT_HEADER_SIZE;
    return RTNC_FRAGMENT_OK;
}

rtnc_fragment_status_t rtnc_reassembly_init(rtnc_reassembly_t *reassembly, uint8_t *packet_buffer, size_t mtu, size_t radio_payload_class, uint32_t timeout_ms) {
    const size_t data_capacity =
        valid_payload_class(radio_payload_class)
            ? radio_payload_class - RTNC_FRAGMENT_HEADER_SIZE
            : 0U;
    if (reassembly == NULL || packet_buffer == NULL || timeout_ms == 0U) {
        return RTNC_FRAGMENT_INVALID_ARGUMENT;
    }
    if (mtu < RTNC_LINK_MIN_MTU || mtu > RTNC_LINK_MAX_MTU ||
        data_capacity == 0U ||
        mtu > RTNC_FRAGMENT_MAX_COUNT * data_capacity) {
        return RTNC_FRAGMENT_INVALID_MTU;
    }
    reassembly->packet = packet_buffer;
    reassembly->mtu = mtu;
    reassembly->radio_payload_class = radio_payload_class;
    reassembly->timeout_ms = timeout_ms;
    rtnc_reassembly_reset(reassembly);
    return RTNC_FRAGMENT_OK;
}

void rtnc_reassembly_reset(rtnc_reassembly_t *reassembly) {
    if (reassembly == NULL) {
        return;
    }
    reassembly->length = 0U;
    reassembly->remaining = 0U;
    reassembly->started_ms = 0U;
    reassembly->active = false;
}

static rtnc_fragment_status_t append_data(rtnc_reassembly_t *reassembly, const uint8_t *data, size_t length) {
    if (reassembly->length + length > reassembly->mtu) {
        rtnc_reassembly_reset(reassembly);
        return RTNC_FRAGMENT_BUFFER_TOO_SMALL;
    }
    (void) memcpy(&reassembly->packet[reassembly->length], data, length);
    reassembly->length += length;
    return RTNC_FRAGMENT_OK;
}

rtnc_fragment_status_t rtnc_reassembly_push(rtnc_reassembly_t *reassembly, const uint8_t *frame_payload, size_t frame_length, uint64_t now_ms, size_t *packet_length) {
    const bool continuation =
        frame_payload != NULL && (frame_payload[0] & CONTINUATION_FLAG) != 0U;
    const uint8_t remaining =
        frame_payload != NULL ? frame_payload[0] & REMAINING_MASK : 0U;
    rtnc_fragment_status_t status;
    if (reassembly == NULL || frame_payload == NULL || packet_length == NULL) {
        return RTNC_FRAGMENT_INVALID_ARGUMENT;
    }
    *packet_length = 0U;
    if (frame_length <= RTNC_FRAGMENT_HEADER_SIZE ||
        frame_length > reassembly->radio_payload_class) {
        return RTNC_FRAGMENT_INVALID_FRAME;
    }
    if (reassembly->active && now_ms - reassembly->started_ms >
                                  (uint64_t) reassembly->timeout_ms) {
        rtnc_reassembly_reset(reassembly);
        if (continuation) {
            return RTNC_FRAGMENT_TIMEOUT;
        }
    }
    if (!continuation) {
        rtnc_reassembly_reset(reassembly);
        reassembly->active = true;
        reassembly->remaining = remaining;
        reassembly->started_ms = now_ms;
    } else {
        if (!reassembly->active || reassembly->remaining == 0U ||
            (uint8_t) (remaining + 1U) != reassembly->remaining) {
            rtnc_reassembly_reset(reassembly);
            return RTNC_FRAGMENT_INVALID_SEQUENCE;
        }
        reassembly->remaining = remaining;
    }
    status = append_data(reassembly, &frame_payload[RTNC_FRAGMENT_HEADER_SIZE], frame_length - RTNC_FRAGMENT_HEADER_SIZE);
    if (status != RTNC_FRAGMENT_OK) {
        return status;
    }
    if (reassembly->remaining == 0U) {
        *packet_length = reassembly->length;
        reassembly->active = false;
        return RTNC_FRAGMENT_OK;
    }
    return RTNC_FRAGMENT_INCOMPLETE;
}
