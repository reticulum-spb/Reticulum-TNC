#include "rtnc/ota_benchmark.h"

#include <string.h>

static const uint8_t benchmark_magic[4] = { 'R', 'T', 'O', 'B' };

static void store_u32(uint8_t *output, uint32_t value) {
    output[0] = (uint8_t) (value >> 24U);
    output[1] = (uint8_t) (value >> 16U);
    output[2] = (uint8_t) (value >> 8U);
    output[3] = (uint8_t) value;
}

static uint32_t load_u32(const uint8_t *input) {
    return ((uint32_t) input[0] << 24U) | ((uint32_t) input[1] << 16U) |
           ((uint32_t) input[2] << 8U) | (uint32_t) input[3];
}

static uint8_t body_byte(uint32_t seed, uint32_t sequence, size_t index) {
    uint32_t value = seed ^ (sequence * UINT32_C(0x9e3779b9)) ^
                     (uint32_t) index * UINT32_C(0x85ebca6b);
    value ^= value >> 16U;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15U;
    return (uint8_t) value;
}

bool rtnc_ota_benchmark_encode(const rtnc_ota_benchmark_message_t *message, uint8_t *packet, size_t capacity, size_t *packet_length) {
    size_t profile_length;
    size_t index;
    size_t required;
    if (message == NULL || packet == NULL || packet_length == NULL ||
        message->type < RTNC_OTA_BENCHMARK_ANNOUNCE ||
        message->type > RTNC_OTA_BENCHMARK_END) {
        return false;
    }
    profile_length = strnlen(message->profile, sizeof(message->profile));
    if (profile_length == 0U || profile_length > RTNC_OTA_BENCHMARK_PROFILE_MAX) {
        return false;
    }
    required = message->type == RTNC_OTA_BENCHMARK_DATA
                   ? (size_t) message->payload_size
                   : RTNC_OTA_BENCHMARK_HEADER_SIZE;
    if (required < RTNC_OTA_BENCHMARK_HEADER_SIZE || required > capacity) {
        return false;
    }
    (void) memset(packet, 0, required);
    (void) memcpy(packet, benchmark_magic, sizeof(benchmark_magic));
    packet[4] = RTNC_OTA_BENCHMARK_VERSION;
    packet[5] = (uint8_t) message->type;
    packet[6] = (uint8_t) profile_length;
    store_u32(&packet[8], message->run_id);
    store_u32(&packet[12], message->block_id);
    store_u32(&packet[16], message->sequence);
    store_u32(&packet[20], message->packet_count);
    store_u32(&packet[24], message->payload_size);
    store_u32(&packet[28], message->seed);
    store_u32(&packet[32], message->guard_ms);
    (void) memcpy(&packet[36], message->profile, profile_length);
    for (index = RTNC_OTA_BENCHMARK_HEADER_SIZE; index < required; ++index) {
        packet[index] = body_byte(message->seed, message->sequence, index);
    }
    *packet_length = required;
    return true;
}

bool rtnc_ota_benchmark_decode(const uint8_t *packet, size_t packet_length, rtnc_ota_benchmark_message_t *message) {
    size_t profile_length;
    size_t index;
    if (packet == NULL || message == NULL ||
        packet_length < RTNC_OTA_BENCHMARK_HEADER_SIZE ||
        memcmp(packet, benchmark_magic, sizeof(benchmark_magic)) != 0 ||
        packet[4] != RTNC_OTA_BENCHMARK_VERSION ||
        packet[5] < RTNC_OTA_BENCHMARK_ANNOUNCE ||
        packet[5] > RTNC_OTA_BENCHMARK_END) {
        return false;
    }
    profile_length = packet[6];
    if (profile_length == 0U || profile_length > RTNC_OTA_BENCHMARK_PROFILE_MAX) {
        return false;
    }
    (void) memset(message, 0, sizeof(*message));
    message->type = (rtnc_ota_benchmark_type_t) packet[5];
    message->run_id = load_u32(&packet[8]);
    message->block_id = load_u32(&packet[12]);
    message->sequence = load_u32(&packet[16]);
    message->packet_count = load_u32(&packet[20]);
    message->payload_size = load_u32(&packet[24]);
    message->seed = load_u32(&packet[28]);
    message->guard_ms = load_u32(&packet[32]);
    (void) memcpy(message->profile, &packet[36], profile_length);
    if (message->type == RTNC_OTA_BENCHMARK_DATA) {
        if (message->payload_size != packet_length ||
            message->sequence >= message->packet_count) {
            return false;
        }
        for (index = RTNC_OTA_BENCHMARK_HEADER_SIZE; index < packet_length;
             ++index) {
            if (packet[index] != body_byte(message->seed, message->sequence, index)) {
                return false;
            }
        }
    }
    return true;
}
