#include "rtnc/runtime.h"
#include "rtnc/kiss.h"
#include "rtnc/tx_eq.h"

#include <math.h>
#include <string.h>

enum { RTNC_RUNTIME_TX_PEAK = 16383 };

static void set_state(rtnc_runtime_t *runtime, rtnc_runtime_state_t state) {
    atomic_store_explicit(&runtime->state, (int) state, memory_order_release);
}

static uint8_t persistence_random(rtnc_runtime_t *runtime) {
    uint32_t state = runtime->persistence_random_state;
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    runtime->persistence_random_state = state;
    return (uint8_t) state;
}

static int16_t clamp_pcm(float value) {
    if (value >= 32767.0F) {
        return 32767;
    }
    if (value <= -32768.0F) {
        return -32768;
    }
    return (int16_t) lrintf(value);
}

static bool prepare_fragment_audio(rtnc_runtime_t *runtime, const uint8_t *fragment, size_t fragment_length, size_t *sample_count) {
    int16_t raw[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    size_t  index;
    if (rtnc_modem_tx_audio(&runtime->tx_modem, fragment, fragment_length, runtime->waveform, RTNC_MODEM_MAX_AUDIO_SAMPLES, sample_count) != RTNC_MODEM_OK) {
        return false;
    }
    for (index = 0U; index < *sample_count; ++index) {
        raw[index] = clamp_pcm(runtime->waveform[index] * (float) RTNC_RUNTIME_TX_PEAK);
    }
    for (index = 0U; index < *sample_count; ++index) {
        const float filtered = rtnc_tx_eq_apply_sample(
            runtime->tx_config.response_eq_taps,
            raw,
            *sample_count,
            index
        );
        runtime->pcm[index] =
            clamp_pcm(runtime->tx_config.filter_gain * filtered);
    }
    return true;
}

static bool release_ptt(rtnc_runtime_t *runtime) {
    const bool released = runtime->backend.set_ptt(
        runtime->backend.context,
        false
    );
    set_state(runtime, RTNC_RUNTIME_RX_GUARD);
    if (runtime->rx_guard_ms > 0U &&
        !runtime->backend.sleep_ms(runtime->backend.context, runtime->rx_guard_ms)) {
        set_state(runtime, RTNC_RUNTIME_RX_IDLE);
        return false;
    }
    set_state(runtime, RTNC_RUNTIME_RX_IDLE);
    return released;
}

bool rtnc_runtime_init(rtnc_runtime_t *runtime, const rtnc_phy_profile_t *profile, fec_mode_t fec_mode, uint8_t payload_class_bytes, uint16_t mtu, uint32_t reassembly_timeout_ms, size_t tx_queue_packets, size_t rx_queue_packets, uint16_t busy_timeout_ms, uint16_t rx_guard_ms, const rtnc_tx_config_t *tx_config, const rtnc_runtime_backend_t *backend) {
    if (runtime == NULL || profile == NULL || tx_config == NULL ||
        backend == NULL || backend->set_ptt == NULL ||
        backend->send_audio == NULL || backend->wait_audio == NULL ||
        backend->sleep_ms == NULL || backend->now_ms == NULL ||
        mtu < RTNC_LINK_MIN_MTU || mtu > RTNC_LINK_MAX_MTU ||
        busy_timeout_ms == 0U || tx_config->filter_gain <= 0.0F) {
        return false;
    }
    (void) memset(runtime, 0, sizeof(*runtime));
    if (!rtnc_packet_queue_init(&runtime->tx_queue, tx_queue_packets, mtu) ||
        !rtnc_packet_queue_init(&runtime->rx_queue, rx_queue_packets, mtu) ||
        rtnc_reassembly_init(&runtime->reassembly, runtime->reassembly_storage, mtu, payload_class_bytes, reassembly_timeout_ms) !=
            RTNC_FRAGMENT_OK ||
        !rtnc_modem_init_profile(&runtime->tx_modem, fec_mode, payload_class_bytes, profile)) {
        return false;
    }
    runtime->backend = *backend;
    runtime->tx_config = *tx_config;
    runtime->mtu = mtu;
    runtime->busy_timeout_ms = busy_timeout_ms;
    runtime->rx_guard_ms = rx_guard_ms;
    atomic_init(&runtime->state, (int) RTNC_RUNTIME_RX_IDLE);
    atomic_init(&runtime->kiss_txdelay_ms, tx_config->lead_ms);
    atomic_init(&runtime->kiss_persistence, 255U);
    atomic_init(&runtime->kiss_slottime_ms, 100U);
    atomic_init(&runtime->kiss_txtail_ms, tx_config->tail_ms);
    runtime->persistence_random_state = 0x6d2b79f5U;
    runtime->initialized = true;
    return true;
}

void rtnc_runtime_deinit(rtnc_runtime_t *runtime) {
    if (runtime == NULL) {
        return;
    }
    if (runtime->initialized) {
        (void) runtime->backend.set_ptt(runtime->backend.context, false);
        rtnc_modem_deinit(&runtime->tx_modem);
    }
    runtime->initialized = false;
    set_state(runtime, RTNC_RUNTIME_RX_IDLE);
}

rtnc_runtime_status_t rtnc_runtime_submit_packet(rtnc_runtime_t *runtime, const uint8_t *packet, size_t length) {
    if (runtime == NULL || !runtime->initialized || packet == NULL ||
        length == 0U || length > runtime->mtu ||
        rtnc_fragment_count(length, runtime->tx_modem.payload_class_bytes) == 0U) {
        return RTNC_RUNTIME_INVALID_PACKET;
    }
    if (!rtnc_packet_queue_push(&runtime->tx_queue, packet, length, runtime->next_tx_sequence)) {
        return RTNC_RUNTIME_QUEUE_FULL;
    }
    runtime->next_tx_sequence += 1U;
    return RTNC_RUNTIME_OK;
}

rtnc_runtime_status_t rtnc_runtime_transmit_next(rtnc_runtime_t *runtime) {
    const rtnc_packet_slot_t *packet;
    uint8_t                   fragment[RTNC_FRAME_MAX_PAYLOAD];
    size_t                    fragment_count;
    size_t                    fragment_index;
    size_t                    packet_length;
    bool                      success = true;
    uint64_t                  now;
    if (runtime == NULL || !runtime->initialized) {
        return RTNC_RUNTIME_INVALID_PACKET;
    }
    if (!rtnc_packet_queue_peek(&runtime->tx_queue, &packet)) {
        runtime->busy_started_ms = 0U;
        return RTNC_RUNTIME_IDLE;
    }
    now = runtime->backend.now_ms(runtime->backend.context);
    if (runtime->backend.channel_busy != NULL &&
        runtime->backend.channel_busy(runtime->backend.context)) {
        set_state(runtime, RTNC_RUNTIME_TX_WAIT_BUSY);
        if (runtime->busy_started_ms == 0U) {
            runtime->busy_started_ms = now != 0U ? now : 1U;
        }
        if (now < runtime->busy_started_ms ||
            now - runtime->busy_started_ms < runtime->busy_timeout_ms) {
            return RTNC_RUNTIME_BUSY;
        }
        (void) rtnc_packet_queue_discard(&runtime->tx_queue);
        runtime->stats.tx_busy_timeouts += 1U;
        runtime->stats.tx_failures += 1U;
        runtime->busy_started_ms = 0U;
        set_state(runtime, RTNC_RUNTIME_RX_IDLE);
        return RTNC_RUNTIME_TX_TIMEOUT;
    }
    runtime->busy_started_ms = 0U;
    {
        const unsigned int persistence = atomic_load_explicit(
            &runtime->kiss_persistence,
            memory_order_acquire
        );
        if (now < runtime->next_persistence_slot_ms) {
            return RTNC_RUNTIME_BUSY;
        }
        if (persistence < 255U &&
            (unsigned int) persistence_random(runtime) > persistence) {
            runtime->next_persistence_slot_ms =
                now + atomic_load_explicit(&runtime->kiss_slottime_ms, memory_order_acquire);
            return RTNC_RUNTIME_BUSY;
        }
        runtime->next_persistence_slot_ms = 0U;
    }
    packet_length = packet->length;
    fragment_count = rtnc_fragment_count(
        packet_length,
        runtime->tx_modem.payload_class_bytes
    );
    set_state(runtime, RTNC_RUNTIME_TX_KEYING);
    if (!runtime->backend.set_ptt(runtime->backend.context, true)) {
        set_state(runtime, RTNC_RUNTIME_RX_IDLE);
        runtime->stats.tx_failures += 1U;
        (void) rtnc_packet_queue_discard(&runtime->tx_queue);
        return RTNC_RUNTIME_TX_FAILED;
    }
    success = runtime->backend.sleep_ms(runtime->backend.context, (uint16_t) atomic_load_explicit(&runtime->kiss_txdelay_ms, memory_order_acquire));
    set_state(runtime, RTNC_RUNTIME_TX_ACTIVE);
    for (fragment_index = 0U; success && fragment_index < fragment_count;
         ++fragment_index) {
        size_t fragment_length = 0U;
        size_t sample_count = 0U;
        success =
            rtnc_fragment_build(packet->bytes, packet_length, runtime->tx_modem.payload_class_bytes, fragment_index, fragment, sizeof(fragment), &fragment_length) == RTNC_FRAGMENT_OK &&
            prepare_fragment_audio(runtime, fragment, fragment_length, &sample_count) &&
            runtime->backend.send_audio(runtime->backend.context, runtime->pcm, sample_count);
        if (success) {
            runtime->stats.tx_frames += 1U;
        }
    }
    set_state(runtime, RTNC_RUNTIME_TX_TAIL);
    success = success &&
              runtime->backend.wait_audio(runtime->backend.context) &&
              runtime->backend.sleep_ms(runtime->backend.context, (uint16_t) atomic_load_explicit(&runtime->kiss_txtail_ms, memory_order_acquire));
    if (!release_ptt(runtime)) {
        success = false;
    }
    (void) rtnc_packet_queue_discard(&runtime->tx_queue);
    if (!success) {
        runtime->stats.tx_failures += 1U;
        return RTNC_RUNTIME_TX_FAILED;
    }
    runtime->stats.tx_packets += 1U;
    runtime->stats.tx_bytes += packet_length;
    return RTNC_RUNTIME_OK;
}

rtnc_runtime_status_t rtnc_runtime_accept_fragment(rtnc_runtime_t *runtime, const uint8_t *fragment, size_t length, uint64_t now_ms) {
    size_t                 packet_length = 0U;
    rtnc_fragment_status_t status;
    if (runtime == NULL || !runtime->initialized || fragment == NULL ||
        atomic_load_explicit(&runtime->state, memory_order_acquire) !=
            (int) RTNC_RUNTIME_RX_IDLE) {
        return RTNC_RUNTIME_RX_REJECTED;
    }
    status = rtnc_reassembly_push(&runtime->reassembly, fragment, length, now_ms, &packet_length);
    if (status == RTNC_FRAGMENT_INCOMPLETE) {
        return RTNC_RUNTIME_RX_INCOMPLETE;
    }
    if (status != RTNC_FRAGMENT_OK) {
        runtime->stats.rx_reassembly_rejects += 1U;
        return RTNC_RUNTIME_RX_REJECTED;
    }
    if (!rtnc_packet_queue_push(&runtime->rx_queue, runtime->reassembly_storage, packet_length, runtime->next_rx_sequence)) {
        runtime->stats.rx_reassembly_rejects += 1U;
        return RTNC_RUNTIME_QUEUE_FULL;
    }
    runtime->next_rx_sequence += 1U;
    runtime->stats.rx_packets += 1U;
    runtime->stats.rx_bytes += packet_length;
    return RTNC_RUNTIME_OK;
}

rtnc_runtime_status_t rtnc_runtime_receive_packet(rtnc_runtime_t *runtime, uint8_t *packet, size_t capacity, size_t *length) {
    const rtnc_packet_slot_t *slot;
    if (runtime == NULL || !runtime->initialized || packet == NULL ||
        length == NULL) {
        return RTNC_RUNTIME_INVALID_PACKET;
    }
    if (!rtnc_packet_queue_peek(&runtime->rx_queue, &slot)) {
        return RTNC_RUNTIME_IDLE;
    }
    if (capacity < slot->length) {
        return RTNC_RUNTIME_BUFFER_TOO_SMALL;
    }
    return rtnc_packet_queue_pop(&runtime->rx_queue, packet, capacity, length, NULL)
               ? RTNC_RUNTIME_OK
               : RTNC_RUNTIME_IDLE;
}

const rtnc_runtime_stats_t *rtnc_runtime_get_stats(
    const rtnc_runtime_t *runtime
) {
    return runtime != NULL && runtime->initialized ? &runtime->stats : NULL;
}

bool rtnc_runtime_set_kiss_parameter(rtnc_runtime_t *runtime, uint8_t command, uint8_t value) {
    atomic_uint *parameter = NULL;
    unsigned int converted = value;
    if (runtime == NULL || !runtime->initialized) {
        return false;
    }
    switch (command) {
        case RTNC_KISS_CMD_TXDELAY:
            parameter = &runtime->kiss_txdelay_ms;
            converted *= 10U;
            break;
        case RTNC_KISS_CMD_P:
            parameter = &runtime->kiss_persistence;
            break;
        case RTNC_KISS_CMD_SLOTTIME:
            parameter = &runtime->kiss_slottime_ms;
            converted *= 10U;
            break;
        case RTNC_KISS_CMD_TXTAIL:
            parameter = &runtime->kiss_txtail_ms;
            converted *= 10U;
            break;
        default:
            return false;
    }
    atomic_store_explicit(parameter, converted, memory_order_release);
    return true;
}
