#include "rtnc/psk.h"

#include <liquid/liquid.h>
#include <stddef.h>

bool rtnc_psk_init(rtnc_psk_t *psk, rtnc_modulation_t modulation) {
    modulation_scheme scheme;
    if (psk == NULL) {
        return false;
    }
    if (modulation == RTNC_MODULATION_QPSK) {
        scheme = LIQUID_MODEM_QPSK;
        psk->bits_per_symbol = 2U;
    } else if (modulation == RTNC_MODULATION_8PSK) {
        scheme = LIQUID_MODEM_PSK8;
        psk->bits_per_symbol = 3U;
    } else {
        return false;
    }
    psk->modulator = modemcf_create(scheme);
    psk->demodulator = modemcf_create(scheme);
    if (psk->modulator == NULL || psk->demodulator == NULL) {
        rtnc_psk_deinit(psk);
        return false;
    }
    return true;
}

void rtnc_psk_deinit(rtnc_psk_t *psk) {
    if (psk == NULL) {
        return;
    }
    if (psk->modulator != NULL) {
        (void) modemcf_destroy((modemcf) psk->modulator);
    }
    if (psk->demodulator != NULL) {
        (void) modemcf_destroy((modemcf) psk->demodulator);
    }
    psk->modulator = NULL;
    psk->demodulator = NULL;
    psk->bits_per_symbol = 0U;
}

bool rtnc_psk_map(rtnc_psk_t *psk, uint8_t symbol, float complex *sample) {
    unsigned int count;
    if (psk == NULL) {
        return false;
    }
    count = 1U << psk->bits_per_symbol;
    return psk->modulator != NULL && sample != NULL &&
           (unsigned int) symbol < count &&
           modemcf_modulate((modemcf) psk->modulator, symbol, sample) == LIQUID_OK;
}

bool rtnc_psk_demap_soft(rtnc_psk_t *psk, float complex sample, uint8_t *symbol, float llr[3], float *evm) {
    unsigned int  hard_symbol = 0U;
    unsigned char soft_bits[3] = { 0U, 0U, 0U };
    unsigned int  bit;
    if (psk == NULL || psk->demodulator == NULL || symbol == NULL ||
        llr == NULL || evm == NULL) {
        return false;
    }
    if (modemcf_demodulate_soft((modemcf) psk->demodulator, sample, &hard_symbol, soft_bits) != LIQUID_OK) {
        return false;
    }
    *symbol = (uint8_t) hard_symbol;
    for (bit = 0U; bit < psk->bits_per_symbol; ++bit) {
        llr[bit] = (127.5F - (float) soft_bits[bit]) / 127.5F;
    }
    *evm = modemcf_get_demodulator_evm((modemcf) psk->demodulator);
    return true;
}
