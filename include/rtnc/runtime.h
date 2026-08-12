#ifndef RTNC_RUNTIME_H
#define RTNC_RUNTIME_H

#include "rtnc/fragmentation.h"
#include "rtnc/modem.h"
#include "rtnc/packet_queue.h"
#include "rtnc/platform_config.h"

#include <stdbool.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    RTNC_RUNTIME_RX_IDLE = 0,
    RTNC_RUNTIME_TX_WAIT_BUSY,
    RTNC_RUNTIME_TX_KEYING,
    RTNC_RUNTIME_TX_ACTIVE,
    RTNC_RUNTIME_TX_TAIL,
    RTNC_RUNTIME_RX_GUARD,
} rtnc_runtime_state_t;

typedef enum {
    RTNC_RUNTIME_OK = 0,
    RTNC_RUNTIME_IDLE,
    RTNC_RUNTIME_BUSY,
    RTNC_RUNTIME_QUEUE_FULL,
    RTNC_RUNTIME_INVALID_PACKET,
    RTNC_RUNTIME_BUFFER_TOO_SMALL,
    RTNC_RUNTIME_TX_FAILED,
    RTNC_RUNTIME_TX_TIMEOUT,
    RTNC_RUNTIME_RX_INCOMPLETE,
    RTNC_RUNTIME_RX_REJECTED,
} rtnc_runtime_status_t;

typedef struct {
    void    *context;
    bool     (*set_ptt)(void *context, bool enabled);
    bool     (*send_audio)(void *context, const int16_t *samples, size_t count);
    bool     (*wait_audio)(void *context);
    bool     (*channel_busy)(void *context);
    bool     (*sleep_ms)(void *context, uint16_t milliseconds);
    uint64_t (*now_ms)(void *context);
} rtnc_runtime_backend_t;

typedef struct {
    uint64_t tx_packets;
    uint64_t tx_bytes;
    uint64_t tx_frames;
    uint64_t tx_failures;
    uint64_t tx_busy_timeouts;
    uint64_t rx_packets;
    uint64_t rx_bytes;
    uint64_t rx_reassembly_rejects;
} rtnc_runtime_stats_t;

typedef struct {
    rtnc_packet_queue_t    tx_queue;
    rtnc_packet_queue_t    rx_queue;
    rtnc_reassembly_t      reassembly;
    rtnc_modem_t           tx_modem;
    rtnc_runtime_backend_t backend;
    rtnc_tx_config_t       tx_config;
    rtnc_runtime_stats_t   stats;
    atomic_int             state;
    atomic_uint            kiss_txdelay_ms;
    atomic_uint            kiss_persistence;
    atomic_uint            kiss_slottime_ms;
    atomic_uint            kiss_txtail_ms;
    uint8_t                reassembly_storage[RTNC_LINK_MAX_MTU];
    float                  waveform[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    int16_t                pcm[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    uint16_t               mtu;
    uint16_t               busy_timeout_ms;
    uint16_t               rx_guard_ms;
    uint64_t               busy_started_ms;
    uint64_t               next_persistence_slot_ms;
    uint32_t               persistence_random_state;
    uint64_t               next_tx_sequence;
    uint64_t               next_rx_sequence;
    bool                   initialized;
} rtnc_runtime_t;

bool                        rtnc_runtime_init(rtnc_runtime_t *runtime, const rtnc_phy_profile_t *profile, fec_mode_t fec_mode, uint8_t payload_class_bytes, uint16_t mtu, uint32_t reassembly_timeout_ms, size_t tx_queue_packets, size_t rx_queue_packets, uint16_t busy_timeout_ms, uint16_t rx_guard_ms, const rtnc_tx_config_t *tx_config, const rtnc_runtime_backend_t *backend);
void                        rtnc_runtime_deinit(rtnc_runtime_t *runtime);
rtnc_runtime_status_t       rtnc_runtime_submit_packet(rtnc_runtime_t *runtime, const uint8_t *packet, size_t length);
rtnc_runtime_status_t       rtnc_runtime_transmit_next(rtnc_runtime_t *runtime);
rtnc_runtime_status_t       rtnc_runtime_accept_fragment(rtnc_runtime_t *runtime, const uint8_t *fragment, size_t length, uint64_t now_ms);
rtnc_runtime_status_t       rtnc_runtime_receive_packet(rtnc_runtime_t *runtime, uint8_t *packet, size_t capacity, size_t *length);
const rtnc_runtime_stats_t *rtnc_runtime_get_stats(
    const rtnc_runtime_t *runtime
);

/** Apply a supported port-zero, one-byte KISS MAC/timing command. */
bool rtnc_runtime_set_kiss_parameter(rtnc_runtime_t *runtime, uint8_t command, uint8_t value);

#endif
