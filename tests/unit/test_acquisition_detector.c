#include "rtnc/acquisition.h"
#include "rtnc/burst_detector.h"
#include "rtnc/modem.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    static float                       audio[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    static rtnc_acquisition_detector_t detector;
    rtnc_modem_t                       modem;
    uint8_t                            payload[64U] = { 0U };
    size_t                             sample_count = 0U;
    size_t                             index;
    bool                               detected = false;

    assert(rtnc_modem_init_config(&modem, FEC_LDPC_ROBUST, 64U));
    assert(rtnc_modem_tx_audio(&modem, payload, sizeof(payload), audio, RTNC_MODEM_MAX_AUDIO_SAMPLES, &sample_count) == RTNC_MODEM_OK);
    assert(rtnc_acquisition_detector_init(
        &detector,
        &modem.profile,
        modem.training,
        4U,
        modem.profile.acquisition_threshold
    ));
    for (index = 0U; index < sample_count; ++index) {
        float score;
        if (rtnc_acquisition_detector_process(&detector, audio[index], &score)) {
            detected = true;
            break;
        }
    }
    assert(detected);
    assert(detector.best_score >= modem.profile.acquisition_threshold);
    rtnc_acquisition_detector_deinit(&detector);
    rtnc_modem_deinit(&modem);

    /* A continuous carrier may contain adjacent independently framed packets.
     * The streaming trigger must reopen after each fixed-length candidate. */
    {
        static rtnc_burst_detector_t burst;
        rtnc_burst_detector_config_t config;
        size_t                       frame;
        size_t                       candidates = 0U;
        size_t                       padding;
        assert(rtnc_modem_init_config(&modem, FEC_LDPC_ROBUST, 64U));
        rtnc_burst_detector_default_config(&config);
        config.energy_trigger_enabled = false;
        config.external_trigger_requires_energy = true;
        config.cooldown_samples = 0U;
        config.capture_samples =
            config.pretrigger_samples + rtnc_modem_frame_samples(&modem) +
            4U * modem.profile.samples_per_symbol -
            (2U * RTNC_MODEM_RRC_DELAY_SYMBOLS +
             RTNC_MODEM_ACQUISITION_SYMBOLS - 1U) *
                modem.profile.samples_per_symbol;
        assert(rtnc_burst_detector_init(&burst, &config));
        assert(rtnc_acquisition_detector_init(
            &detector,
            &modem.profile,
            modem.training,
            4U,
            modem.profile.acquisition_threshold
        ));
        for (padding = 0U; padding < config.warmup_samples; ++padding) {
            const float *candidate = NULL;
            size_t       candidate_count = 0U;
            float        score;
            const bool   trigger =
                rtnc_acquisition_detector_process(&detector, 0.0F, &score);
            assert(!rtnc_burst_detector_process_triggered(
                &burst,
                1.0F,
                0.0F,
                trigger,
                &candidate,
                &candidate_count
            ));
        }
        for (frame = 0U; frame < 5U; ++frame) {
            assert(rtnc_modem_tx_audio(&modem, payload, sizeof(payload), audio, RTNC_MODEM_MAX_AUDIO_SAMPLES, &sample_count) == RTNC_MODEM_OK);
            for (index = 0U; index < sample_count; ++index) {
                const float *candidate = NULL;
                size_t       candidate_count = 0U;
                float        score;
                const bool   trigger = rtnc_acquisition_detector_process(
                    &detector,
                    audio[index],
                    &score
                );
                if (rtnc_burst_detector_process_triggered(
                        &burst,
                        16.0F,
                        audio[index],
                        trigger,
                        &candidate,
                        &candidate_count
                    )) {
                    assert(candidate != NULL);
                    assert(candidate_count > 0U);
                    candidates += 1U;
                }
            }
        }
        for (padding = 0U; padding < config.pretrigger_samples; ++padding) {
            const float *candidate = NULL;
            size_t       candidate_count = 0U;
            float        score;
            const bool   trigger =
                rtnc_acquisition_detector_process(&detector, 0.0F, &score);
            if (rtnc_burst_detector_process_triggered(
                    &burst,
                    1.0F,
                    0.0F,
                    trigger,
                    &candidate,
                    &candidate_count
                )) {
                candidates += 1U;
            }
        }
        if (candidates != 5U) {
            (void) fprintf(stderr, "dense candidates=%zu expected=5\n", candidates);
        }
        assert(candidates == 5U);
        rtnc_acquisition_detector_deinit(&detector);
        rtnc_modem_deinit(&modem);
    }
    return 0;
}
