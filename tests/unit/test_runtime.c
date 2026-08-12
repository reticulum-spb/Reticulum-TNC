#include "rtnc/runtime.h"
#include "rtnc/kiss.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    bool         ptt;
    bool         busy;
    uint64_t     now_ms;
    unsigned int ptt_on_calls;
    unsigned int ptt_off_calls;
    unsigned int send_calls;
    unsigned int wait_calls;
    unsigned int sleep_calls;
    unsigned int fail_send_call;
    uint64_t     sent_samples;
} mock_t;

static bool set_ptt(void *context, bool enabled) {
    mock_t *mock = context;
    mock->ptt = enabled;
    if (enabled) {
        mock->ptt_on_calls += 1U;
    } else {
        mock->ptt_off_calls += 1U;
    }
    return true;
}

static bool send_audio(void *context, const int16_t *samples, size_t count) {
    mock_t *mock = context;
    assert(samples != NULL);
    assert(count > 0U);
    mock->send_calls += 1U;
    mock->sent_samples += count;
    return mock->fail_send_call == 0U ||
           mock->send_calls != mock->fail_send_call;
}

static bool wait_audio(void *context) {
    mock_t *mock = context;
    mock->wait_calls += 1U;
    return true;
}

static bool channel_busy(void *context) {
    return ((mock_t *) context)->busy;
}

static bool sleep_ms(void *context, uint16_t milliseconds) {
    mock_t *mock = context;
    mock->sleep_calls += 1U;
    mock->now_ms += milliseconds;
    return true;
}

static uint64_t now_ms(void *context) {
    return ((mock_t *) context)->now_ms;
}

static void initialize(rtnc_runtime_t *runtime, mock_t *mock) {
    rtnc_phy_profile_t profile;
    rtnc_tx_config_t   tx = {
          .lead_ms = 100U,
          .tail_ms = 100U,
          .filter_gain = 0.30F,
          .response_eq_taps = {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F},
    };
    const rtnc_runtime_backend_t backend = {
        .context = mock,
        .set_ptt = set_ptt,
        .send_audio = send_audio,
        .wait_audio = wait_audio,
        .channel_busy = channel_busy,
        .sleep_ms = sleep_ms,
        .now_ms = now_ms,
    };
    assert(rtnc_phy_profile_psk(RTNC_MODULATION_8PSK, 1000U, 1650.0F, &profile));
    assert(rtnc_runtime_init(runtime, &profile, FEC_LDPC_ROBUST, 64U, 1000U, 10000U, 2U, 2U, 500U, 150U, &tx, &backend));
}

int main(void) {
    static rtnc_runtime_t runtime;
    static uint8_t        packet[500U];
    static uint8_t        received[500U];
    mock_t                mock = { 0 };
    size_t                index;
    size_t                length = 0U;
    const size_t          fragments = rtnc_fragment_count(sizeof(packet), 64U);

    for (index = 0U; index < sizeof(packet); ++index) {
        packet[index] = (uint8_t) (index * 37U + 9U);
    }
    initialize(&runtime, &mock);
    assert(rtnc_runtime_submit_packet(&runtime, packet, sizeof(packet)) == RTNC_RUNTIME_OK);
    assert(rtnc_runtime_transmit_next(&runtime) == RTNC_RUNTIME_OK);
    assert(mock.ptt_on_calls == 1U);
    assert(mock.ptt_off_calls == 1U);
    assert(!mock.ptt);
    assert(mock.send_calls == fragments);
    assert(mock.wait_calls == 1U);
    assert(mock.sleep_calls == 3U);
    assert(runtime.stats.tx_packets == 1U);
    assert(runtime.stats.tx_frames == fragments);
    assert(runtime.stats.tx_bytes == sizeof(packet));

    /* MTU is a maximum; short KISS/Reticulum packets are valid. */
    assert(rtnc_runtime_set_kiss_parameter(&runtime, RTNC_KISS_CMD_TXDELAY, 7U));
    assert(rtnc_runtime_set_kiss_parameter(&runtime, RTNC_KISS_CMD_TXTAIL, 9U));
    assert(rtnc_runtime_set_kiss_parameter(&runtime, RTNC_KISS_CMD_P, 255U));
    {
        const uint64_t before = mock.now_ms;
        assert(rtnc_runtime_submit_packet(&runtime, packet, 32U) == RTNC_RUNTIME_OK);
        assert(rtnc_runtime_transmit_next(&runtime) == RTNC_RUNTIME_OK);
        assert(mock.now_ms - before == 70U + 90U + 150U);
    }
    assert(runtime.stats.tx_packets == 2U);
    assert(runtime.stats.tx_bytes == sizeof(packet) + 32U);

    /* A failed p-persistence trial waits for the configured slot. */
    assert(rtnc_runtime_set_kiss_parameter(&runtime, RTNC_KISS_CMD_P, 0U));
    assert(rtnc_runtime_set_kiss_parameter(&runtime, RTNC_KISS_CMD_SLOTTIME, 5U));
    assert(rtnc_runtime_submit_packet(&runtime, packet, 32U) == RTNC_RUNTIME_OK);
    assert(rtnc_runtime_transmit_next(&runtime) == RTNC_RUNTIME_BUSY);
    assert(rtnc_runtime_set_kiss_parameter(&runtime, RTNC_KISS_CMD_P, 255U));
    assert(rtnc_runtime_transmit_next(&runtime) == RTNC_RUNTIME_BUSY);
    mock.now_ms += 50U;
    assert(rtnc_runtime_transmit_next(&runtime) == RTNC_RUNTIME_OK);

    /* A playback failure must release PTT and consume the failed packet. */
    mock.fail_send_call = mock.send_calls + 2U;
    assert(rtnc_runtime_submit_packet(&runtime, packet, sizeof(packet)) == RTNC_RUNTIME_OK);
    assert(rtnc_runtime_transmit_next(&runtime) == RTNC_RUNTIME_TX_FAILED);
    assert(!mock.ptt);
    assert(mock.ptt_off_calls == 4U);
    assert(runtime.stats.tx_failures == 1U);
    assert(rtnc_packet_queue_depth(&runtime.tx_queue) == 0U);

    /* Busy-channel timeout is explicit and never keys PTT. */
    mock.fail_send_call = 0U;
    mock.busy = true;
    mock.now_ms = 1000U;
    assert(rtnc_runtime_submit_packet(&runtime, packet, sizeof(packet)) == RTNC_RUNTIME_OK);
    assert(rtnc_runtime_transmit_next(&runtime) == RTNC_RUNTIME_BUSY);
    mock.now_ms += 499U;
    assert(rtnc_runtime_transmit_next(&runtime) == RTNC_RUNTIME_BUSY);
    mock.now_ms += 1U;
    assert(rtnc_runtime_transmit_next(&runtime) == RTNC_RUNTIME_TX_TIMEOUT);
    assert(mock.ptt_on_calls == 4U);
    mock.busy = false;

    /* CRC-valid decoded fragments complete immediately on the last one. */
    for (index = 0U; index < fragments; ++index) {
        uint8_t fragment[64U];
        size_t  fragment_length = 0U;
        assert(rtnc_fragment_build(packet, sizeof(packet), 64U, index, fragment, sizeof(fragment), &fragment_length) == RTNC_FRAGMENT_OK);
        assert(rtnc_runtime_accept_fragment(&runtime, fragment, fragment_length, 2000U + index) == (index + 1U == fragments ? RTNC_RUNTIME_OK : RTNC_RUNTIME_RX_INCOMPLETE));
    }
    assert(rtnc_runtime_receive_packet(&runtime, received, sizeof(received), &length) == RTNC_RUNTIME_OK);
    assert(length == sizeof(packet));
    assert(memcmp(received, packet, sizeof(packet)) == 0);
    assert(runtime.stats.rx_packets == 1U);
    rtnc_runtime_deinit(&runtime);
    assert(!mock.ptt);
    return 0;
}
