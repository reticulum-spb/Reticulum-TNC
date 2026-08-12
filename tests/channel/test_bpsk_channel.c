#include "rtnc/modem.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void run_case(float phase, float cfo_hz, size_t prefix) {
    static float                  audio[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    static rtnc_modem_workspace_t workspace;
    rtnc_phy_profile_t            profile;
    rtnc_modem_t                  modem;
    rtnc_sync_metrics_t           metrics = { 0 };
    uint8_t                       payload[64U];
    uint8_t                       decoded[64U];
    size_t                        sample_count = 0U;
    size_t                        decoded_length = 0U;
    size_t                        index;
    (void) memset(audio, 0, sizeof(audio));
    for (index = 0U; index < sizeof(payload); ++index) {
        payload[index] = (uint8_t) (index * 29U + 0x43U);
    }
    assert(rtnc_phy_profile_psk(
        RTNC_MODULATION_BPSK,
        1200U,
        1650.0F,
        &profile
    ));
    assert(rtnc_modem_init_profile(
        &modem,
        FEC_LDPC_ROBUST,
        sizeof(payload),
        &profile
    ));
    assert(rtnc_carrier_set_tx_phase(&modem.carrier, phase));
    assert(rtnc_carrier_set_tx_frequency_offset(&modem.carrier, cfo_hz));
    assert(rtnc_modem_tx_audio(&modem, payload, sizeof(payload), &audio[prefix], RTNC_MODEM_MAX_AUDIO_SAMPLES - prefix, &sample_count) == RTNC_MODEM_OK);
    sample_count += prefix;
    assert(rtnc_modem_rx_audio(&modem, audio, sample_count, decoded, sizeof(decoded), &decoded_length, &metrics, &workspace) == RTNC_MODEM_OK);
    assert(decoded_length == sizeof(payload));
    assert(memcmp(decoded, payload, sizeof(payload)) == 0);
    assert(fabsf(metrics.carrier_offset_hz - cfo_hz) < 0.35F);
    assert(fabsf(metrics.timing_symbols - (float) prefix / 40.0F) <= 0.026F);
    rtnc_modem_deinit(&modem);
}

int main(void) {
    run_case(2.7F, 0.0F, 0U);
    run_case(-2.2F, 8.0F, 0U);
    run_case(0.8F, -8.0F, 73U);
    return 0;
}
