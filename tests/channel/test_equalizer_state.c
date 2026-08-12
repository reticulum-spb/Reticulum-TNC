#include "rtnc/modem.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum { PAYLOAD_BYTES = 64U };

static void apply_echo(const float *input, size_t count, size_t delay, float gain, float *output) {
    size_t index;
    for (index = 0U; index < count; ++index) {
        output[index] = input[index];
        if (index >= delay) {
            output[index] += gain * input[index - delay];
        }
    }
}

static rtnc_modem_status_t receive(rtnc_modem_t *modem, const float *audio, size_t sample_count, const uint8_t *expected, bool expect_success, bool expect_equalizer) {
    static rtnc_modem_workspace_t workspace;
    uint8_t                       decoded[PAYLOAD_BYTES];
    size_t                        decoded_length = 0U;
    rtnc_sync_metrics_t           metrics;
    const rtnc_modem_status_t     status =
        rtnc_modem_rx_audio(modem, audio, sample_count, decoded, sizeof(decoded), &decoded_length, &metrics, &workspace);

    assert(metrics.frame_detected);
    assert(metrics.equalizer_used == expect_equalizer);
    if (expect_success) {
        assert(status == RTNC_MODEM_OK);
        assert(decoded_length == PAYLOAD_BYTES);
        assert(memcmp(decoded, expected, PAYLOAD_BYTES) == 0);
    } else {
        assert(status != RTNC_MODEM_OK);
    }
    return status;
}

int main(void) {
    static float clean[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    static float impaired[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    uint8_t      payload[PAYLOAD_BYTES];
    rtnc_modem_t modem;
    size_t       sample_count = 0U;
    size_t       index;
    const size_t preserved_symbols = RTNC_MODEM_TRAINING_SYMBOLS + 24U;
    const size_t preserved_samples = preserved_symbols * 40U;

    for (index = 0U; index < sizeof(payload); ++index) {
        payload[index] = (uint8_t) (index * 53U + 0x17U);
    }
    assert(rtnc_modem_init_config(&modem, FEC_LDPC_ROBUST, PAYLOAD_BYTES));
    assert(rtnc_modem_tx_audio(&modem, payload, sizeof(payload), clean, RTNC_MODEM_MAX_AUDIO_SAMPLES, &sample_count) == RTNC_MODEM_OK);

    apply_echo(clean, sample_count, 20U, 0.40F, impaired);
    {
        static rtnc_modem_workspace_t fast_workspace;
        uint8_t                       decoded[PAYLOAD_BYTES];
        size_t                        decoded_length = 0U;
        rtnc_sync_metrics_t           metrics = { 0 };
        assert(rtnc_modem_rx_audio_fast(&modem, impaired, sample_count, decoded, sizeof(decoded), &decoded_length, &metrics, &fast_workspace) == RTNC_MODEM_FRAME_REJECTED);
        assert(metrics.frame_detected);
        assert(!metrics.equalizer_used);
    }
    (void) receive(&modem, impaired, sample_count, payload, true, true);
    (void) receive(&modem, clean, sample_count, payload, true, false);

    (void) memcpy(impaired, clean, sample_count * sizeof(impaired[0]));
    assert(preserved_samples < sample_count);
    (void) memset(&impaired[preserved_samples], 0, (sample_count - preserved_samples) * sizeof(impaired[0]));
    (void) receive(&modem, impaired, sample_count, payload, false, true);
    (void) receive(&modem, clean, sample_count, payload, true, false);

    rtnc_modem_deinit(&modem);
    return 0;
}
