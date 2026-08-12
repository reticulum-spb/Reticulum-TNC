#include "rtnc/modem.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    static const uint32_t         rates[] = { 1200U, 1500U, 1600U, 1920U, 2000U, 2400U };
    static float                  audio[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    static rtnc_modem_workspace_t workspace;
    uint8_t                       input[64U];
    uint8_t                       output[64U];
    size_t                        rate_index;
    size_t                        index;

    for (index = 0U; index < sizeof(input); ++index) {
        input[index] = (uint8_t) (index * 37U + 11U);
    }
    (void) printf("baud,samples_per_symbol,samples,airtime_s,ceiling_Bps,evm\n");
    for (rate_index = 0U; rate_index < sizeof(rates) / sizeof(rates[0]);
         ++rate_index) {
        rtnc_phy_profile_t  profile;
        rtnc_modem_t        modem;
        rtnc_sync_metrics_t metrics = { 0 };
        size_t              sample_count = 0U;
        size_t              output_length = 0U;
        assert(rtnc_phy_profile_qpsk(rates[rate_index], 1650.0F, &profile));
        assert(rtnc_modem_init_profile(&modem, FEC_LDPC_ROBUST, sizeof(input), &profile));
        assert(rtnc_modem_tx_audio(&modem, input, sizeof(input), audio, RTNC_MODEM_MAX_AUDIO_SAMPLES, &sample_count) == RTNC_MODEM_OK);
        assert(sample_count == 688U * (size_t) profile.samples_per_symbol);
        assert(rtnc_modem_rx_audio(&modem, audio, sample_count, output, sizeof(output), &output_length, &metrics, &workspace) == RTNC_MODEM_OK);
        assert(output_length == sizeof(input));
        assert(memcmp(input, output, sizeof(input)) == 0);
        (void) printf("%u,%u,%zu,%.6f,%.3f,%.6f\n", rates[rate_index], (unsigned int) profile.samples_per_symbol, sample_count, (double) sample_count / 48000.0, (double) sizeof(input) * 48000.0 / (double) sample_count, (double) metrics.evm_rms);
        rtnc_modem_deinit(&modem);
    }
    return 0;
}
