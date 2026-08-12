#include "rtnc/modem.h"
#include "rtnc/phy.h"

#include <assert.h>
#include <stddef.h>

int main(void) {
    rtnc_phy_profile_t    profile = rtnc_phy_profile_qpsk_1200();
    rtnc_modem_rate_t     profile_rate;
    static const uint32_t rates[] = { 1200U, 1500U, 1600U, 1920U, 2000U, 2400U };
    size_t                index;

    assert(profile.sample_rate_hz == 48000U);
    assert(profile.modulation == RTNC_MODULATION_QPSK);
    assert(profile.bits_per_symbol == 2U);
    assert(profile.symbol_rate_baud == 1200U);
    assert(profile.samples_per_symbol == 40U);
    assert(profile.carrier_hz == 1650.0F);
    assert(profile.rrc_rolloff == 0.25F);
    assert(profile.acquisition_threshold == 0.90F);
    assert(profile.training_threshold == 0.70F);
    assert(rtnc_phy_profile_is_valid(&profile));
    assert(!rtnc_phy_profile_is_valid(NULL));
    assert(rtnc_modem_profile_rate(&profile, FEC_LDPC_ROBUST, 64U, &profile_rate));
    assert(profile_rate.raw_bitrate_bps == 2400U);
    assert(profile_rate.fec_bitrate_bps == 1200U);
    assert(profile_rate.interface_bitrate_bps == 879U);
    assert(profile_rate.frame_samples == 27520U);

    profile.samples_per_symbol = 39U;
    assert(!rtnc_phy_profile_is_valid(&profile));
    profile = rtnc_phy_profile_qpsk_1200();
    profile.rrc_rolloff = 1.25F;
    assert(!rtnc_phy_profile_is_valid(&profile));
    profile = rtnc_phy_profile_qpsk_1200();
    profile.acquisition_threshold = 0.0F;
    assert(!rtnc_phy_profile_is_valid(&profile));

    for (index = 0U; index < sizeof(rates) / sizeof(rates[0]); ++index) {
        assert(rtnc_phy_profile_qpsk(rates[index], 1650.0F, &profile));
        assert(profile.symbol_rate_baud == rates[index]);
        assert(profile.samples_per_symbol == 48000U / rates[index]);
    }
    assert(!rtnc_phy_profile_qpsk(1800U, 1650.0F, &profile));
    assert(!rtnc_phy_profile_qpsk(3000U, 1650.0F, &profile));
    assert(rtnc_phy_profile_psk(RTNC_MODULATION_QPSK, 800U, 1650.0F, &profile));
    assert(profile.samples_per_symbol == 60U);
    assert(rtnc_phy_profile_psk(RTNC_MODULATION_BPSK, 750U, 1650.0F, &profile));
    assert(profile.bits_per_symbol == 1U);
    assert(profile.samples_per_symbol == 64U);
    assert(rtnc_phy_profile_psk(RTNC_MODULATION_8PSK, 600U, 1650.0F, &profile));
    assert(profile.bits_per_symbol == 3U);
    assert(profile.samples_per_symbol == 80U);
    assert(!rtnc_phy_profile_psk((rtnc_modulation_t) 99, 800U, 1650.0F, &profile));

    assert(rtnc_phy_profile_psk(RTNC_MODULATION_8PSK, 1000U, 1650.0F, &profile));
    assert(rtnc_modem_profile_rate(&profile, FEC_LDPC_ROBUST, 64U, &profile_rate));
    assert(profile_rate.raw_bitrate_bps == 3000U);
    assert(profile_rate.fec_bitrate_bps == 1500U);
    assert(profile_rate.interface_bitrate_bps == 1016U);
    assert(profile_rate.frame_samples == 23808U);
    assert(!rtnc_modem_profile_rate(&profile, FEC_LDPC_FAST, 64U, &profile_rate));
    assert(!rtnc_modem_profile_rate(&profile, FEC_LDPC_ROBUST, 65U, &profile_rate));
    assert(rtnc_phy_profile_psk(RTNC_MODULATION_16PSK, 1000U, 1650.0F, &profile));
    assert(profile.bits_per_symbol == 4U);
    assert(rtnc_modem_profile_rate(&profile, FEC_LDPC_ROBUST, 64U, &profile_rate));
    assert(profile_rate.raw_bitrate_bps == 4000U);

    return 0;
}
