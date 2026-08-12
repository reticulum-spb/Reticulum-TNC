#ifndef RTNC_OTA_BENCHMARK_H
#define RTNC_OTA_BENCHMARK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    RTNC_OTA_BENCHMARK_VERSION = 1U,
    RTNC_OTA_BENCHMARK_PROFILE_MAX = 20U,
    RTNC_OTA_BENCHMARK_HEADER_SIZE = 56U,
};

typedef enum {
    RTNC_OTA_BENCHMARK_ANNOUNCE = 1,
    RTNC_OTA_BENCHMARK_DATA = 2,
} rtnc_ota_benchmark_type_t;

typedef struct {
    rtnc_ota_benchmark_type_t type;
    uint32_t                  run_id;
    uint32_t                  block_id;
    uint32_t                  sequence;
    uint32_t                  packet_count;
    uint32_t                  payload_size;
    uint32_t                  seed;
    uint32_t                  guard_ms;
    char                      profile[RTNC_OTA_BENCHMARK_PROFILE_MAX + 1U];
} rtnc_ota_benchmark_message_t;

/** Encode a benchmark message and deterministic DATA body. */
bool rtnc_ota_benchmark_encode(
    const rtnc_ota_benchmark_message_t *message,
    uint8_t                            *packet,
    size_t                              capacity,
    size_t                             *packet_length
);

/** Parse the header and verify the deterministic DATA body. */
bool rtnc_ota_benchmark_decode(
    const uint8_t                *packet,
    size_t                        packet_length,
    rtnc_ota_benchmark_message_t *message
);

#endif
