#ifndef RTNC_ALSA_RUNTIME_BACKEND_H
#define RTNC_ALSA_RUNTIME_BACKEND_H

#include "rtnc/audio.h"
#include "rtnc/ptt.h"
#include "rtnc/runtime.h"

#include <stdatomic.h>

typedef struct {
    rtnc_audio_t      *audio;
    rtnc_ptt_t        *ptt;
    const atomic_bool *channel_busy;
} rtnc_alsa_runtime_context_t;

/** Bind an initialized ALSA device and GPIO PTT to the packet runtime. */
bool rtnc_alsa_runtime_backend(rtnc_alsa_runtime_context_t *context, rtnc_audio_t *audio, rtnc_ptt_t *ptt, const atomic_bool *channel_busy, rtnc_runtime_backend_t *backend);

#endif
