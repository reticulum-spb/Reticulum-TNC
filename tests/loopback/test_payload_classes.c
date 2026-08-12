#include "rtnc/modem.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    fec_mode_t mode;
    uint8_t    payload_class;
    size_t     expected_samples;
} test_case_t;

int main(void) {
    static const test_case_t cases[] = {
        {FEC_NONE,         64U,  15200U},
        { FEC_NONE,        128U, 25440U},
        { FEC_LDPC_ROBUST, 64U,  27520U},
        { FEC_LDPC_ROBUST, 128U, 48000U},
        { FEC_LDPC_NORMAL, 64U,  21760U},
        { FEC_LDPC_NORMAL, 128U, 37120U},
    };
    static float                  audio[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    static rtnc_modem_workspace_t workspace;
    uint8_t                       input[RTNC_FRAME_MAX_PAYLOAD];
    uint8_t                       output[RTNC_FRAME_MAX_PAYLOAD];
    size_t                        case_index;

    (void) printf("fec_mode,payload_bytes,samples,ceiling_bytes_per_second\n");
    for (case_index = 0U; case_index < sizeof(cases) / sizeof(cases[0]);
         ++case_index) {
        rtnc_modem_t        modem;
        rtnc_sync_metrics_t metrics;
        size_t              sample_count = 0U;
        size_t              output_length = 0U;
        size_t              index;
        for (index = 0U; index < cases[case_index].payload_class; ++index) {
            input[index] = (uint8_t) (index * 17U + case_index);
        }
        (void) memset(audio, 0, sizeof(audio));
        assert(rtnc_modem_init_config(&modem, cases[case_index].mode, cases[case_index].payload_class));
        assert(rtnc_modem_tx_audio(&modem, input, cases[case_index].payload_class, audio, RTNC_MODEM_MAX_AUDIO_SAMPLES, &sample_count) == RTNC_MODEM_OK);
        assert(sample_count == cases[case_index].expected_samples);
        assert(rtnc_modem_rx_audio(&modem, audio, sample_count, output, sizeof(output), &output_length, &metrics, &workspace) == RTNC_MODEM_OK);
        assert(output_length == cases[case_index].payload_class);
        assert(memcmp(input, output, output_length) == 0);
        (void) printf("%d,%u,%zu,%.3f\n", (int) cases[case_index].mode, cases[case_index].payload_class, sample_count, (double) cases[case_index].payload_class * 48000.0 / (double) sample_count);
        rtnc_modem_deinit(&modem);
    }
    return 0;
}
