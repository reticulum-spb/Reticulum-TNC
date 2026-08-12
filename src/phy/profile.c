#include "rtnc/phy.h"

#include <stddef.h>

rtnc_phy_profile_t rtnc_phy_profile_qpsk_1200(void) {
    const rtnc_phy_profile_t profile = {
        .sample_rate_hz = 48000U,
        .modulation = RTNC_MODULATION_QPSK,
        .bits_per_symbol = 2U,
        .symbol_rate_baud = 1200U,
        .carrier_hz = 1650.0F,
        .rrc_rolloff = 0.25F,
        .acquisition_threshold = 0.90F,
        .training_threshold = 0.70F,
        .samples_per_symbol = 40U,
    };
    return profile;
}

bool rtnc_phy_profile_qpsk(uint32_t symbol_rate_baud, float carrier_hz, rtnc_phy_profile_t *profile) {
    return rtnc_phy_profile_psk(RTNC_MODULATION_QPSK, symbol_rate_baud, carrier_hz, profile);
}

bool rtnc_phy_profile_psk(rtnc_modulation_t modulation, uint32_t symbol_rate_baud, float carrier_hz, rtnc_phy_profile_t *profile) {
    rtnc_phy_profile_t candidate;
    if (profile == NULL || symbol_rate_baud == 0U ||
        (48000U % symbol_rate_baud) != 0U) {
        return false;
    }
    candidate = rtnc_phy_profile_qpsk_1200();
    candidate.modulation = modulation;
    if (modulation == RTNC_MODULATION_BPSK) {
        candidate.bits_per_symbol = 1U;
    } else if (modulation == RTNC_MODULATION_QPSK) {
        candidate.bits_per_symbol = 2U;
    } else if (modulation == RTNC_MODULATION_8PSK) {
        candidate.bits_per_symbol = 3U;
    } else if (modulation == RTNC_MODULATION_16PSK) {
        candidate.bits_per_symbol = 4U;
    } else {
        return false;
    }
    candidate.symbol_rate_baud = symbol_rate_baud;
    candidate.samples_per_symbol =
        (uint16_t) (candidate.sample_rate_hz / symbol_rate_baud);
    candidate.carrier_hz = carrier_hz;
    if (!rtnc_phy_profile_is_valid(&candidate)) {
        return false;
    }
    *profile = candidate;
    return true;
}

bool rtnc_phy_profile_is_valid(const rtnc_phy_profile_t *profile) {
    if (profile == NULL || profile->sample_rate_hz == 0U ||
        profile->symbol_rate_baud == 0U) {
        return false;
    }
    if ((profile->sample_rate_hz % profile->symbol_rate_baud) != 0U) {
        return false;
    }
    if (profile->samples_per_symbol !=
        (profile->sample_rate_hz / profile->symbol_rate_baud)) {
        return false;
    }
    if (profile->samples_per_symbol < 20U ||
        profile->samples_per_symbol > 80U) {
        return false;
    }
    if ((profile->modulation == RTNC_MODULATION_BPSK &&
         profile->bits_per_symbol != 1U) ||
        (profile->modulation == RTNC_MODULATION_QPSK &&
         profile->bits_per_symbol != 2U) ||
        (profile->modulation == RTNC_MODULATION_8PSK &&
         profile->bits_per_symbol != 3U) ||
        (profile->modulation == RTNC_MODULATION_16PSK &&
         profile->bits_per_symbol != 4U) ||
        (profile->modulation != RTNC_MODULATION_BPSK &&
         profile->modulation != RTNC_MODULATION_QPSK &&
         profile->modulation != RTNC_MODULATION_8PSK &&
         profile->modulation != RTNC_MODULATION_16PSK)) {
        return false;
    }
    if (profile->rrc_rolloff <= 0.0F || profile->rrc_rolloff > 1.0F) {
        return false;
    }
    if (profile->acquisition_threshold <= 0.0F ||
        profile->acquisition_threshold > 1.0F ||
        profile->training_threshold <= 0.0F ||
        profile->training_threshold > 1.0F) {
        return false;
    }
    return profile->carrier_hz > 300.0F && profile->carrier_hz < 3000.0F;
}
