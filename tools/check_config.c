#include "rtnc/modem.h"
#include "rtnc/platform_config.h"

#include <stdio.h>

int main(int argc, char **argv) {
    rtnc_platform_config_t          *config = NULL;
    const rtnc_phy_profile_config_t *profile;
    rtnc_phy_profile_t               phy_profile;
    rtnc_modem_rate_t                rate;
    if (argc < 2 || argc > 3) {
        (void) fprintf(stderr, "usage: %s CONFIG.yaml [PROFILE]\n", argv[0]);
        return 2;
    }
    if (!rtnc_platform_config_load(argv[1], &config)) {
        (void) fprintf(stderr, "invalid configuration: %s\n", argv[1]);
        return 1;
    }
    profile = argc == 3
                  ? rtnc_platform_profile_config_named(config, argv[2])
                  : rtnc_platform_selected_profile(config);
    if (profile == NULL ||
        !rtnc_platform_phy_profile_named(config, profile->name, &phy_profile) ||
        !rtnc_modem_profile_rate(
            &phy_profile,
            (fec_mode_t) profile->fec_mode,
            profile->payload_class_bytes,
            &rate
        )) {
        (void) fprintf(stderr, "selected profile is missing\n");
        rtnc_platform_config_free(config);
        return 1;
    }
    (void) printf("audio=%s rate=%u period_ms=%u periods=%u ptt=%u:%u "
                  "active_high=%d profile=%s modulation=%s fec=%u payload=%u baud=%u "
                  "raw_bitrate_bps=%u fec_bitrate_bps=%u interface_bitrate_bps=%u "
                  "frame_airtime_ms=%.3f "
                  "carrier=%.1f "
                  "tx_filter_gain=%.3f tx_eq_taps=%u mtu=%u "
                  "reassembly_timeout_ms=%u runtime_tx_queue=%u "
                  "runtime_rx_queue=%u busy_timeout_ms=%u rx_guard_ms=%u "
                  "kiss_tcp_port=%u "
                  "dsp_queue=%u "
                  "decode_queue=%u eq_queue=%u parallel_eq=%d eq_nice=%u\n",
                  config->audio.device,
                  config->audio.sample_rate_hz,
                  (unsigned int) config->audio.period_ms,
                  (unsigned int) config->audio.periods,
                  (unsigned int) config->ptt.port,
                  (unsigned int) config->ptt.pin,
                  config->ptt.active_high ? 1 : 0,
                  argc == 3 ? argv[2] : config->modem.profile,
                  profile->modulation,
                  (unsigned int) profile->fec_mode,
                  (unsigned int) profile->payload_class_bytes,
                  profile->symbol_rate_baud,
                  rate.raw_bitrate_bps,
                  rate.fec_bitrate_bps,
                  rate.interface_bitrate_bps,
                  1000.0 * (double) rate.frame_samples / (double) phy_profile.sample_rate_hz,
                  (double) profile->carrier_hz,
                  (double) config->tx.filter_gain,
                  (unsigned int) RTNC_TX_EQ_TAP_COUNT,
                  (unsigned int) config->link.mtu,
                  (unsigned int) config->link.reassembly_timeout_ms,
                  (unsigned int) config->runtime.tx_queue_packets,
                  (unsigned int) config->runtime.rx_queue_packets,
                  (unsigned int) config->runtime.channel_busy_timeout_ms,
                  (unsigned int) config->runtime.rx_guard_ms,
                  (unsigned int) config->runtime.kiss_tcp_port,
                  (unsigned int) config->workers.dsp_queue_blocks,
                  (unsigned int) config->workers.decode_queue_frames,
                  (unsigned int) config->workers.equalizer_queue_frames,
                  config->workers.parallel_equalizer ? 1 : 0,
                  (unsigned int) config->workers.equalizer_nice);
    (void) printf("detector warmup_ms=%u pretrigger_ms=%u cooldown_ms=%u "
                  "noise_attack_ms=%u noise_release_ms=%u signal_attack_ms=%u "
                  "maximum_busy_ms=%u open=%.3f close=%.3f impulse=%.3f "
                  "acquisition=%.3f bandwidth_margin=%.3f\n",
                  (unsigned int) config->detector.warmup_ms,
                  (unsigned int) config->detector.pretrigger_ms,
                  (unsigned int) config->detector.cooldown_ms,
                  (unsigned int) config->detector.noise_attack_ms,
                  (unsigned int) config->detector.noise_release_ms,
                  (unsigned int) config->detector.signal_attack_ms,
                  (unsigned int) config->detector.maximum_busy_ms,
                  (double) config->detector.energy_open_ratio,
                  (double) config->detector.energy_close_ratio,
                  (double) config->detector.impulse_limit_ratio,
                  (double) profile->acquisition_threshold,
                  (double) profile->detector_bandwidth_margin);
    rtnc_platform_config_free(config);
    return 0;
}
