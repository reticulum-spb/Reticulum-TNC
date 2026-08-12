#include "rtnc/ota_benchmark.h"

#include <assert.h>
#include <string.h>

int main(void) {
    const rtnc_ota_benchmark_message_t source = {
        .type = RTNC_OTA_BENCHMARK_DATA,
        .run_id = 0x12345678U,
        .block_id = 7U,
        .sequence = 3U,
        .packet_count = 20U,
        .payload_size = 500U,
        .seed = 0xa55a31U,
        .guard_ms = 1500U,
        .profile = "turbo",
    };
    rtnc_ota_benchmark_message_t decoded;
    uint8_t                      packet[500U];
    size_t                       length = 0U;
    assert(rtnc_ota_benchmark_encode(&source, packet, sizeof(packet), &length));
    assert(length == sizeof(packet));
    assert(rtnc_ota_benchmark_decode(packet, length, &decoded));
    assert(decoded.type == source.type);
    assert(decoded.run_id == source.run_id);
    assert(decoded.block_id == source.block_id);
    assert(decoded.sequence == source.sequence);
    assert(decoded.packet_count == source.packet_count);
    assert(decoded.payload_size == source.payload_size);
    assert(strcmp(decoded.profile, source.profile) == 0);
    packet[100] ^= 1U;
    assert(!rtnc_ota_benchmark_decode(packet, length, &decoded));
    return 0;
}
