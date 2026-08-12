#include "rtnc/modem.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { PAYLOAD_BYTES = 64U };

typedef struct {
    const char *name;
    float       highpass_hz;
    float       lowpass_hz;
    size_t      echo_delay;
    float       echo_gain;
    float       clip_fraction;
    bool        expect_success;
    bool        expect_equalizer;
} channel_case_t;

static void apply_channel(const float *input, size_t count, float *output, const channel_case_t *channel) {
    const float sample_rate = 48000.0F;
    const float dt = 1.0F / sample_rate;
    const float hp_rc = channel->highpass_hz > 0.0F
                            ? 1.0F / (2.0F * (float) M_PI * channel->highpass_hz)
                            : 0.0F;
    const float lp_rc = channel->lowpass_hz > 0.0F
                            ? 1.0F / (2.0F * (float) M_PI * channel->lowpass_hz)
                            : 0.0F;
    const float hp_alpha = hp_rc > 0.0F ? hp_rc / (hp_rc + dt) : 0.0F;
    const float lp_alpha = lp_rc > 0.0F ? dt / (lp_rc + dt) : 0.0F;
    float       previous_input = 0.0F;
    float       highpass_state = 0.0F;
    float       lowpass_state = 0.0F;
    float       peak = 0.0F;
    float       clip_level;
    size_t      index;

    for (index = 0U; index < count; ++index) {
        peak = fmaxf(peak, fabsf(input[index]));
    }
    clip_level = channel->clip_fraction > 0.0F
                     ? channel->clip_fraction * peak
                     : peak;
    for (index = 0U; index < count; ++index) {
        float value = input[index];
        if (channel->echo_gain != 0.0F && index >= channel->echo_delay) {
            value += channel->echo_gain * input[index - channel->echo_delay];
        }
        if (channel->highpass_hz > 0.0F) {
            highpass_state =
                hp_alpha * (highpass_state + value - previous_input);
            previous_input = value;
            value = highpass_state;
        }
        if (channel->lowpass_hz > 0.0F) {
            lowpass_state += lp_alpha * (value - lowpass_state);
            value = lowpass_state;
        }
        if (channel->clip_fraction > 0.0F) {
            value = fmaxf(-clip_level, fminf(clip_level, value));
        }
        output[index] = value;
    }
}

static bool run_case(const channel_case_t *channel, float *evm, float *training_score, float *cfo_hz, bool *equalizer_used, unsigned int *hard_errors) {
    static float                  transmitted[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    static float                  received[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    static rtnc_modem_workspace_t workspace;
    uint8_t                       payload[PAYLOAD_BYTES];
    uint8_t                       decoded[PAYLOAD_BYTES];
    rtnc_modem_t                  modem;
    rtnc_sync_metrics_t           metrics;
    size_t                        sample_count = 0U;
    size_t                        decoded_length = 0U;
    size_t                        index;
    rtnc_modem_status_t           status;
    uint8_t                       frame[RTNC_FRAME_MAX_ENCODED_SIZE];
    uint8_t protected[RTNC_MODEM_MAX_PROTECTED_BYTES] = { 0U };
    uint8_t encoded[RTNC_MODEM_MAX_FEC_BYTES];
    size_t  frame_length = 0U;
    size_t  encoded_length = 0U;

    for (index = 0U; index < PAYLOAD_BYTES; ++index) {
        payload[index] = (uint8_t) (index * 37U + 0x29U);
    }
    assert(rtnc_modem_init_config(&modem, FEC_LDPC_ROBUST, PAYLOAD_BYTES));
    assert(rtnc_modem_tx_audio(&modem, payload, sizeof(payload), transmitted, RTNC_MODEM_MAX_AUDIO_SAMPLES, &sample_count) == RTNC_MODEM_OK);
    apply_channel(transmitted, sample_count, received, channel);
    status = rtnc_modem_rx_audio(&modem, received, sample_count, decoded, sizeof(decoded), &decoded_length, &metrics, &workspace);
    *evm = metrics.evm_rms;
    *training_score = metrics.training_correlation;
    *cfo_hz = metrics.carrier_offset_hz;
    *equalizer_used = metrics.equalizer_used;
    assert(rtnc_frame_build(payload, sizeof(payload), frame, sizeof(frame), &frame_length) == RTNC_FRAME_OK);
    (void) memcpy(protected, frame, frame_length);
    assert(rtnc_fec_encode(FEC_LDPC_ROBUST, protected, RTNC_FRAME_HEADER_SIZE + PAYLOAD_BYTES + RTNC_FRAME_CRC_SIZE, encoded, sizeof(encoded), &encoded_length) == RTNC_FEC_OK);
    *hard_errors = 0U;
    if (workspace.llr_count == encoded_length * 8U) {
        for (index = 0U; index < workspace.llr_count; ++index) {
            const unsigned int expected =
                ((unsigned int) encoded[index / 8U] >>
                 (7U - (unsigned int) (index % 8U))) &
                1U;
            const unsigned int received_bit =
                workspace.llr[index] < 0.0F ? 1U : 0U;
            if (expected != received_bit) {
                ++(*hard_errors);
            }
        }
    }
    rtnc_modem_deinit(&modem);
    return status == RTNC_MODEM_OK && decoded_length == sizeof(payload) &&
           memcmp(payload, decoded, sizeof(payload)) == 0;
}

int main(void) {
    static const channel_case_t cases[] = {
        {"identity",         0.0F,   0.0F,    0U,  0.0F,  0.0F,  true, false},
        { "voice_band",      300.0F, 3000.0F, 0U,  0.0F,  0.0F,  true, false},
        { "echo_6",          0.0F,   0.0F,    6U,  0.20F, 0.0F,  true, false},
        { "echo_12",         0.0F,   0.0F,    12U, 0.30F, 0.0F,  true, false},
        { "echo_12_gain45",  0.0F,   0.0F,    12U, 0.45F, 0.0F,  true, false},
        { "echo_20_gain40",  0.0F,   0.0F,    20U, 0.40F, 0.0F,  true, true },
        { "clip_70",         0.0F,   0.0F,    0U,  0.0F,  0.70F, true, false},
        { "voice_echo_clip", 300.0F, 3000.0F, 8U,  0.20F, 0.75F, true, false},
    };
    size_t index;
    (void) printf("channel,success,equalizer_used,training_score,cfo_hz,evm,"
                  "hard_errors\n");
    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        float        evm = 0.0F;
        float        training_score = 0.0F;
        unsigned int hard_errors = 0U;
        float        cfo_hz = 0.0F;
        bool         equalizer_used = false;
        const bool   success =
            run_case(&cases[index], &evm, &training_score, &cfo_hz, &equalizer_used, &hard_errors);
        (void) printf("%s,%d,%d,%.6f,%.6f,%.6f,%u\n", cases[index].name, success ? 1 : 0, equalizer_used ? 1 : 0, (double) training_score, (double) cfo_hz, (double) evm, hard_errors);
        assert(success == cases[index].expect_success);
        assert(equalizer_used == cases[index].expect_equalizer);
    }
    return 0;
}
