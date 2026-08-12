#include "rtnc/burst_detector.h"

#include <string.h>

void rtnc_burst_detector_default_config(rtnc_burst_detector_config_t *config) {
    if (config == NULL) {
        return;
    }
    config->warmup_samples = 24000U;
    config->pretrigger_samples = 4800U;
    config->release_samples = 2400U;
    config->cooldown_samples = 4800U;
    config->noise_attack_alpha = 0.0002F;
    config->noise_release_alpha = 0.0002F;
    config->signal_alpha = 0.02F;
    config->trigger_ratio = 4.0F;
    config->release_ratio = 1.8F;
    config->impulse_limit_ratio = 20.0F;
    config->maximum_active_samples = RTNC_MODEM_MAX_AUDIO_SAMPLES;
    config->capture_samples = 0U;
    config->energy_trigger_enabled = true;
    config->external_trigger_requires_energy = false;
}

bool rtnc_burst_detector_init(rtnc_burst_detector_t *detector, const rtnc_burst_detector_config_t *config) {
    if (detector == NULL || config == NULL || config->warmup_samples == 0U ||
        config->pretrigger_samples == 0U ||
        config->pretrigger_samples > RTNC_BURST_MAX_PRETRIGGER_SAMPLES ||
        config->release_samples == 0U ||
        config->noise_attack_alpha <= 0.0F ||
        config->noise_attack_alpha > 1.0F ||
        config->noise_release_alpha <= 0.0F ||
        config->noise_release_alpha > 1.0F || config->signal_alpha <= 0.0F ||
        config->signal_alpha > 1.0F || config->trigger_ratio <= 1.0F ||
        config->release_ratio <= 1.0F ||
        config->release_ratio >= config->trigger_ratio ||
        (config->energy_trigger_enabled &&
         config->impulse_limit_ratio <= config->trigger_ratio) ||
        config->maximum_active_samples == 0U ||
        config->maximum_active_samples > RTNC_MODEM_MAX_AUDIO_SAMPLES ||
        config->capture_samples > config->maximum_active_samples) {
        return false;
    }
    (void) memset(detector, 0, sizeof(*detector));
    detector->config = *config;
    return true;
}

bool rtnc_burst_detector_process(rtnc_burst_detector_t *detector, float channel_power, float audio_sample, const float **candidate, size_t *candidate_count) {
    return rtnc_burst_detector_process_triggered(
        detector,
        channel_power,
        audio_sample,
        false,
        candidate,
        candidate_count
    );
}

bool rtnc_burst_detector_process_triggered(rtnc_burst_detector_t *detector, float channel_power, float audio_sample, bool external_trigger, const float **candidate, size_t *candidate_count) {
    bool finished = false;
    if (detector == NULL || candidate == NULL || candidate_count == NULL ||
        channel_power < 0.0F) {
        return false;
    }
    *candidate = NULL;
    *candidate_count = 0U;
    detector->samples_seen += 1U;
    if (detector->samples_seen == 1U) {
        detector->noise_power = channel_power;
        detector->signal_power = channel_power;
    } else {
        detector->signal_power +=
            (double) detector->config.signal_alpha *
            ((double) channel_power - detector->signal_power);
        if (!detector->active && detector->cooldown_samples == 0U &&
            (!detector->config.external_trigger_requires_energy ||
             detector->signal_power <=
                 detector->noise_power *
                     (double) detector->config.release_ratio)) {
            double limited_power = channel_power;
            float  alpha = detector->config.noise_release_alpha;
            if (detector->noise_power > 0.0 &&
                limited_power > detector->noise_power *
                                    (double) detector->config.impulse_limit_ratio) {
                limited_power = detector->noise_power *
                                (double) detector->config.impulse_limit_ratio;
            }
            if (limited_power > detector->noise_power) {
                alpha = detector->config.noise_attack_alpha;
            }
            detector->noise_power +=
                (double) alpha * (limited_power - detector->noise_power);
        }
    }
    if (!detector->active && detector->cooldown_samples > 0U) {
        detector->cooldown_samples -= 1U;
    } else if (!detector->active && detector->samples_seen >= detector->config.warmup_samples && ((external_trigger && (!detector->config.external_trigger_requires_energy || detector->signal_power > detector->noise_power * (double) detector->config.release_ratio)) || (detector->config.energy_trigger_enabled && detector->signal_power > detector->noise_power * (double) detector->config.trigger_ratio))) {
        size_t index;
        detector->candidate_count = 0U;
        for (index = 0U; index < detector->pretrigger_count; ++index) {
            const size_t source =
                (detector->pretrigger_write +
                 detector->config.pretrigger_samples -
                 detector->pretrigger_count + index) %
                detector->config.pretrigger_samples;
            detector->candidate[detector->candidate_count++] =
                detector->pretrigger[source];
        }
        detector->active = true;
        detector->quiet_samples = 0U;
        detector->detected_bursts += 1U;
    }
    if (detector->active) {
        if (detector->candidate_count < RTNC_MODEM_MAX_AUDIO_SAMPLES) {
            detector->candidate[detector->candidate_count++] = audio_sample;
        }
        if (detector->signal_power <
            detector->noise_power * (double) detector->config.release_ratio) {
            detector->quiet_samples += 1U;
        } else {
            detector->quiet_samples = 0U;
        }
        finished = (detector->config.capture_samples > 0U &&
                    detector->candidate_count >=
                        detector->config.capture_samples) ||
                   (detector->config.capture_samples == 0U &&
                    detector->quiet_samples >=
                        detector->config.release_samples) ||
                   detector->candidate_count >=
                       detector->config.maximum_active_samples;
    }
    detector->pretrigger[detector->pretrigger_write] = audio_sample;
    detector->pretrigger_write =
        (detector->pretrigger_write + 1U) % detector->config.pretrigger_samples;
    if (detector->pretrigger_count < detector->config.pretrigger_samples) {
        detector->pretrigger_count += 1U;
    }
    if (finished) {
        *candidate = detector->candidate;
        *candidate_count = detector->candidate_count;
        detector->active = false;
        detector->candidate_count = 0U;
        detector->quiet_samples = 0U;
        detector->cooldown_samples = detector->config.cooldown_samples;
    }
    return finished;
}
