#include "rtnc/platform_config.h"
#include "rtnc/audio_ring.h"
#include "rtnc/decode_queue.h"
#include "rtnc/fragmentation.h"
#include "rtnc/modem.h"
#include "rtnc/packet_queue.h"

#include <cyaml/cyaml.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

static const cyaml_schema_field_t gpio_fields[] = {
    CYAML_FIELD_UINT("port", CYAML_FLAG_DEFAULT, rtnc_gpio_config_t, port),
    CYAML_FIELD_UINT("pin", CYAML_FLAG_DEFAULT, rtnc_gpio_config_t, pin),
    CYAML_FIELD_BOOL("active_high", CYAML_FLAG_DEFAULT, rtnc_gpio_config_t, active_high),
    CYAML_FIELD_END,
};

static const cyaml_schema_field_t audio_fields[] = {
    CYAML_FIELD_STRING_PTR("device", CYAML_FLAG_POINTER, rtnc_audio_config_t, device, 1U, 127U),
    CYAML_FIELD_UINT("sample_rate_hz", CYAML_FLAG_DEFAULT, rtnc_audio_config_t, sample_rate_hz),
    CYAML_FIELD_UINT("period_ms", CYAML_FLAG_DEFAULT, rtnc_audio_config_t, period_ms),
    CYAML_FIELD_UINT("periods", CYAML_FLAG_DEFAULT, rtnc_audio_config_t, periods),
    CYAML_FIELD_END,
};

static const cyaml_schema_field_t modem_fields[] = {
    CYAML_FIELD_STRING_PTR("profile", CYAML_FLAG_POINTER, rtnc_modem_config_t, profile, 1U, 31U),
    CYAML_FIELD_STRING_PTR("control_profile", CYAML_FLAG_POINTER | CYAML_FLAG_OPTIONAL, rtnc_modem_config_t, control_profile, 1U, 31U),
    CYAML_FIELD_END,
};

static const cyaml_schema_field_t phy_profile_fields[] = {
    CYAML_FIELD_STRING_PTR("name", CYAML_FLAG_POINTER, rtnc_phy_profile_config_t, name, 1U, 31U),
    CYAML_FIELD_STRING_PTR("modulation", CYAML_FLAG_POINTER, rtnc_phy_profile_config_t, modulation, 1U, 8U),
    CYAML_FIELD_UINT("symbol_rate_baud", CYAML_FLAG_DEFAULT, rtnc_phy_profile_config_t, symbol_rate_baud),
    CYAML_FIELD_FLOAT("carrier_hz", CYAML_FLAG_DEFAULT, rtnc_phy_profile_config_t, carrier_hz),
    CYAML_FIELD_FLOAT("rrc_rolloff", CYAML_FLAG_DEFAULT, rtnc_phy_profile_config_t, rrc_rolloff),
    CYAML_FIELD_FLOAT("acquisition_threshold", CYAML_FLAG_DEFAULT, rtnc_phy_profile_config_t, acquisition_threshold),
    CYAML_FIELD_FLOAT("training_threshold", CYAML_FLAG_DEFAULT, rtnc_phy_profile_config_t, training_threshold),
    CYAML_FIELD_FLOAT("detector_bandwidth_margin", CYAML_FLAG_DEFAULT, rtnc_phy_profile_config_t, detector_bandwidth_margin),
    CYAML_FIELD_UINT("fec_mode", CYAML_FLAG_DEFAULT, rtnc_phy_profile_config_t, fec_mode),
    CYAML_FIELD_UINT("payload_class_bytes", CYAML_FLAG_DEFAULT, rtnc_phy_profile_config_t, payload_class_bytes),
    CYAML_FIELD_END,
};

static const cyaml_schema_value_t phy_profile_schema = {
    CYAML_VALUE_MAPPING(CYAML_FLAG_DEFAULT, rtnc_phy_profile_config_t, phy_profile_fields),
};

static const cyaml_schema_value_t float_schema = {
    CYAML_VALUE_FLOAT(CYAML_FLAG_DEFAULT, float),
};

static const cyaml_schema_field_t tx_fields[] = {
    CYAML_FIELD_UINT("lead_ms", CYAML_FLAG_DEFAULT, rtnc_tx_config_t, lead_ms),
    CYAML_FIELD_UINT("tail_ms", CYAML_FLAG_DEFAULT, rtnc_tx_config_t, tail_ms),
    CYAML_FIELD_FLOAT("filter_gain", CYAML_FLAG_DEFAULT, rtnc_tx_config_t, filter_gain),
    CYAML_FIELD_SEQUENCE_FIXED("response_eq_taps", CYAML_FLAG_DEFAULT, rtnc_tx_config_t, response_eq_taps, &float_schema, RTNC_TX_EQ_TAP_COUNT),
    CYAML_FIELD_END,
};

static const cyaml_schema_field_t link_fields[] = {
    CYAML_FIELD_UINT("mtu", CYAML_FLAG_DEFAULT, rtnc_link_config_t, mtu),
    CYAML_FIELD_UINT("reassembly_timeout_ms", CYAML_FLAG_DEFAULT, rtnc_link_config_t, reassembly_timeout_ms),
    CYAML_FIELD_END,
};

static const cyaml_schema_field_t worker_fields[] = {
    CYAML_FIELD_UINT("dsp_queue_blocks", CYAML_FLAG_DEFAULT, rtnc_worker_config_t, dsp_queue_blocks),
    CYAML_FIELD_UINT("decode_queue_frames", CYAML_FLAG_DEFAULT, rtnc_worker_config_t, decode_queue_frames),
    CYAML_FIELD_UINT("equalizer_queue_frames", CYAML_FLAG_DEFAULT, rtnc_worker_config_t, equalizer_queue_frames),
    CYAML_FIELD_BOOL("parallel_equalizer", CYAML_FLAG_DEFAULT, rtnc_worker_config_t, parallel_equalizer),
    CYAML_FIELD_UINT("equalizer_nice", CYAML_FLAG_DEFAULT, rtnc_worker_config_t, equalizer_nice),
    CYAML_FIELD_END,
};

static const cyaml_schema_field_t detector_fields[] = {
    CYAML_FIELD_UINT("warmup_ms", CYAML_FLAG_DEFAULT, rtnc_detector_config_t, warmup_ms),
    CYAML_FIELD_UINT("pretrigger_ms", CYAML_FLAG_DEFAULT, rtnc_detector_config_t, pretrigger_ms),
    CYAML_FIELD_UINT("cooldown_ms", CYAML_FLAG_DEFAULT, rtnc_detector_config_t, cooldown_ms),
    CYAML_FIELD_UINT("noise_attack_ms", CYAML_FLAG_DEFAULT, rtnc_detector_config_t, noise_attack_ms),
    CYAML_FIELD_UINT("noise_release_ms", CYAML_FLAG_DEFAULT, rtnc_detector_config_t, noise_release_ms),
    CYAML_FIELD_UINT("signal_attack_ms", CYAML_FLAG_DEFAULT, rtnc_detector_config_t, signal_attack_ms),
    CYAML_FIELD_UINT("maximum_busy_ms", CYAML_FLAG_DEFAULT, rtnc_detector_config_t, maximum_busy_ms),
    CYAML_FIELD_FLOAT("energy_open_ratio", CYAML_FLAG_DEFAULT, rtnc_detector_config_t, energy_open_ratio),
    CYAML_FIELD_FLOAT("energy_close_ratio", CYAML_FLAG_DEFAULT, rtnc_detector_config_t, energy_close_ratio),
    CYAML_FIELD_FLOAT("impulse_limit_ratio", CYAML_FLAG_DEFAULT, rtnc_detector_config_t, impulse_limit_ratio),
    CYAML_FIELD_END,
};

static const cyaml_schema_field_t runtime_fields[] = {
    CYAML_FIELD_UINT("tx_queue_packets", CYAML_FLAG_DEFAULT, rtnc_runtime_config_t, tx_queue_packets),
    CYAML_FIELD_UINT("rx_queue_packets", CYAML_FLAG_DEFAULT, rtnc_runtime_config_t, rx_queue_packets),
    CYAML_FIELD_UINT("channel_busy_timeout_ms", CYAML_FLAG_DEFAULT, rtnc_runtime_config_t, channel_busy_timeout_ms),
    CYAML_FIELD_UINT("rx_guard_ms", CYAML_FLAG_DEFAULT, rtnc_runtime_config_t, rx_guard_ms),
    CYAML_FIELD_UINT("kiss_tcp_port", CYAML_FLAG_DEFAULT, rtnc_runtime_config_t, kiss_tcp_port),
    CYAML_FIELD_END,
};

static const cyaml_schema_field_t platform_fields[] = {
    CYAML_FIELD_MAPPING("ptt", CYAML_FLAG_DEFAULT, rtnc_platform_config_t, ptt, gpio_fields),
    CYAML_FIELD_MAPPING("audio", CYAML_FLAG_DEFAULT, rtnc_platform_config_t, audio, audio_fields),
    CYAML_FIELD_MAPPING("modem", CYAML_FLAG_DEFAULT, rtnc_platform_config_t, modem, modem_fields),
    CYAML_FIELD_MAPPING("tx", CYAML_FLAG_DEFAULT, rtnc_platform_config_t, tx, tx_fields),
    CYAML_FIELD_MAPPING("link", CYAML_FLAG_DEFAULT, rtnc_platform_config_t, link, link_fields),
    CYAML_FIELD_MAPPING("workers", CYAML_FLAG_DEFAULT, rtnc_platform_config_t, workers, worker_fields),
    CYAML_FIELD_MAPPING("detector", CYAML_FLAG_DEFAULT, rtnc_platform_config_t, detector, detector_fields),
    CYAML_FIELD_MAPPING("runtime", CYAML_FLAG_DEFAULT, rtnc_platform_config_t, runtime, runtime_fields),
    CYAML_FIELD_SEQUENCE("profiles", CYAML_FLAG_POINTER, rtnc_platform_config_t, profiles, &phy_profile_schema, 1U, 32U),
    CYAML_FIELD_END,
};

static const cyaml_schema_value_t platform_schema = {
    CYAML_VALUE_MAPPING(CYAML_FLAG_POINTER, rtnc_platform_config_t, platform_fields),
};

static const cyaml_config_t cyaml_config = {
    .log_fn = cyaml_log,
    .mem_fn = cyaml_mem,
    .log_level = CYAML_LOG_WARNING,
};

static bool platform_config_is_valid(const rtnc_platform_config_t *config);

static bool platform_phy_profile(const rtnc_platform_config_t *config, rtnc_phy_profile_t *profile);

static bool platform_config_is_valid(const rtnc_platform_config_t *config) {
    double             tap_absolute_sum = 0.0;
    bool               tap_nonzero = false;
    size_t             tap;
    rtnc_phy_profile_t profile;
    unsigned int       index;
    if (config == NULL || config->audio.device == NULL ||
        config->modem.profile == NULL || config->profiles == NULL ||
        config->profiles_count == 0U ||
        !platform_phy_profile(config, &profile)) {
        return false;
    }
    if (config->modem.control_profile != NULL &&
        (strcmp(config->modem.control_profile, config->modem.profile) == 0 ||
         rtnc_platform_profile_config_named(
             config,
             config->modem.control_profile
         ) == NULL)) {
        return false;
    }
    for (index = 0U; index < config->profiles_count; ++index) {
        unsigned int       other;
        rtnc_phy_profile_t candidate;
        rtnc_modem_rate_t  rate;
        if (!rtnc_platform_phy_profile_named(
                config,
                config->profiles[index].name,
                &candidate
            ) ||
            !rtnc_modem_profile_rate(
                &candidate,
                (fec_mode_t) config->profiles[index].fec_mode,
                config->profiles[index].payload_class_bytes,
                &rate
            )) {
            return false;
        }
        for (other = index + 1U; other < config->profiles_count; ++other) {
            if (strcmp(config->profiles[index].name, config->profiles[other].name) == 0) {
                return false;
            }
        }
    }
    for (tap = 0U; tap < RTNC_TX_EQ_TAP_COUNT; ++tap) {
        const float value = config->tx.response_eq_taps[tap];
        if (!isfinite(value)) {
            return false;
        }
        tap_absolute_sum += fabs((double) value);
        tap_nonzero = tap_nonzero || fabsf(value) > 1.0e-6F;
        if (tap < RTNC_TX_EQ_TAP_COUNT / 2U &&
            fabsf(value - config->tx.response_eq_taps[RTNC_TX_EQ_TAP_COUNT - 1U - tap]) > 1.0e-5F) {
            return false;
        }
    }
    {
        const double half_band =
            0.5 * (double) profile.symbol_rate_baud *
            (1.0 + (double) profile.rrc_rolloff);
        double frequency;
        for (frequency = (double) profile.carrier_hz - half_band;
             frequency <= (double) profile.carrier_hz + half_band;
             frequency += 100.0) {
            if (rtnc_tx_eq_zero_phase_gain(config->tx.response_eq_taps, frequency) <= 0.05) {
                return false;
            }
        }
    }
    return config->audio.sample_rate_hz == 48000U &&
           config->audio.period_ms == 10U && config->audio.periods >= 2U &&
           config->audio.periods <= 8U &&
           config->tx.lead_ms <= 1000U && config->tx.tail_ms <= 1000U &&
           config->tx.filter_gain >= 0.1F &&
           config->tx.filter_gain <= 4.0F &&
           tap_nonzero && tap_absolute_sum <= 8.0 &&
           config->link.mtu >= RTNC_LINK_MIN_MTU &&
           config->runtime.tx_queue_packets >= 1U &&
           config->runtime.tx_queue_packets <=
               RTNC_PACKET_QUEUE_MAX_CAPACITY &&
           config->runtime.rx_queue_packets >= 1U &&
           config->runtime.rx_queue_packets <=
               RTNC_PACKET_QUEUE_MAX_CAPACITY &&
           config->runtime.channel_busy_timeout_ms >= 100U &&
           config->runtime.channel_busy_timeout_ms <= 60000U &&
           config->runtime.rx_guard_ms <= 2000U &&
           config->runtime.kiss_tcp_port >= 1024U &&
           config->link.mtu <= RTNC_LINK_MAX_MTU &&
           config->link.reassembly_timeout_ms >= 1000U &&
           config->link.reassembly_timeout_ms <= 60000U &&
           config->workers.dsp_queue_blocks >= 2U &&
           config->workers.dsp_queue_blocks <= RTNC_AUDIO_RING_MAX_CAPACITY &&
           config->workers.decode_queue_frames >= 2U &&
           config->workers.decode_queue_frames <=
               RTNC_DECODE_QUEUE_MAX_CAPACITY &&
           config->workers.equalizer_queue_frames >= 1U &&
           config->workers.equalizer_queue_frames <=
               RTNC_DECODE_QUEUE_MAX_CAPACITY &&
           config->workers.equalizer_nice <= 19U &&
           config->detector.warmup_ms >= 100U &&
           config->detector.warmup_ms <= 2000U &&
           config->detector.pretrigger_ms >= 20U &&
           config->detector.pretrigger_ms <= 250U &&
           config->detector.cooldown_ms <= 1000U &&
           config->detector.noise_attack_ms >= 10U &&
           config->detector.noise_release_ms >=
               config->detector.noise_attack_ms &&
           config->detector.noise_release_ms <= 10000U &&
           config->detector.signal_attack_ms >= 1U &&
           config->detector.signal_attack_ms <= 500U &&
           config->detector.maximum_busy_ms >= 500U &&
           config->detector.maximum_busy_ms <= 5000U &&
           config->detector.energy_open_ratio > 1.0F &&
           config->detector.energy_close_ratio > 1.0F &&
           config->detector.energy_close_ratio <
               config->detector.energy_open_ratio &&
           config->detector.impulse_limit_ratio >
               config->detector.energy_open_ratio;
}

static bool platform_phy_profile(const rtnc_platform_config_t *config, rtnc_phy_profile_t *profile) {
    if (config == NULL || config->modem.profile == NULL) {
        return false;
    }
    return rtnc_platform_phy_profile_named(config, config->modem.profile, profile);
}

bool rtnc_platform_phy_profile_named(const rtnc_platform_config_t *config, const char *name, rtnc_phy_profile_t *profile) {
    unsigned int index;
    if (config == NULL || name == NULL || profile == NULL ||
        config->profiles == NULL) {
        return false;
    }
    for (index = 0U; index < config->profiles_count; ++index) {
        const rtnc_phy_profile_config_t *entry = &config->profiles[index];
        rtnc_phy_profile_t               candidate;
        rtnc_modulation_t                modulation;
        if (entry->name == NULL || strcmp(entry->name, name) != 0) {
            continue;
        }
        if (entry->modulation == NULL) {
            return false;
        }
        if (strcmp(entry->modulation, "bpsk") == 0) {
            modulation = RTNC_MODULATION_BPSK;
        } else if (strcmp(entry->modulation, "qpsk") == 0) {
            modulation = RTNC_MODULATION_QPSK;
        } else if (strcmp(entry->modulation, "8psk") == 0) {
            modulation = RTNC_MODULATION_8PSK;
        } else if (strcmp(entry->modulation, "16psk") == 0) {
            modulation = RTNC_MODULATION_16PSK;
        } else {
            return false;
        }
        if (entry->fec_mode > 2U ||
            (entry->payload_class_bytes != 64U &&
             entry->payload_class_bytes != 128U) ||
            entry->detector_bandwidth_margin < 1.0F ||
            entry->detector_bandwidth_margin > 3.0F ||
            !rtnc_phy_profile_psk(modulation, entry->symbol_rate_baud, entry->carrier_hz, &candidate)) {
            return false;
        }
        candidate.rrc_rolloff = entry->rrc_rolloff;
        candidate.acquisition_threshold = entry->acquisition_threshold;
        candidate.training_threshold = entry->training_threshold;
        if (!rtnc_phy_profile_is_valid(&candidate)) {
            return false;
        }
        *profile = candidate;
        return true;
    }
    return false;
}

const rtnc_phy_profile_config_t *rtnc_platform_selected_profile(
    const rtnc_platform_config_t *config
) {
    if (config == NULL) {
        return NULL;
    }
    return rtnc_platform_profile_config_named(config, config->modem.profile);
}

const rtnc_phy_profile_config_t *rtnc_platform_profile_config_named(
    const rtnc_platform_config_t *config,
    const char                   *name
) {
    unsigned int index;
    if (config == NULL || name == NULL || config->profiles == NULL) {
        return NULL;
    }
    for (index = 0U; index < config->profiles_count; ++index) {
        if (config->profiles[index].name != NULL &&
            strcmp(config->profiles[index].name, name) == 0) {
            return &config->profiles[index];
        }
    }
    return NULL;
}

bool rtnc_platform_burst_config(const rtnc_platform_config_t *config, rtnc_burst_detector_config_t *detector) {
    if (!platform_config_is_valid(config) || detector == NULL) {
        return false;
    }
    detector->warmup_samples =
        (size_t) config->audio.sample_rate_hz * config->detector.warmup_ms /
        1000U;
    detector->pretrigger_samples =
        (size_t) config->audio.sample_rate_hz * config->detector.pretrigger_ms /
        1000U;
    detector->release_samples = 1U;
    detector->cooldown_samples =
        (size_t) config->audio.sample_rate_hz * config->detector.cooldown_ms /
        1000U;
    detector->noise_attack_alpha = 1.0F - expf(
                                              -1000.0F / ((float) config->audio.sample_rate_hz *
                                                          (float) config->detector.noise_attack_ms)
                                          );
    detector->noise_release_alpha = 1.0F - expf(
                                               -1000.0F / ((float) config->audio.sample_rate_hz *
                                                           (float) config->detector.noise_release_ms)
                                           );
    detector->signal_alpha = 1.0F - expf(
                                        -1000.0F / ((float) config->audio.sample_rate_hz *
                                                    (float) config->detector.signal_attack_ms)
                                    );
    detector->trigger_ratio = config->detector.energy_open_ratio;
    detector->release_ratio = config->detector.energy_close_ratio;
    detector->impulse_limit_ratio = config->detector.impulse_limit_ratio;
    detector->maximum_active_samples =
        (size_t) config->audio.sample_rate_hz *
        config->detector.maximum_busy_ms / 1000U;
    if (detector->maximum_active_samples > RTNC_MODEM_MAX_AUDIO_SAMPLES) {
        detector->maximum_active_samples = RTNC_MODEM_MAX_AUDIO_SAMPLES;
    }
    detector->capture_samples = 0U;
    detector->energy_trigger_enabled = true;
    return true;
}

bool rtnc_platform_config_load(const char *filename, rtnc_platform_config_t **config) {
    cyaml_err_t error;
    if (filename == NULL || config == NULL) {
        return false;
    }
    *config = NULL;
    error = cyaml_load_file(filename, &cyaml_config, &platform_schema, (void **) config, NULL);
    if (error != CYAML_OK || !platform_config_is_valid(*config)) {
        rtnc_platform_config_free(*config);
        *config = NULL;
        return false;
    }
    return true;
}

void rtnc_platform_config_free(rtnc_platform_config_t *config) {
    if (config != NULL) {
        cyaml_free(&cyaml_config, &platform_schema, config, 0U);
    }
}
