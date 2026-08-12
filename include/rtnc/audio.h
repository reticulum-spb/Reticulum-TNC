#ifndef RTNC_AUDIO_H
#define RTNC_AUDIO_H

#include "rtnc/audio_ring.h"
#include "rtnc/platform_config.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    void *implementation;
} rtnc_audio_t;

/** Open configured ALSA playback/capture streams without starting capture. */
bool rtnc_audio_init(rtnc_audio_t *audio, const rtnc_audio_config_t *config, rtnc_audio_ring_t *capture_ring);
bool rtnc_audio_start_capture(rtnc_audio_t *audio);
void rtnc_audio_stop_capture(rtnc_audio_t *audio);

/** Send mono S16 samples, recovering ALSA underruns when possible. */
bool rtnc_audio_send(rtnc_audio_t *audio, const int16_t *samples, size_t count);
/** Drain playback and prepare it for the next transmission. */
bool rtnc_audio_wait(rtnc_audio_t *audio);

uint64_t rtnc_audio_capture_xruns(const rtnc_audio_t *audio);
uint64_t rtnc_audio_playback_xruns(const rtnc_audio_t *audio);
void     rtnc_audio_deinit(rtnc_audio_t *audio);

#endif
