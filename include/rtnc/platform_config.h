#ifndef RTNC_PLATFORM_CONFIG_H
#define RTNC_PLATFORM_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#include "rtnc/burst_detector.h"
#include "rtnc/phy.h"
#include "rtnc/tx_eq.h"

typedef struct {
    uint8_t port;
    uint8_t pin;
    bool    active_high;
} rtnc_gpio_config_t;

typedef struct {
    char    *device;
    uint32_t sample_rate_hz;
    uint16_t period_ms;
    uint16_t periods;
} rtnc_audio_config_t;

typedef struct {
    char *profile;
    /** Optional second RX profile; NULL disables its detector. */
    char *control_profile;
} rtnc_modem_config_t;

typedef struct {
    char    *name;
    char    *modulation;
    uint32_t symbol_rate_baud;
    float    carrier_hz;
    float    rrc_rolloff;
    float    acquisition_threshold;
    float    training_threshold;
    float    detector_bandwidth_margin;
    uint8_t  fec_mode;
    uint8_t  payload_class_bytes;
} rtnc_phy_profile_config_t;

typedef struct {
    uint16_t lead_ms;
    uint16_t tail_ms;
    float    filter_gain;
    float    response_eq_taps[RTNC_TX_EQ_TAP_COUNT];
} rtnc_tx_config_t;

typedef struct {
    uint16_t mtu;
    uint32_t reassembly_timeout_ms;
} rtnc_link_config_t;

typedef struct {
    uint8_t dsp_queue_blocks;
    uint8_t decode_queue_frames;
    uint8_t equalizer_queue_frames;
    bool    parallel_equalizer;
    uint8_t equalizer_nice;
} rtnc_worker_config_t;

typedef struct {
    uint16_t warmup_ms;
    uint16_t pretrigger_ms;
    uint16_t cooldown_ms;
    uint16_t noise_attack_ms;
    uint16_t noise_release_ms;
    uint16_t signal_attack_ms;
    uint16_t maximum_busy_ms;
    float    energy_open_ratio;
    float    energy_close_ratio;
    float    impulse_limit_ratio;
} rtnc_detector_config_t;

typedef struct {
    uint8_t  tx_queue_packets;
    uint8_t  rx_queue_packets;
    uint16_t channel_busy_timeout_ms;
    uint16_t rx_guard_ms;
    uint16_t kiss_tcp_port;
} rtnc_runtime_config_t;

typedef struct {
    rtnc_gpio_config_t         ptt;
    rtnc_audio_config_t        audio;
    rtnc_modem_config_t        modem;
    rtnc_tx_config_t           tx;
    rtnc_link_config_t         link;
    rtnc_worker_config_t       workers;
    rtnc_detector_config_t     detector;
    rtnc_runtime_config_t      runtime;
    rtnc_phy_profile_config_t *profiles;
    unsigned int               profiles_count;
} rtnc_platform_config_t;

/** Load and validate a platform YAML file using libcyaml. */
bool                             rtnc_platform_config_load(const char *filename, rtnc_platform_config_t **config);
void                             rtnc_platform_config_free(rtnc_platform_config_t *config);
bool                             rtnc_platform_phy_profile_named(const rtnc_platform_config_t *config, const char *name, rtnc_phy_profile_t *profile);
const rtnc_phy_profile_config_t *rtnc_platform_selected_profile(
    const rtnc_platform_config_t *config
);
const rtnc_phy_profile_config_t *rtnc_platform_profile_config_named(
    const rtnc_platform_config_t *config,
    const char                   *name
);
/** Convert validated millisecond YAML settings to detector sample counts. */
bool rtnc_platform_burst_config(const rtnc_platform_config_t *config, rtnc_burst_detector_config_t *detector);

#endif
