#include "rtnc/modem.h"

#include <math.h>
#include <string.h>

static const uint8_t acquisition_symbols[RTNC_MODEM_ACQUISITION_SYMBOLS] = {
    0U,
    3U,
    1U,
    1U,
    2U,
    0U,
    3U,
    2U,
    1U,
    0U,
    2U,
    3U,
    3U,
    0U,
    1U,
    2U,
    2U,
    1U,
    3U,
    0U,
    1U,
    3U,
    2U,
    2U,
    0U,
    1U,
    0U,
    3U,
    2U,
    3U,
    1U,
    0U,
};

static const uint8_t control_acquisition_symbols[RTNC_MODEM_ACQUISITION_SYMBOLS] = {
    3U,
    0U,
    2U,
    1U,
    0U,
    2U,
    2U,
    3U,
    1U,
    3U,
    0U,
    1U,
    2U,
    1U,
    0U,
    3U,
    0U,
    1U,
    3U,
    3U,
    2U,
    0U,
    1U,
    2U,
    3U,
    2U,
    1U,
    0U,
    1U,
    3U,
    2U,
    0U,
};

static size_t payload_symbol_count(size_t byte_count, uint8_t bits_per_symbol) {
    return (byte_count * 8U + (size_t) bits_per_symbol - 1U) /
           (size_t) bits_per_symbol;
}

static uint8_t read_symbol_bits(const uint8_t *bytes, size_t bit_count, size_t symbol_index, uint8_t bits_per_symbol) {
    unsigned int symbol = 0U;
    uint8_t      bit;
    for (bit = 0U; bit < bits_per_symbol; ++bit) {
        const size_t source_bit = symbol_index * bits_per_symbol + bit;
        symbol = symbol << 1U;
        if (source_bit < bit_count) {
            symbol |= ((unsigned int) bytes[source_bit / 8U] >>
                       (7U - (unsigned int) (source_bit % 8U))) &
                      1U;
        }
    }
    return (uint8_t) symbol;
}

static bool emit_symbol(rtnc_modem_t *modem, float complex symbol, float *audio, size_t *write_index) {
    float complex shaped[80];
    size_t        index;
    if (modem->profile.samples_per_symbol > 80U ||
        !rtnc_rrc_interpolate(&modem->rrc, symbol, shaped)) {
        return false;
    }
    for (index = 0U; index < modem->profile.samples_per_symbol; ++index) {
        if (!rtnc_carrier_upconvert(&modem->carrier, shaped[index], &audio[*write_index])) {
            return false;
        }
        ++(*write_index);
    }
    return true;
}

bool rtnc_modem_init(rtnc_modem_t *modem) {
    return rtnc_modem_init_config(modem, FEC_NONE, RTNC_FRAME_MAX_PAYLOAD);
}

bool rtnc_modem_init_config(rtnc_modem_t *modem, fec_mode_t fec_mode, uint8_t payload_class_bytes) {
    const rtnc_phy_profile_t profile = rtnc_phy_profile_qpsk_1200();
    return rtnc_modem_init_profile(modem, fec_mode, payload_class_bytes, &profile);
}

bool rtnc_modem_init_profile(rtnc_modem_t *modem, fec_mode_t fec_mode, uint8_t payload_class_bytes, const rtnc_phy_profile_t *profile) {
    return rtnc_modem_init_profile_preamble(modem, fec_mode, payload_class_bytes, profile, RTNC_PREAMBLE_DATA);
}

bool rtnc_modem_init_profile_preamble(rtnc_modem_t *modem, fec_mode_t fec_mode, uint8_t payload_class_bytes, const rtnc_phy_profile_t *profile, rtnc_preamble_t preamble) {
    size_t   index;
    uint32_t bpsk_training_state = preamble == RTNC_PREAMBLE_DATA
                                       ? UINT32_C(0x9e3779b9)
                                       : UINT32_C(0x243f6a88);
    if (modem == NULL || !rtnc_phy_profile_is_valid(profile) ||
        (payload_class_bytes != 64U && payload_class_bytes != 128U) ||
        (fec_mode != FEC_NONE && fec_mode != FEC_LDPC_ROBUST &&
         fec_mode != FEC_LDPC_NORMAL) ||
        (preamble != RTNC_PREAMBLE_DATA &&
         preamble != RTNC_PREAMBLE_CONTROL)) {
        return false;
    }
    if (profile->modulation != RTNC_MODULATION_BPSK &&
        profile->modulation != RTNC_MODULATION_QPSK && fec_mode == FEC_NONE) {
        return false;
    }
    (void) memset(modem, 0, sizeof(*modem));
    modem->profile = *profile;
    modem->fec_mode = fec_mode;
    modem->payload_class_bytes = payload_class_bytes;
    modem->preamble = preamble;
    if (!rtnc_psk_init(&modem->psk, modem->profile.modulation) ||
        !rtnc_rrc_init(&modem->rrc, modem->profile.samples_per_symbol, RTNC_MODEM_RRC_DELAY_SYMBOLS, modem->profile.rrc_rolloff) ||
        !rtnc_timing_init(&modem->timing, modem->profile.samples_per_symbol, RTNC_MODEM_RRC_DELAY_SYMBOLS, modem->profile.rrc_rolloff, 0.03F) ||
        !rtnc_equalizer_init(&modem->equalizer, RTNC_MODEM_EQUALIZER_TAPS, 0.95F) ||
        !rtnc_carrier_init(&modem->carrier, modem->profile.sample_rate_hz, modem->profile.carrier_hz)) {
        rtnc_modem_deinit(modem);
        return false;
    }
    for (index = 0U; index < RTNC_MODEM_TRAINING_SYMBOLS; ++index) {
        uint8_t value;
        if (modem->profile.bits_per_symbol == 1U) {
            bpsk_training_state ^= bpsk_training_state << 13U;
            bpsk_training_state ^= bpsk_training_state >> 17U;
            bpsk_training_state ^= bpsk_training_state << 5U;
            value = (uint8_t) (bpsk_training_state & 1U);
        } else if (index < RTNC_MODEM_ACQUISITION_SYMBOLS) {
            const uint8_t *sequence = preamble == RTNC_PREAMBLE_DATA
                                          ? acquisition_symbols
                                          : control_acquisition_symbols;
            value = (uint8_t) (sequence[index] *
                               (modem->profile.bits_per_symbol - 1U));
        } else {
            uint32_t state = (uint32_t) index + 1U;
            state ^= state << 13U;
            state ^= state >> 17U;
            state ^= state << 5U;
            value = (uint8_t) (state &
                               ((1U << modem->profile.bits_per_symbol) - 1U));
        }
        if (!rtnc_psk_map(&modem->psk, value, &modem->training[index])) {
            rtnc_modem_deinit(modem);
            return false;
        }
    }
    return true;
}

void rtnc_modem_deinit(rtnc_modem_t *modem) {
    if (modem == NULL) {
        return;
    }
    rtnc_carrier_deinit(&modem->carrier);
    rtnc_timing_deinit(&modem->timing);
    rtnc_equalizer_deinit(&modem->equalizer);
    rtnc_rrc_deinit(&modem->rrc);
    rtnc_psk_deinit(&modem->psk);
}

size_t rtnc_modem_frame_samples(const rtnc_modem_t *modem) {
    rtnc_modem_rate_t rate;
    if (modem == NULL ||
        !rtnc_modem_profile_rate(
            &modem->profile,
            modem->fec_mode,
            modem->payload_class_bytes,
            &rate
        )) {
        return 0U;
    }
    return rate.frame_samples;
}

rtnc_modem_status_t rtnc_modem_tx_audio(rtnc_modem_t *modem, const uint8_t *payload, size_t payload_length, float *audio, size_t capacity, size_t *sample_count) {
    uint8_t frame[RTNC_FRAME_MAX_ENCODED_SIZE];
    uint8_t protected[RTNC_FRAME_MAX_ENCODED_SIZE] = { 0U };
    uint8_t fec_encoded[RTNC_MODEM_MAX_FEC_BYTES];
    size_t  frame_length = 0U;
    size_t  fec_encoded_length = 0U;
    size_t  protected_length;
    size_t  required_symbols;
    size_t  write_index = 0U;
    size_t  index;
    if (modem == NULL || audio == NULL || sample_count == NULL ||
        (payload == NULL && payload_length != 0U)) {
        return RTNC_MODEM_INVALID_ARGUMENT;
    }
    if (payload_length == 0U || payload_length > modem->payload_class_bytes) {
        return RTNC_MODEM_FRAME_REJECTED;
    }
    if (rtnc_frame_build(payload, payload_length, frame, sizeof(frame), &frame_length) != RTNC_FRAME_OK) {
        return RTNC_MODEM_FRAME_REJECTED;
    }
    protected_length = modem->fec_mode == FEC_NONE
                           ? frame_length
                           : RTNC_FRAME_HEADER_SIZE +
                                 (size_t) modem->payload_class_bytes +
                                 RTNC_FRAME_CRC_SIZE;
    (void) memcpy(protected, frame, frame_length);
    if (rtnc_fec_encode(modem->fec_mode, protected, protected_length, fec_encoded, sizeof(fec_encoded), &fec_encoded_length) != RTNC_FEC_OK) {
        return RTNC_MODEM_FRAME_REJECTED;
    }
    required_symbols = RTNC_MODEM_TRAINING_SYMBOLS +
                       payload_symbol_count(fec_encoded_length, modem->profile.bits_per_symbol) +
                       RTNC_MODEM_TRAILING_SYMBOLS;
    if (required_symbols * modem->profile.samples_per_symbol > capacity) {
        return RTNC_MODEM_BUFFER_TOO_SMALL;
    }
    rtnc_rrc_reset(&modem->rrc);
    rtnc_carrier_reset(&modem->carrier);
    for (index = 0U; index < RTNC_MODEM_TRAINING_SYMBOLS; ++index) {
        if (!emit_symbol(modem, modem->training[index], audio, &write_index)) {
            return RTNC_MODEM_DSP_ERROR;
        }
    }
    for (index = 0U;
         index < payload_symbol_count(fec_encoded_length, modem->profile.bits_per_symbol);
         ++index) {
        const uint8_t symbol = read_symbol_bits(
            fec_encoded,
            fec_encoded_length * 8U,
            index,
            modem->profile.bits_per_symbol
        );
        float complex mapped;
        if (!rtnc_psk_map(&modem->psk, symbol, &mapped) ||
            !emit_symbol(modem, mapped, audio, &write_index)) {
            return RTNC_MODEM_DSP_ERROR;
        }
    }
    for (index = 0U; index < RTNC_MODEM_TRAILING_SYMBOLS; ++index) {
        if (!emit_symbol(modem, 0.0F, audio, &write_index)) {
            return RTNC_MODEM_DSP_ERROR;
        }
    }
    *sample_count = write_index;
    return RTNC_MODEM_OK;
}

static bool decode_bytes(rtnc_modem_t *modem, const rtnc_modem_workspace_t *workspace, size_t start, uint8_t *bytes, size_t byte_count, float *evm_square_sum, float *llr, size_t *llr_index, float tracking_gain, float *tracking_phase, float *tracking_frequency) {
    const size_t bit_count = byte_count * 8U;
    const size_t symbol_count = payload_symbol_count(
        byte_count,
        modem->profile.bits_per_symbol
    );
    size_t symbol_index;
    (void) memset(bytes, 0, byte_count);
    for (symbol_index = 0U; symbol_index < symbol_count; ++symbol_index) {
        uint8_t             symbol;
        float               symbol_llr[4];
        float               evm;
        const float complex corrected =
            workspace->recovered[start + symbol_index] / tracking_gain *
            cexpf(-I * *tracking_phase);
        uint8_t bit;
        if (!rtnc_psk_demap_soft(&modem->psk, corrected, &symbol, symbol_llr, &evm)) {
            return false;
        }
        *tracking_phase += *tracking_frequency;
        for (bit = 0U; bit < modem->profile.bits_per_symbol; ++bit) {
            const size_t destination_bit =
                symbol_index * modem->profile.bits_per_symbol + bit;
            if (destination_bit < bit_count) {
                const uint8_t hard_bit = (uint8_t) (((unsigned int) symbol >>
                                                     (modem->profile.bits_per_symbol - 1U - bit)) &
                                                    1U);
                bytes[destination_bit / 8U] |=
                    (uint8_t) (hard_bit << (7U - destination_bit % 8U));
                llr[*llr_index] = symbol_llr[bit];
                ++(*llr_index);
            }
        }
        *evm_square_sum += evm * evm;
    }
    return true;
}

static float acquisition_score_at(const rtnc_modem_t *modem, const rtnc_modem_workspace_t *workspace, size_t start, float *phase) {
    float complex correlation = 0.0F;
    float         energy = 0.0F;
    size_t        training_index;
    for (training_index = 0U;
         training_index < RTNC_MODEM_ACQUISITION_SYMBOLS;
         ++training_index) {
        const float complex received = workspace->filtered[start + training_index * modem->profile.samples_per_symbol];
        correlation += conjf(modem->training[training_index]) * received;
        energy += crealf(received * conjf(received));
    }
    if (energy <= 0.0F) {
        *phase = 0.0F;
        return 0.0F;
    }
    *phase = cargf(correlation);
    return cabsf(correlation) /
           sqrtf((float) RTNC_MODEM_ACQUISITION_SYMBOLS * energy);
}

static rtnc_modem_status_t decode_equalizer_fallback(
    rtnc_modem_t           *modem,
    const float            *audio,
    size_t                  sample_count,
    size_t                  coarse_start,
    float                   phase_at_training_start,
    float                   radians_per_sample,
    uint8_t                *payload,
    size_t                  capacity,
    size_t                 *payload_length,
    rtnc_sync_metrics_t    *metrics,
    rtnc_modem_workspace_t *workspace,
    bool                    decision_directed
) {
    uint8_t      frame[RTNC_MODEM_MAX_PROTECTED_BYTES];
    uint8_t      hard_encoded[RTNC_MODEM_MAX_FEC_BYTES];
    const size_t protected_bytes = RTNC_FRAME_HEADER_SIZE +
                                   (size_t) modem->payload_class_bytes +
                                   RTNC_FRAME_CRC_SIZE;
    const size_t encoded_bytes =
        rtnc_fec_encoded_size(modem->fec_mode, protected_bytes);
    const size_t required_symbols = payload_symbol_count(
        encoded_bytes,
        modem->profile.bits_per_symbol
    );
    size_t              recovered_count = 0U;
    size_t              best_start = 0U;
    size_t              search_limit;
    float               best_score = 0.0F;
    size_t              index;
    size_t              equalized_count = 0U;
    float               equalizer_input_scale = 1.0F;
    float               training_error_sum = 0.0F;
    float               evm_square_sum = 0.0F;
    size_t              llr_index = 0U;
    size_t              fec_output_bytes = 0U;
    rtnc_modem_status_t result = RTNC_MODEM_FRAME_REJECTED;

    if (encoded_bytes == 0U ||
        !rtnc_timing_set_output_rate(&modem->timing, 2U)) {
        return RTNC_MODEM_DSP_ERROR;
    }
    rtnc_carrier_reset(&modem->carrier);
    rtnc_timing_reset(&modem->timing);
    for (index = 0U; index < sample_count; ++index) {
        float complex baseband;
        float complex output[4];
        size_t        produced = 0U;
        size_t        output_index;
        const float   correction =
            phase_at_training_start +
            radians_per_sample * ((float) index - (float) coarse_start);
        if (!rtnc_carrier_downconvert(&modem->carrier, audio[index], &baseband) ||
            !rtnc_timing_execute(&modem->timing, baseband * cexpf(-I * correction), output, 4U, &produced) ||
            recovered_count + produced > RTNC_MODEM_MAX_RECOVERED_SYMBOLS) {
            result = RTNC_MODEM_DSP_ERROR;
            goto cleanup;
        }
        for (output_index = 0U; output_index < produced; ++output_index) {
            workspace->recovered[recovered_count++] = output[output_index];
        }
    }
    search_limit = 2U * (RTNC_MODEM_TRAINING_SYMBOLS + 40U) +
                   (size_t) ceilf(metrics->timing_symbols * 2.0F);
    if (search_limit > recovered_count) {
        search_limit = recovered_count;
    }
    for (index = 0U;
         index + 2U * RTNC_MODEM_TIMING_TRAINING_SYMBOLS <= search_limit;
         ++index) {
        float complex correlation = 0.0F;
        float         energy = 0.0F;
        size_t        training_index;
        for (training_index = 0U;
             training_index < RTNC_MODEM_TIMING_TRAINING_SYMBOLS;
             ++training_index) {
            const float complex received =
                workspace->recovered[index + 2U * training_index];
            correlation +=
                conjf(modem->training[RTNC_MODEM_TIMING_WARMUP_SYMBOLS + training_index]) * received;
            energy += crealf(received * conjf(received));
        }
        if (energy > 0.0F) {
            const float score = cabsf(correlation) /
                                sqrtf((float) RTNC_MODEM_TIMING_TRAINING_SYMBOLS * energy);
            if (score > best_score) {
                best_score = score;
                best_start = index;
            }
        }
    }
    if (best_score < modem->profile.training_threshold) {
        result = RTNC_MODEM_NO_FRAME;
        goto cleanup;
    }
    {
        const size_t data_start =
            best_start + 2U * RTNC_MODEM_TIMING_TRAINING_SYMBOLS;
        const size_t first_pair = best_start & 1U;
        float        input_energy = 0.0F;
        size_t       input_count = 0U;
        size_t       pair;
        for (pair = first_pair; pair + 1U < recovered_count && pair < data_start;
             pair += 2U) {
            if (pair >= best_start) {
                input_energy += crealf(workspace->recovered[pair] * conjf(workspace->recovered[pair]));
                input_energy += crealf(workspace->recovered[pair + 1U] * conjf(workspace->recovered[pair + 1U]));
                input_count += 2U;
            }
        }
        if (input_count == 0U || input_energy <= 1.0e-12F) {
            result = RTNC_MODEM_DSP_ERROR;
            goto cleanup;
        }
        equalizer_input_scale = sqrtf((float) input_count / input_energy);
        rtnc_equalizer_reset(&modem->equalizer);
        for (pair = first_pair; pair + 1U < recovered_count; pair += 2U) {
            const float complex scaled_samples[2] = {
                workspace->recovered[pair] * equalizer_input_scale,
                workspace->recovered[pair + 1U] * equalizer_input_scale,
            };
            float complex equalized;
            float         error;
            const bool    training = pair >= best_start && pair < data_start;
            const size_t  training_index =
                training ? (pair - best_start) / 2U : 0U;
            const float complex desired =
                training
                    ? modem->training[RTNC_MODEM_TIMING_WARMUP_SYMBOLS + training_index]
                    : 0.0F;
            if (!rtnc_equalizer_execute(&modem->equalizer, scaled_samples, training, desired, &equalized, &error)) {
                result = RTNC_MODEM_DSP_ERROR;
                goto cleanup;
            }
            if (training) {
                training_error_sum += error;
            } else if (pair >= data_start && equalized_count < required_symbols) {
                uint8_t       decision;
                float         decision_llr[4];
                float         decision_error;
                float complex desired_decision;
                if (decision_directed &&
                    rtnc_psk_demap_soft(&modem->psk, equalized, &decision, decision_llr, &decision_error) &&
                    decision_error < 0.50F &&
                    rtnc_psk_map(&modem->psk, decision, &desired_decision) &&
                    !rtnc_equalizer_adapt(&modem->equalizer, desired_decision, equalized)) {
                    result = RTNC_MODEM_DSP_ERROR;
                    goto cleanup;
                }
                workspace->recovered[equalized_count++] = equalized;
            }
        }
    }
    if (equalized_count < required_symbols) {
        result = RTNC_MODEM_TRUNCATED_FRAME;
        goto cleanup;
    }
    metrics->equalizer_used = true;
    metrics->equalizer_training_error =
        training_error_sum / (float) RTNC_MODEM_TIMING_TRAINING_SYMBOLS;
    if (!rtnc_equalizer_copy_taps(&modem->equalizer, metrics->equalizer_taps, RTNC_EQUALIZER_DIAGNOSTIC_TAPS)) {
        result = RTNC_MODEM_DSP_ERROR;
        goto cleanup;
    }
    if (!decode_bytes(modem, workspace, 0U, hard_encoded, encoded_bytes, &evm_square_sum, workspace->llr, &llr_index, 1.0F, &(float) { 0.0F }, &(float) { 0.0F })) {
        result = RTNC_MODEM_DSP_ERROR;
        goto cleanup;
    }
    workspace->llr_count = llr_index;
    metrics->evm_rms =
        sqrtf(evm_square_sum / (float) required_symbols);
    metrics->training_snr_db =
        -20.0F * log10f(fmaxf(metrics->evm_rms, 1.0e-6F));
    if (rtnc_fec_decode(modem->fec_mode, workspace->llr, workspace->llr_count, frame, sizeof(frame), &fec_output_bytes, &workspace->fec_stats) != RTNC_FEC_OK) {
        goto cleanup;
    }
    {
        const size_t declared_length = (size_t) frame[0] + 1U;
        const size_t frame_length = RTNC_FRAME_HEADER_SIZE + declared_length +
                                    RTNC_FRAME_CRC_SIZE;
        if (declared_length > modem->payload_class_bytes ||
            rtnc_frame_parse(frame, frame_length, payload, capacity, payload_length) != RTNC_FRAME_OK) {
            goto cleanup;
        }
    }
    result = RTNC_MODEM_OK;

cleanup:
    if (!rtnc_timing_set_output_rate(&modem->timing, 1U)) {
        return RTNC_MODEM_DSP_ERROR;
    }
    if (result == RTNC_MODEM_FRAME_REJECTED && !decision_directed) {
        return decode_equalizer_fallback(
            modem,
            audio,
            sample_count,
            coarse_start,
            phase_at_training_start,
            radians_per_sample,
            payload,
            capacity,
            payload_length,
            metrics,
            workspace,
            true
        );
    }
    return result;
}

static rtnc_modem_status_t modem_rx_audio(
    rtnc_modem_t           *modem,
    const float            *audio,
    size_t                  sample_count,
    uint8_t                *payload,
    size_t                  capacity,
    size_t                 *payload_length,
    rtnc_sync_metrics_t    *metrics,
    rtnc_modem_workspace_t *workspace,
    bool                    allow_equalizer
) {
    uint8_t frame[RTNC_MODEM_MAX_PROTECTED_BYTES];
    uint8_t hard_encoded[RTNC_MODEM_MAX_FEC_BYTES];
    size_t  index;
    size_t  best_start = 0U;
    float   best_score = 0.0F;
    size_t  data_start;
    size_t  frame_length;
    size_t  declared_length;
    float   evm_square_sum = 0.0F;
    float   radians_per_sample = 0.0F;
    float   phase_at_training_start = 0.0F;
    float   tracking_phase = 0.0F;
    float   tracking_frequency = 0.0F;
    float   tracking_gain = 1.0F;
    size_t  llr_index = 0U;
    size_t  fec_output_bytes = 0U;
    size_t  expected_encoded_bytes = 0U;
    size_t  recovered_count = 0U;
    size_t  coarse_start = 0U;
    size_t  maximum_acquisition_start;
    if (modem == NULL || audio == NULL || payload == NULL ||
        payload_length == NULL || metrics == NULL || workspace == NULL ||
        sample_count > RTNC_MODEM_MAX_AUDIO_SAMPLES) {
        return RTNC_MODEM_INVALID_ARGUMENT;
    }
    (void) memset(metrics, 0, sizeof(*metrics));
    workspace->llr_count = 0U;
    rtnc_rrc_reset(&modem->rrc);
    rtnc_carrier_reset(&modem->carrier);
    for (index = 0U; index < sample_count; ++index) {
        float complex baseband;
        if (!rtnc_carrier_downconvert(&modem->carrier, audio[index], &baseband) ||
            !rtnc_rrc_match(&modem->rrc, baseband, &workspace->filtered[index])) {
            return RTNC_MODEM_DSP_ERROR;
        }
    }
    if (sample_count <=
        (RTNC_MODEM_ACQUISITION_SYMBOLS - 1U) *
            modem->profile.samples_per_symbol) {
        return RTNC_MODEM_NO_FRAME;
    }
    maximum_acquisition_start =
        sample_count - 1U -
        (RTNC_MODEM_ACQUISITION_SYMBOLS - 1U) *
            modem->profile.samples_per_symbol;
    for (index = 0U; index <= maximum_acquisition_start; index += 4U) {
        float       phase;
        const float score =
            acquisition_score_at(modem, workspace, index, &phase);
        if (score > best_score) {
            best_score = score;
            best_start = index;
            metrics->phase_radians = phase;
        }
    }
    {
        const size_t refine_start = best_start > 3U ? best_start - 3U : 0U;
        const size_t refine_end =
            best_start + 3U < maximum_acquisition_start
                ? best_start + 3U
                : maximum_acquisition_start;
        for (index = refine_start; index <= refine_end; ++index) {
            float       phase;
            const float score =
                acquisition_score_at(modem, workspace, index, &phase);
            if (score > best_score) {
                best_score = score;
                best_start = index;
                metrics->phase_radians = phase;
            }
        }
    }
    metrics->acquisition_correlation = best_score;
    if (best_score < modem->profile.acquisition_threshold) {
        return RTNC_MODEM_NO_FRAME;
    }
    metrics->frame_detected = true;
    coarse_start = best_start;
    {
        const size_t filter_delay_samples =
            2U * (size_t) RTNC_MODEM_RRC_DELAY_SYMBOLS *
            modem->profile.samples_per_symbol;
        metrics->timing_symbols = best_start >= filter_delay_samples
                                      ? (float) (best_start - filter_delay_samples) /
                                            (float) modem->profile.samples_per_symbol
                                      : 0.0F;
    }
    {
        enum { CFO_LAG_SYMBOLS = 8U };
        float complex differential = 0.0F;
        float complex coherent = 0.0F;
        float complex residuals[RTNC_MODEM_TRAINING_SYMBOLS];
        size_t        training_index;
        for (training_index = 0U; training_index < RTNC_MODEM_TRAINING_SYMBOLS;
             ++training_index) {
            const float complex received = workspace->filtered[best_start + training_index * modem->profile.samples_per_symbol];
            residuals[training_index] =
                received * conjf(modem->training[training_index]);
            if (training_index >= CFO_LAG_SYMBOLS) {
                differential += residuals[training_index] *
                                conjf(residuals[training_index - CFO_LAG_SYMBOLS]);
            }
        }
        radians_per_sample =
            cargf(differential) /
            ((float) CFO_LAG_SYMBOLS *
             (float) modem->profile.samples_per_symbol);
        for (training_index = 0U; training_index < RTNC_MODEM_TRAINING_SYMBOLS;
             ++training_index) {
            const float complex received = workspace->filtered[best_start + training_index * modem->profile.samples_per_symbol];
            const float         phase = radians_per_sample *
                                (float) (training_index *
                                         modem->profile.samples_per_symbol);
            coherent += received * conjf(modem->training[training_index]) *
                        cexpf(-I * phase);
        }
        phase_at_training_start = cargf(coherent);
        metrics->phase_radians = phase_at_training_start;
        metrics->carrier_offset_hz = radians_per_sample *
                                     (float) modem->profile.sample_rate_hz /
                                     (2.0F * (float) M_PI);
    }
    /* Run timing recovery on the raw downconverted stream after removing the
     * coarse carrier estimate. Re-running the NCO keeps the acquisition
     * matched-filter buffer available without adding another sample buffer. */
    rtnc_carrier_reset(&modem->carrier);
    rtnc_timing_reset(&modem->timing);
    for (index = 0U; index < sample_count; ++index) {
        float complex baseband;
        float complex symbols[4];
        size_t        produced = 0U;
        const float   relative_sample = (float) index - (float) best_start;
        const float   correction = phase_at_training_start +
                                 radians_per_sample * relative_sample;
        size_t output_index;
        if (!rtnc_carrier_downconvert(&modem->carrier, audio[index], &baseband) ||
            !rtnc_timing_execute(&modem->timing, baseband * cexpf(-I * correction), symbols, 4U, &produced) ||
            recovered_count + produced > RTNC_MODEM_MAX_RECOVERED_SYMBOLS) {
            return RTNC_MODEM_DSP_ERROR;
        }
        for (output_index = 0U; output_index < produced; ++output_index) {
            workspace->recovered[recovered_count++] = symbols[output_index];
        }
    }
    best_score = 0.0F;
    best_start = 0U;
    for (index = 0U;
         index + RTNC_MODEM_TIMING_TRAINING_SYMBOLS <= recovered_count;
         ++index) {
        float complex correlation = 0.0F;
        float         energy = 0.0F;
        size_t        training_index;
        for (training_index = 0U;
             training_index < RTNC_MODEM_TIMING_TRAINING_SYMBOLS;
             ++training_index) {
            const float complex received =
                workspace->recovered[index + training_index];
            correlation +=
                conjf(modem->training[RTNC_MODEM_TIMING_WARMUP_SYMBOLS + training_index]) * received;
            energy += crealf(received * conjf(received));
        }
        if (energy > 0.0F) {
            const float score = cabsf(correlation) /
                                sqrtf((float) RTNC_MODEM_TIMING_TRAINING_SYMBOLS * energy);
            if (score > best_score) {
                best_score = score;
                best_start = index;
                tracking_phase = cargf(correlation);
            }
        }
    }
    metrics->training_correlation = best_score;
    if (best_score < modem->profile.training_threshold) {
        return RTNC_MODEM_NO_FRAME;
    }
    {
        float complex coherent = 0.0F;
        size_t        training_index;
        tracking_frequency = 0.0F;
        for (training_index = 0U;
             training_index < RTNC_MODEM_TIMING_TRAINING_SYMBOLS;
             ++training_index) {
            coherent +=
                workspace->recovered[best_start + training_index] *
                conjf(modem->training[RTNC_MODEM_TIMING_WARMUP_SYMBOLS + training_index]);
        }
        tracking_phase = cargf(coherent);
        tracking_gain = cabsf(coherent) /
                        (float) RTNC_MODEM_TIMING_TRAINING_SYMBOLS;
        if (tracking_gain < 1.0e-6F) {
            return RTNC_MODEM_NO_FRAME;
        }
    }
    data_start = best_start + RTNC_MODEM_TIMING_TRAINING_SYMBOLS;
    if (modem->fec_mode == FEC_LDPC_ROBUST ||
        modem->fec_mode == FEC_LDPC_NORMAL) {
        const size_t protected_bytes = RTNC_FRAME_HEADER_SIZE +
                                       (size_t) modem->payload_class_bytes +
                                       RTNC_FRAME_CRC_SIZE;
        expected_encoded_bytes =
            rtnc_fec_encoded_size(modem->fec_mode, protected_bytes);
        if (expected_encoded_bytes == 0U ||
            data_start + payload_symbol_count(
                             expected_encoded_bytes,
                             modem->profile.bits_per_symbol
                         ) >
                recovered_count) {
            return RTNC_MODEM_TRUNCATED_FRAME;
        }
        if (!decode_bytes(modem, workspace, data_start, hard_encoded, expected_encoded_bytes, &evm_square_sum, workspace->llr, &llr_index, tracking_gain, &tracking_phase, &tracking_frequency)) {
            return RTNC_MODEM_DSP_ERROR;
        }
        workspace->llr_count = llr_index;
        metrics->evm_rms =
            sqrtf(evm_square_sum / (float) payload_symbol_count(expected_encoded_bytes, modem->profile.bits_per_symbol));
        metrics->training_snr_db =
            -20.0F * log10f(fmaxf(metrics->evm_rms, 1.0e-6F));
        if (rtnc_fec_decode(modem->fec_mode, workspace->llr, workspace->llr_count, frame, sizeof(frame), &fec_output_bytes, &workspace->fec_stats) !=
            RTNC_FEC_OK) {
            if (!allow_equalizer) {
                return RTNC_MODEM_FRAME_REJECTED;
            }
            return decode_equalizer_fallback(
                modem,
                audio,
                sample_count,
                coarse_start,
                phase_at_training_start,
                radians_per_sample,
                payload,
                capacity,
                payload_length,
                metrics,
                workspace,
                false
            );
        }
        declared_length = (size_t) frame[0] + 1U;
        if (declared_length > modem->payload_class_bytes) {
            return RTNC_MODEM_FRAME_REJECTED;
        }
        frame_length = RTNC_FRAME_HEADER_SIZE + declared_length +
                       RTNC_FRAME_CRC_SIZE;
        metrics->carrier_offset_hz += tracking_frequency *
                                      (float) modem->profile.symbol_rate_baud /
                                      (2.0F * (float) M_PI);
        return rtnc_frame_parse(frame, frame_length, payload, capacity, payload_length) == RTNC_FRAME_OK
                   ? RTNC_MODEM_OK
                   : RTNC_MODEM_FRAME_REJECTED;
    }
    if (data_start + RTNC_FRAME_HEADER_SIZE * 4U > recovered_count) {
        return RTNC_MODEM_TRUNCATED_FRAME;
    }
    if (!decode_bytes(modem, workspace, data_start, frame, RTNC_FRAME_HEADER_SIZE, &evm_square_sum, workspace->llr, &llr_index, tracking_gain, &tracking_phase, &tracking_frequency)) {
        return RTNC_MODEM_DSP_ERROR;
    }
    declared_length = (size_t) frame[0] + 1U;
    if (declared_length > RTNC_FRAME_MAX_PAYLOAD) {
        return RTNC_MODEM_FRAME_REJECTED;
    }
    frame_length = RTNC_FRAME_HEADER_SIZE + declared_length + RTNC_FRAME_CRC_SIZE;
    if (data_start + frame_length * 4U > recovered_count) {
        return RTNC_MODEM_TRUNCATED_FRAME;
    }
    if (!decode_bytes(modem, workspace, data_start + RTNC_FRAME_HEADER_SIZE * 4U, &frame[RTNC_FRAME_HEADER_SIZE], frame_length - RTNC_FRAME_HEADER_SIZE, &evm_square_sum, workspace->llr, &llr_index, tracking_gain, &tracking_phase, &tracking_frequency)) {
        return RTNC_MODEM_DSP_ERROR;
    }
    workspace->llr_count = llr_index;
    metrics->evm_rms = sqrtf(evm_square_sum / (float) (frame_length * 4U));
    metrics->training_snr_db =
        -20.0F * log10f(fmaxf(metrics->evm_rms, 1.0e-6F));
    metrics->carrier_offset_hz += tracking_frequency *
                                  (float) modem->profile.symbol_rate_baud /
                                  (2.0F * (float) M_PI);
    if (rtnc_fec_decode(modem->fec_mode, workspace->llr, workspace->llr_count, frame, sizeof(frame), &fec_output_bytes, &workspace->fec_stats) != RTNC_FEC_OK ||
        fec_output_bytes != frame_length) {
        return RTNC_MODEM_FRAME_REJECTED;
    }
    return rtnc_frame_parse(frame, frame_length, payload, capacity, payload_length) == RTNC_FRAME_OK
               ? RTNC_MODEM_OK
               : RTNC_MODEM_FRAME_REJECTED;
}

rtnc_modem_status_t rtnc_modem_rx_audio(rtnc_modem_t *modem, const float *audio, size_t sample_count, uint8_t *payload, size_t capacity, size_t *payload_length, rtnc_sync_metrics_t *metrics, rtnc_modem_workspace_t *workspace) {
    return modem_rx_audio(modem, audio, sample_count, payload, capacity, payload_length, metrics, workspace, true);
}

rtnc_modem_status_t rtnc_modem_rx_audio_fast(
    rtnc_modem_t           *modem,
    const float            *audio,
    size_t                  sample_count,
    uint8_t                *payload,
    size_t                  capacity,
    size_t                 *payload_length,
    rtnc_sync_metrics_t    *metrics,
    rtnc_modem_workspace_t *workspace
) {
    return modem_rx_audio(modem, audio, sample_count, payload, capacity, payload_length, metrics, workspace, false);
}
