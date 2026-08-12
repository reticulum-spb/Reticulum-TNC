#include "rtnc/burst_detector.h"

#include <assert.h>
#include <stddef.h>

int main(void) {
    static rtnc_burst_detector_t detector;
    rtnc_burst_detector_config_t config = {
        .warmup_samples = 10U,
        .pretrigger_samples = 4U,
        .release_samples = 3U,
        .cooldown_samples = 4U,
        .noise_attack_alpha = 0.01F,
        .noise_release_alpha = 0.02F,
        .signal_alpha = 0.5F,
        .trigger_ratio = 4.0F,
        .release_ratio = 1.8F,
        .impulse_limit_ratio = 20.0F,
        .maximum_active_samples = RTNC_MODEM_MAX_AUDIO_SAMPLES,
        .capture_samples = 0U,
        .energy_trigger_enabled = true,
    };
    const float *candidate = NULL;
    size_t       candidate_count = 0U;
    size_t       index;
    bool         completed = false;

    assert(rtnc_burst_detector_init(&detector, &config));
    for (index = 0U; index < 12U; ++index) {
        assert(!rtnc_burst_detector_process(&detector, 1.0F, (float) index, &candidate, &candidate_count));
    }
    for (index = 0U; index < 8U; ++index) {
        completed = rtnc_burst_detector_process(
            &detector,
            16.0F,
            100.0F + (float) index,
            &candidate,
            &candidate_count
        );
        assert(!completed);
    }
    for (index = 0U; index < 16U && !completed; ++index) {
        completed = rtnc_burst_detector_process(
            &detector,
            1.0F,
            200.0F + (float) index,
            &candidate,
            &candidate_count
        );
    }
    assert(completed);
    assert(detector.detected_bursts == 1U);
    assert(candidate_count >= config.pretrigger_samples + 8U);
    assert(candidate[0] == 8.0F);
    assert(candidate[3] == 11.0F);
    for (index = 0U; index < config.cooldown_samples; ++index) {
        assert(!rtnc_burst_detector_process(&detector, 16.0F, 300.0F, &candidate, &candidate_count));
    }
    assert(detector.detected_bursts == 1U);
    return 0;
}
