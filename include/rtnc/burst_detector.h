#ifndef RTNC_BURST_DETECTOR_H
#define RTNC_BURST_DETECTOR_H

#include "rtnc/modem.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { RTNC_BURST_MAX_PRETRIGGER_SAMPLES = 12000U };

typedef struct {
    size_t warmup_samples;
    size_t pretrigger_samples;
    size_t release_samples;
    size_t cooldown_samples;
    float  noise_attack_alpha;
    float  noise_release_alpha;
    float  signal_alpha;
    float  trigger_ratio;
    float  release_ratio;
    float  impulse_limit_ratio;
    size_t maximum_active_samples;
    size_t capture_samples;
    bool   energy_trigger_enabled;
    bool   external_trigger_requires_energy;
} rtnc_burst_detector_config_t;

typedef struct {
    rtnc_burst_detector_config_t config;
    float                        pretrigger[RTNC_BURST_MAX_PRETRIGGER_SAMPLES];
    float                        candidate[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    size_t                       pretrigger_count;
    size_t                       pretrigger_write;
    size_t                       candidate_count;
    size_t                       quiet_samples;
    size_t                       cooldown_samples;
    uint64_t                     samples_seen;
    uint64_t                     detected_bursts;
    double                       noise_power;
    double                       signal_power;
    bool                         active;
} rtnc_burst_detector_t;

void rtnc_burst_detector_default_config(rtnc_burst_detector_config_t *config);
bool rtnc_burst_detector_init(rtnc_burst_detector_t *detector, const rtnc_burst_detector_config_t *config);

/**
 * Consume one demodulated audio sample and its pre-demod complex-channel power.
 * On completion, candidate points into detector-owned storage until next call.
 */
bool rtnc_burst_detector_process(rtnc_burst_detector_t *detector, float channel_power, float audio_sample, const float **candidate, size_t *candidate_count);

/** As above, but an external normalized correlator may force opening. */
bool rtnc_burst_detector_process_triggered(rtnc_burst_detector_t *detector, float channel_power, float audio_sample, bool external_trigger, const float **candidate, size_t *candidate_count);

#endif
