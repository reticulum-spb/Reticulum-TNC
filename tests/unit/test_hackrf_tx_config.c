#include "rtnc/hackrf_tx.h"

#include <assert.h>

int main(void) {
    rtnc_hackrf_tx_config_t config;
    rtnc_hackrf_tx_default_config(&config);
    assert(rtnc_hackrf_tx_config_is_valid(&config));
    assert(config.frequency_hz == 446006250U);
    assert(config.iq_sample_rate_hz == 2400000U);
    assert(config.audio_sample_rate_hz == 48000U);
    assert(config.txvga_gain_db == 20U);
    assert(config.iq_amplitude == 100);

    config.iq_sample_rate_hz = 2300000U;
    assert(!rtnc_hackrf_tx_config_is_valid(&config));
    config.iq_sample_rate_hz = 2400000U;
    config.txvga_gain_db = 48U;
    assert(!rtnc_hackrf_tx_config_is_valid(&config));
    config.txvga_gain_db = 20U;
    config.deviation_hz = 499.0F;
    assert(!rtnc_hackrf_tx_config_is_valid(&config));
    return 0;
}
