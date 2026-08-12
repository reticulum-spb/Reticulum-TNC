#include "rtnc/modem.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    rtnc_modulation_t modulation;
    uint32_t          baud;
    const char       *name;
} case_t;

static void voice_band(const float *input, size_t count, float *output) {
    const float dt = 1.0F / 48000.0F;
    const float pi = acosf(-1.0F);
    const float hp_rc = 1.0F / (2.0F * pi * 300.0F);
    const float lp_rc = 1.0F / (2.0F * pi * 3000.0F);
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
    static const case_t cases[] = {
        {RTNC_MODULATION_BPSK,   750U,  "bpsk_750"  },
        { RTNC_MODULATION_BPSK,  1200U, "bpsk_1200" },
        { RTNC_MODULATION_QPSK,  800U,  "qpsk_800"  },
        { RTNC_MODULATION_8PSK,  600U,  "8psk_600"  },
        { RTNC_MODULATION_8PSK,  800U,  "8psk_800"  },
        { RTNC_MODULATION_8PSK,  1000U, "8psk_1000" },
        { RTNC_MODULATION_8PSK,  1200U, "8psk_1200" },
        { RTNC_MODULATION_8PSK,  1600U, "8psk_1600" },
        { RTNC_MODULATION_16PSK, 750U,  "16psk_750" },
        { RTNC_MODULATION_16PSK, 1000U, "16psk_1000"},
    };
    static float                  audio[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    static float                  filtered[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    static rtnc_modem_workspace_t workspace;
    uint8_t                       input[64U];
    uint8_t                       output[64U];
    size_t                        index;
    size_t                        case_index;

    for (index = 0U; index < sizeof(input); ++index) {
        input[index] = (uint8_t) (index * 43U + 7U);
    }
    (void) printf("profile,samples,airtime_s,ceiling_Bps,evm,voice_evm,llr\n");
    for (case_index = 0U; case_index < sizeof(cases) / sizeof(cases[0]);
         ++case_index) {
        rtnc_phy_profile_t  profile;
        rtnc_modem_t        modem;
        rtnc_sync_metrics_t metrics = { 0 };
        size_t              sample_count = 0U;
        size_t              output_length = 0U;
        size_t              encoded_bytes;
        float               ideal_evm;
        assert(rtnc_phy_profile_psk(cases[case_index].modulation, cases[case_index].baud, 1650.0F, &profile));
        assert(rtnc_modem_init_profile(&modem, FEC_LDPC_ROBUST, sizeof(input), &profile));
        encoded_bytes = rtnc_fec_encoded_size(
            FEC_LDPC_ROBUST,
            RTNC_FRAME_HEADER_SIZE + sizeof(input) + RTNC_FRAME_CRC_SIZE
        );
        assert(rtnc_modem_tx_audio(&modem, input, sizeof(input), audio, sizeof(audio) / sizeof(audio[0]), &sample_count) == RTNC_MODEM_OK);
        assert(sample_count == rtnc_modem_frame_samples(&modem));
        {
            const rtnc_modem_status_t status = rtnc_modem_rx_audio(&modem, audio, sample_count, output, sizeof(output), &output_length, &metrics, &workspace);
            if (status != RTNC_MODEM_OK) {
                (void) fprintf(stderr, "%s status=%d acquisition=%.6f training=%.6f evm=%.6f equalizer=%d llr=%zu converged=%d iterations=%u\n", cases[case_index].name, (int) status, (double) metrics.acquisition_correlation, (double) metrics.training_correlation, (double) metrics.evm_rms, metrics.equalizer_used ? 1 : 0, workspace.llr_count, workspace.fec_stats.converged ? 1 : 0, workspace.fec_stats.iterations);
            }
            assert(status == RTNC_MODEM_OK);
        }
        assert(output_length == sizeof(input));
        assert(memcmp(input, output, sizeof(input)) == 0);
        assert(workspace.llr_count == encoded_bytes * 8U);
        ideal_evm = metrics.evm_rms;
        voice_band(audio, sample_count, filtered);
        (void) memset(&metrics, 0, sizeof(metrics));
        assert(rtnc_modem_rx_audio(&modem, filtered, sample_count, output, sizeof(output), &output_length, &metrics, &workspace) == RTNC_MODEM_OK);
        assert(output_length == sizeof(input));
        assert(memcmp(input, output, sizeof(input)) == 0);
        (void) printf("%s,%zu,%.6f,%.3f,%.6f,%.6f,%zu\n", cases[case_index].name, sample_count, (double) sample_count / 48000.0, (double) sizeof(input) * 48000.0 / (double) sample_count, (double) ideal_evm, (double) metrics.evm_rms, workspace.llr_count);
        rtnc_modem_deinit(&modem);
    }
    return 0;
}
