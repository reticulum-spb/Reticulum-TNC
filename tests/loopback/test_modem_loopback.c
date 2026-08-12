#include "rtnc/modem.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { PREFIX_SAMPLES = 19U };

int main(void) {
    const uint8_t input[] = {
        0x52U,
        0x65U,
        0x74U,
        0x69U,
        0x63U,
        0x75U,
        0x6cU,
        0x75U,
        0x6dU,
    };
    float                         audio[RTNC_MODEM_MAX_AUDIO_SAMPLES] = { 0.0F };
    uint8_t                       output[RTNC_FRAME_MAX_PAYLOAD];
    rtnc_modem_t                  modem;
    static rtnc_modem_workspace_t workspace;
    rtnc_sync_metrics_t           metrics;
    size_t                        generated = 0U;
    size_t                        output_length = 0U;

    assert(rtnc_modem_init(&modem));
    assert(rtnc_modem_tx_audio(&modem, input, sizeof(input), &audio[PREFIX_SAMPLES], RTNC_MODEM_MAX_AUDIO_SAMPLES - PREFIX_SAMPLES, &generated) == RTNC_MODEM_OK);
    {
        const rtnc_modem_status_t status = rtnc_modem_rx_audio(
            &modem,
            audio,
            generated + PREFIX_SAMPLES,
            output,
            sizeof(output),
            &output_length,
            &metrics,
            &workspace
        );
        if (status != RTNC_MODEM_OK) {
            (void) fprintf(stderr, "modem RX status=%d detected=%d timing=%f\n", (int) status, metrics.frame_detected ? 1 : 0, (double) metrics.timing_symbols);
        }
        assert(status == RTNC_MODEM_OK);
    }
    assert(metrics.frame_detected);
    assert(metrics.acquisition_correlation > 0.99F);
    assert(metrics.acquisition_correlation <= 1.0F);
    assert(metrics.training_correlation > 0.95F);
    assert(metrics.training_correlation <= 1.0F);
    assert(fabsf(metrics.timing_symbols - (float) PREFIX_SAMPLES / 40.0F) < 0.001F);
    assert(output_length == sizeof(input));
    assert(memcmp(input, output, sizeof(input)) == 0);
    assert(workspace.llr_count == (RTNC_FRAME_HEADER_SIZE + sizeof(input) + RTNC_FRAME_CRC_SIZE) * 8U);
    assert(workspace.fec_stats.converged);
    assert(workspace.fec_stats.iterations == 0U);

    {
        uint8_t maximum[RTNC_FRAME_MAX_PAYLOAD];
        size_t  index;
        for (index = 0U; index < sizeof(maximum); ++index) {
            maximum[index] = (uint8_t) (index ^ 0x5aU);
        }
        (void) memset(audio, 0, sizeof(audio));
        assert(rtnc_modem_tx_audio(&modem, maximum, sizeof(maximum), audio, RTNC_MODEM_MAX_AUDIO_SAMPLES, &generated) == RTNC_MODEM_OK);
        assert(rtnc_modem_rx_audio(&modem, audio, generated, output, sizeof(output), &output_length, &metrics, &workspace) == RTNC_MODEM_OK);
        assert(output_length == sizeof(maximum));
        assert(memcmp(maximum, output, sizeof(maximum)) == 0);
    }

    rtnc_modem_deinit(&modem);

    {
        uint8_t robust_payload[64];
        size_t  index;
        for (index = 0U; index < sizeof(robust_payload); ++index) {
            robust_payload[index] = (uint8_t) (0xa5U ^ index);
        }
        (void) memset(audio, 0, sizeof(audio));
        assert(rtnc_modem_init_config(&modem, FEC_LDPC_ROBUST, 64U));
        assert(rtnc_modem_tx_audio(&modem, robust_payload, sizeof(robust_payload), audio, RTNC_MODEM_MAX_AUDIO_SAMPLES, &generated) == RTNC_MODEM_OK);
        assert(rtnc_modem_rx_audio(&modem, audio, generated, output, sizeof(output), &output_length, &metrics, &workspace) == RTNC_MODEM_OK);
        assert(output_length == sizeof(robust_payload));
        assert(memcmp(robust_payload, output, sizeof(robust_payload)) == 0);
        assert(workspace.fec_stats.converged);
        assert(workspace.llr_count == 9U * 128U);
        rtnc_modem_deinit(&modem);
    }
    return 0;
}
