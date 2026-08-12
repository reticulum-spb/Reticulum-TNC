#include "rtnc/carrier.h"

#include <liquid/liquid.h>
#include <math.h>
#include <stddef.h>

bool rtnc_carrier_init(rtnc_carrier_t *carrier, uint32_t sample_rate_hz, float carrier_hz) {
    float radians_per_sample;
    if (carrier == NULL || sample_rate_hz == 0U || carrier_hz <= 0.0F ||
        carrier_hz >= 0.5F * (float) sample_rate_hz) {
        return false;
    }
    carrier->tx_nco = nco_crcf_create(LIQUID_NCO);
    carrier->rx_nco = nco_crcf_create(LIQUID_NCO);
    if (carrier->tx_nco == NULL || carrier->rx_nco == NULL) {
        rtnc_carrier_deinit(carrier);
        return false;
    }
    radians_per_sample = 2.0F * (float) M_PI * carrier_hz /
                         (float) sample_rate_hz;
    carrier->tx_radians_per_sample = radians_per_sample;
    carrier->rx_radians_per_sample = radians_per_sample;
    carrier->tx_initial_phase = 0.0F;
    carrier->sample_rate_hz = sample_rate_hz;
    (void) nco_crcf_set_frequency((nco_crcf) carrier->tx_nco, radians_per_sample);
    (void) nco_crcf_set_frequency((nco_crcf) carrier->rx_nco, radians_per_sample);
    return true;
}

void rtnc_carrier_deinit(rtnc_carrier_t *carrier) {
    if (carrier == NULL) {
        return;
    }
    if (carrier->tx_nco != NULL) {
        (void) nco_crcf_destroy((nco_crcf) carrier->tx_nco);
    }
    if (carrier->rx_nco != NULL) {
        (void) nco_crcf_destroy((nco_crcf) carrier->rx_nco);
    }
    carrier->tx_nco = NULL;
    carrier->rx_nco = NULL;
    carrier->tx_radians_per_sample = 0.0F;
    carrier->rx_radians_per_sample = 0.0F;
    carrier->tx_initial_phase = 0.0F;
    carrier->sample_rate_hz = 0U;
}

void rtnc_carrier_reset(rtnc_carrier_t *carrier) {
    if (carrier == NULL) {
        return;
    }
    if (carrier->tx_nco != NULL) {
        (void) nco_crcf_reset((nco_crcf) carrier->tx_nco);
        (void) nco_crcf_set_frequency((nco_crcf) carrier->tx_nco, carrier->tx_radians_per_sample);
        (void) nco_crcf_set_phase((nco_crcf) carrier->tx_nco, carrier->tx_initial_phase);
    }
    if (carrier->rx_nco != NULL) {
        (void) nco_crcf_reset((nco_crcf) carrier->rx_nco);
        (void) nco_crcf_set_frequency((nco_crcf) carrier->rx_nco, carrier->rx_radians_per_sample);
    }
}

bool rtnc_carrier_set_tx_frequency_offset(rtnc_carrier_t *carrier, float offset_hz) {
    if (carrier == NULL || carrier->tx_nco == NULL ||
        carrier->sample_rate_hz == 0U) {
        return false;
    }
    carrier->tx_radians_per_sample = carrier->rx_radians_per_sample +
                                     2.0F * (float) M_PI * offset_hz / (float) carrier->sample_rate_hz;
    return true;
}

bool rtnc_carrier_set_tx_phase(rtnc_carrier_t *carrier, float phase_radians) {
    if (carrier == NULL || carrier->tx_nco == NULL || !isfinite(phase_radians)) {
        return false;
    }
    carrier->tx_initial_phase = phase_radians;
    return true;
}

bool rtnc_carrier_upconvert(rtnc_carrier_t *carrier, float complex baseband, float *audio) {
    float complex passband;
    if (carrier == NULL || carrier->tx_nco == NULL || audio == NULL) {
        return false;
    }
    if (nco_crcf_mix_up((nco_crcf) carrier->tx_nco, baseband, &passband) !=
        LIQUID_OK) {
        return false;
    }
    *audio = crealf(passband);
    (void) nco_crcf_step((nco_crcf) carrier->tx_nco);
    return true;
}

bool rtnc_carrier_downconvert(rtnc_carrier_t *carrier, float audio, float complex *baseband) {
    if (carrier == NULL || carrier->rx_nco == NULL || baseband == NULL) {
        return false;
    }
    if (nco_crcf_mix_down((nco_crcf) carrier->rx_nco, audio, baseband) !=
        LIQUID_OK) {
        return false;
    }
    *baseband *= 2.0F;
    (void) nco_crcf_step((nco_crcf) carrier->rx_nco);
    return true;
}
