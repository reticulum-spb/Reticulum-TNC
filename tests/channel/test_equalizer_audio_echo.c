#include "rtnc/carrier.h"
#include "rtnc/equalizer.h"
#include "rtnc/modem.h"
#include "rtnc/rrc.h"
#include "rtnc/timing.h"

#include <assert.h>
#include <complex.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    DATA_SYMBOLS = 576U,
    TRAILING_SYMBOLS = 64U,
    TAP_COUNT = 17U,
    MAX_SAMPLES = 32000U,
    MAX_RECOVERED = 1800U,
};

static void emit(rtnc_rrc_t *rrc, rtnc_carrier_t *carrier, float complex symbol, float *audio, size_t *count) {
    float complex shaped[40];
    size_t        sample;
    assert(rtnc_rrc_interpolate(rrc, symbol, shaped));
    for (sample = 0U; sample < 40U; ++sample) {
        assert(rtnc_carrier_upconvert(carrier, shaped[sample], &audio[*count]));
        ++(*count);
    }
}

int main(void) {
    float         clean[MAX_SAMPLES] = { 0.0F };
    float         echoed[MAX_SAMPLES] = { 0.0F };
    float complex recovered[MAX_RECOVERED];
    float complex data_reference[DATA_SYMBOLS];
    uint8_t       data_values[DATA_SYMBOLS];
    uint8_t       payload[64];
    uint8_t       frame[RTNC_FRAME_MAX_ENCODED_SIZE];
    uint8_t protected[RTNC_MODEM_MAX_PROTECTED_BYTES] = { 0U };
    uint8_t          encoded[RTNC_MODEM_MAX_FEC_BYTES];
    rtnc_modem_t     modem;
    rtnc_equalizer_t equalizer = { 0 };
    size_t           clean_count = 0U;
    size_t           recovered_count = 0U;
    size_t           best_start = 0U;
    size_t           search_limit;
    float            best_score = 0.0F;
    size_t           index;
    size_t           frame_length = 0U;
    size_t           encoded_length = 0U;
    unsigned int     raw_errors = 0U;
    unsigned int     equalized_errors = 0U;
    float            test_error = 0.0F;
    float            raw_error = 0.0F;

    assert(rtnc_modem_init(&modem));
    assert(rtnc_timing_set_output_rate(&modem.timing, 2U));
    assert(rtnc_equalizer_init(&equalizer, TAP_COUNT, 0.95F));
    for (index = 0U; index < sizeof(payload); ++index) {
        payload[index] = (uint8_t) (index * 37U + 0x29U);
    }
    assert(rtnc_frame_build(payload, sizeof(payload), frame, sizeof(frame), &frame_length) == RTNC_FRAME_OK);
    (void) memcpy(protected, frame, frame_length);
    assert(rtnc_fec_encode(FEC_LDPC_ROBUST, protected, RTNC_FRAME_HEADER_SIZE + sizeof(payload) + RTNC_FRAME_CRC_SIZE, encoded, sizeof(encoded), &encoded_length) == RTNC_FEC_OK);
    assert(encoded_length * 4U == DATA_SYMBOLS);
    for (index = 0U; index < DATA_SYMBOLS; ++index) {
        const unsigned int shift = 6U - (unsigned int) (index % 4U) * 2U;
        data_values[index] =
            (uint8_t) (((unsigned int) encoded[index / 4U] >> shift) & 3U);
    }
    for (index = 0U; index < RTNC_MODEM_TRAINING_SYMBOLS; ++index) {
        emit(&modem.rrc, &modem.carrier, modem.training[index], clean, &clean_count);
    }
    for (index = 0U; index < DATA_SYMBOLS; ++index) {
        assert(rtnc_psk_map(&modem.psk, data_values[index], &data_reference[index]));
        emit(&modem.rrc, &modem.carrier, data_reference[index], clean, &clean_count);
    }
    for (index = 0U; index < TRAILING_SYMBOLS; ++index) {
        emit(&modem.rrc, &modem.carrier, 0.0F, clean, &clean_count);
    }
    assert(clean_count <= MAX_SAMPLES);
    for (index = 0U; index < clean_count; ++index) {
        echoed[index] = clean[index];
        if (index >= 20U) {
            echoed[index] += 0.40F * clean[index - 20U];
        }
    }
    rtnc_carrier_reset(&modem.carrier);
    rtnc_timing_reset(&modem.timing);
    for (index = 0U; index < clean_count; ++index) {
        float complex baseband;
        float complex output[4];
        size_t        produced = 0U;
        size_t        output_index;
        assert(rtnc_carrier_downconvert(&modem.carrier, echoed[index], &baseband));
        assert(rtnc_timing_execute(&modem.timing, baseband, output, 4U, &produced));
        assert(recovered_count + produced <= MAX_RECOVERED);
        for (output_index = 0U; output_index < produced; ++output_index) {
            recovered[recovered_count++] = output[output_index];
        }
    }
    search_limit = 2U * (RTNC_MODEM_TRAINING_SYMBOLS + 30U);
    if (search_limit > recovered_count) {
        search_limit = recovered_count;
    }
    for (index = 0U;
         index + 2U * RTNC_MODEM_TIMING_TRAINING_SYMBOLS <= search_limit;
         ++index) {
        float complex correlation = 0.0F;
        float         energy = 0.0F;
        size_t        symbol;
        for (symbol = 0U; symbol < RTNC_MODEM_TIMING_TRAINING_SYMBOLS;
             ++symbol) {
            const float complex received = recovered[index + 2U * symbol];
            correlation +=
                conjf(modem.training[RTNC_MODEM_TIMING_WARMUP_SYMBOLS + symbol]) *
                received;
            energy += crealf(received * conjf(received));
        }
        if (energy > 0.0F) {
            const float score =
                cabsf(correlation) /
                sqrtf((float) RTNC_MODEM_TIMING_TRAINING_SYMBOLS * energy);
            if (score > best_score) {
                best_score = score;
                best_start = index;
            }
        }
    }
    assert(best_score > 0.90F);
    {
        const size_t first_pair = best_start & 1U;
        const size_t data_start =
            best_start + 2U * RTNC_MODEM_TIMING_TRAINING_SYMBOLS;
        float complex training_gain = 0.0F;
        size_t        pair;
        for (pair = first_pair; pair + 1U < data_start; pair += 2U) {
            float complex output;
            float         error;
            const bool    training = pair >= best_start;
            const size_t  training_index =
                training ? (pair - best_start) / 2U : 0U;
            const float complex desired =
                training
                    ? modem.training[RTNC_MODEM_TIMING_WARMUP_SYMBOLS + training_index]
                    : 0.0F;
            assert(rtnc_equalizer_execute(&equalizer, &recovered[pair], training, desired, &output, &error));
        }
        for (index = 0U; index < RTNC_MODEM_TIMING_TRAINING_SYMBOLS; ++index) {
            training_gain +=
                recovered[best_start + 2U * index] *
                conjf(modem.training[RTNC_MODEM_TIMING_WARMUP_SYMBOLS + index]);
        }
        training_gain /= (float) RTNC_MODEM_TIMING_TRAINING_SYMBOLS;
        for (index = 0U; index < DATA_SYMBOLS; ++index) {
            float complex equalized;
            float         error;
            uint8_t       hard;
            float         llr[3];
            float         evm;
            const size_t  data_pair = data_start + 2U * index;
            assert(data_pair + 1U < recovered_count);
            {
                const float complex raw = recovered[data_pair] / training_gain;
                assert(rtnc_psk_demap_soft(&modem.psk, raw, &hard, llr, &evm));
                if (hard != data_values[index]) {
                    ++raw_errors;
                }
                raw_error += cabsf(data_reference[index] - raw);
            }
            assert(rtnc_equalizer_execute(&equalizer, &recovered[data_pair], false, data_reference[index], &equalized, &error));
            test_error += error;
            assert(rtnc_psk_demap_soft(&modem.psk, equalized, &hard, llr, &evm));
            if (hard != data_values[index]) {
                ++equalized_errors;
            }
        }
    }
    test_error /= (float) DATA_SYMBOLS;
    raw_error /= (float) DATA_SYMBOLS;
    (void) printf("score=%.6f raw_errors=%u equalized_errors=%u raw_error=%.6f "
                  "equalized_error=%.6f\n",
                  (double) best_score,
                  raw_errors,
                  equalized_errors,
                  (double) raw_error,
                  (double) test_error);
    assert(equalized_errors == 0U);
    assert(test_error < 0.20F);
    assert(test_error < raw_error * 0.90F);
    rtnc_equalizer_deinit(&equalizer);
    rtnc_modem_deinit(&modem);
    return 0;
}
