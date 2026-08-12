#ifndef RTNC_TRANSMITTER_H
#define RTNC_TRANSMITTER_H

#include "rtnc/audio.h"
#include "rtnc/platform_config.h"
#include "rtnc/ptt.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Apply the configured fixed TX response correction, key PTT, observe
 * lead/tail delays, and synchronously play one 48-kHz PCM waveform. PTT is
 * released before returning, including every playback error path.
 */
bool rtnc_transmit_audio(rtnc_audio_t *audio, rtnc_ptt_t *ptt, const rtnc_tx_config_t *config, const int16_t *samples, size_t count);

#endif
