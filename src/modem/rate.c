#include "rtnc/modem.h"

#include "rtnc/fragmentation.h"

#include <stdint.h>

static size_t symbol_count(size_t byte_count, uint8_t bits_per_symbol) {
    return (byte_count * 8U + (size_t) bits_per_symbol - 1U) /
           (size_t) bits_per_symbol;
}

bool rtnc_modem_profile_rate(const rtnc_phy_profile_t *profile, fec_mode_t fec_mode, uint8_t payload_class_bytes, rtnc_modem_rate_t *rate) {
    size_t   protected_bytes;
    size_t   encoded_bytes;
    size_t   symbols;
    uint64_t frame_samples;
    uint64_t raw_bitrate;
    uint64_t fec_bitrate;
    uint64_t interface_bits_per_second;

    if (rate == NULL || !rtnc_phy_profile_is_valid(profile) ||
        (payload_class_bytes != 64U && payload_class_bytes != 128U) ||
        (fec_mode != FEC_NONE && fec_mode != FEC_LDPC_ROBUST &&
         fec_mode != FEC_LDPC_NORMAL)) {
        return false;
    }

    protected_bytes = RTNC_FRAME_HEADER_SIZE +
                      (size_t) payload_class_bytes + RTNC_FRAME_CRC_SIZE;
    encoded_bytes = rtnc_fec_encoded_size(fec_mode, protected_bytes);
    if (encoded_bytes == 0U) {
        return false;
    }
    symbols = RTNC_MODEM_TRAINING_SYMBOLS +
              symbol_count(encoded_bytes, profile->bits_per_symbol) +
              RTNC_MODEM_TRAILING_SYMBOLS;
    frame_samples = (uint64_t) symbols * profile->samples_per_symbol;
    raw_bitrate = (uint64_t) profile->symbol_rate_baud *
                  profile->bits_per_symbol;
    if (fec_mode == FEC_LDPC_ROBUST) {
        fec_bitrate = raw_bitrate / 2U;
    } else if (fec_mode == FEC_LDPC_NORMAL) {
        fec_bitrate = raw_bitrate * 2U / 3U;
    } else {
        fec_bitrate = raw_bitrate;
    }
    interface_bits_per_second =
        ((uint64_t) payload_class_bytes - RTNC_FRAGMENT_HEADER_SIZE) * 8U *
        profile->sample_rate_hz / frame_samples;
    if (frame_samples > RTNC_MODEM_MAX_AUDIO_SAMPLES ||
        frame_samples > SIZE_MAX || raw_bitrate > UINT32_MAX ||
        fec_bitrate > UINT32_MAX ||
        interface_bits_per_second > UINT32_MAX) {
        return false;
    }

    rate->raw_bitrate_bps = (uint32_t) raw_bitrate;
    rate->fec_bitrate_bps = (uint32_t) fec_bitrate;
    rate->interface_bitrate_bps = (uint32_t) interface_bits_per_second;
    rate->frame_samples = (size_t) frame_samples;
    return true;
}
