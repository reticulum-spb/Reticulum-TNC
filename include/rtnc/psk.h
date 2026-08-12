#ifndef RTNC_PSK_H
#define RTNC_PSK_H

#include "rtnc/phy.h"

#include <complex.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    void   *modulator;
    void   *demodulator;
    uint8_t bits_per_symbol;
} rtnc_psk_t;

bool rtnc_psk_init(rtnc_psk_t *psk, rtnc_modulation_t modulation);
void rtnc_psk_deinit(rtnc_psk_t *psk);
bool rtnc_psk_map(rtnc_psk_t *psk, uint8_t symbol, float complex *sample);
bool rtnc_psk_demap_soft(rtnc_psk_t *psk, float complex sample, uint8_t *symbol, float llr[4], float *evm);

#endif
