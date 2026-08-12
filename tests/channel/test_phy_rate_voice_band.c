#include "rtnc/modem.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void voice_band(const float *input, size_t count, float *output) {
    const float dt = 1.0F / 48000.0F;
    const float hp_rc = 1.0F / (2.0F * (float) M_PI * 300.0F);
    const float lp_rc = 1.0F / (2.0F * (float) M_PI * 3000.0F);
    const float hp_alpha = hp_rc / (hp_rc + dt);
    const float lp_alpha = dt / (lp_rc + dt);
    float       previous = 0.0F;
    float       hp_state = 0.0F;
    float       lp_state = 0.0F;
    size_t      index;
    for (index = 0U; index < count; ++index) {
        hp_state = hp_alpha * (hp_state + input[index] - previous);
        previous = input[index];
        lp_state += lp_alpha * (hp_state - lp_state);
        output[index] = lp_state;
    }
}

int main(void) {
    static const uint32_t         rates[] = { 1200U, 1500U, 1600U, 1920U, 2000U, 2400U };
    static float                  transmitted[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    static float                  received[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    static rtnc_modem_workspace_t workspace;
    uint8_t                       payload[64U];
    uint8_t                       decoded[64U];
    size_t                        rate_index;
    size_t                        index;
    for (index = 0U; index < sizeof(payload); ++index) {
        payload[index] = (uint8_t) (index * 43U + 5U);
    }
    (void) printf("baud,success,acquisition,training,evm,equalizer\n");
    for (rate_index = 0U; rate_index < sizeof(rates) / sizeof(rates[0]);
         ++rate_index) {
        rtnc_phy_profile_t  profile;
        rtnc_modem_t        modem;
        rtnc_sync_metrics_t metrics = { 0 };
        size_t              sample_count = 0U;
        size_t              decoded_length = 0U;
        rtnc_modem_status_t status;
        assert(rtnc_phy_profile_qpsk(rates[rate_index], 1650.0F, &profile));
        assert(rtnc_modem_init_profile(&modem, FEC_LDPC_ROBUST, sizeof(payload), &profile));
        assert(rtnc_modem_tx_audio(&modem, payload, sizeof(payload), transmitted, RTNC_MODEM_MAX_AUDIO_SAMPLES, &sample_count) == RTNC_MODEM_OK);
        voice_band(transmitted, sample_count, received);
        status = rtnc_modem_rx_audio(&modem, received, sample_count, decoded, sizeof(decoded), &decoded_length, &metrics, &workspace);
        (void) printf("%u,%d,%.6f,%.6f,%.6f,%d\n", rates[rate_index], status == RTNC_MODEM_OK ? 1 : 0, (double) metrics.acquisition_correlation, (double) metrics.training_correlation, (double) metrics.evm_rms, metrics.equalizer_used ? 1 : 0);
        assert(status == RTNC_MODEM_OK);
        assert(decoded_length == sizeof(payload));
        assert(memcmp(payload, decoded, sizeof(payload)) == 0);
        rtnc_modem_deinit(&modem);
    }
    return 0;
}
