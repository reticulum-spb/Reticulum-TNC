#include "rtnc/audio.h"

#include <alsa/asoundlib.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>

typedef struct {
    snd_pcm_t           *playback;
    snd_pcm_t           *capture;
    pthread_t            capture_thread;
    atomic_bool          capture_running;
    atomic_uint_fast64_t capture_xruns;
    atomic_uint_fast64_t playback_xruns;
    bool                 thread_started;
    snd_pcm_uframes_t    period_frames;
    rtnc_audio_ring_t   *capture_ring;
} audio_implementation_t;

static bool configure_pcm(snd_pcm_t *pcm, const rtnc_audio_config_t *config, snd_pcm_uframes_t *period_frames) {
    snd_pcm_hw_params_t *parameters;
    unsigned int         rate = config->sample_rate_hz;
    snd_pcm_uframes_t    period =
        (snd_pcm_uframes_t) (config->sample_rate_hz * config->period_ms / 1000U);
    snd_pcm_uframes_t buffer = period * config->periods;
    snd_pcm_hw_params_alloca(&parameters);
    if (snd_pcm_hw_params_any(pcm, parameters) < 0 ||
        snd_pcm_hw_params_set_access(pcm, parameters, SND_PCM_ACCESS_RW_INTERLEAVED) < 0 ||
        snd_pcm_hw_params_set_format(pcm, parameters, SND_PCM_FORMAT_S16_LE) <
            0 ||
        snd_pcm_hw_params_set_channels(pcm, parameters, 1U) < 0 ||
        snd_pcm_hw_params_set_rate_near(pcm, parameters, &rate, NULL) < 0 ||
        rate != config->sample_rate_hz ||
        snd_pcm_hw_params_set_period_size_near(pcm, parameters, &period, NULL) < 0 ||
        snd_pcm_hw_params_set_buffer_size_near(pcm, parameters, &buffer) < 0 ||
        snd_pcm_hw_params(pcm, parameters) < 0 ||
        period == 0U || period > RTNC_AUDIO_BLOCK_SAMPLES) {
        return false;
    }
    *period_frames = period;
    return true;
}

static void *capture_thread_main(void *argument) {
    audio_implementation_t *implementation = argument;
    int16_t                 samples[RTNC_AUDIO_BLOCK_SAMPLES];
    while (atomic_load_explicit(&implementation->capture_running, memory_order_acquire)) {
        const snd_pcm_sframes_t count =
            snd_pcm_readi(implementation->capture, samples, implementation->period_frames);
        if (count == -EPIPE) {
            (void) atomic_fetch_add_explicit(&implementation->capture_xruns, 1U, memory_order_relaxed);
            (void) snd_pcm_prepare(implementation->capture);
        } else if (count < 0) {
            if (!atomic_load_explicit(&implementation->capture_running, memory_order_acquire)) {
                break;
            }
            if (snd_pcm_recover(implementation->capture, (int) count, 0) < 0) {
                break;
            }
            (void) atomic_fetch_add_explicit(&implementation->capture_xruns, 1U, memory_order_relaxed);
        } else if (count > 0) {
            (void) rtnc_audio_ring_push(implementation->capture_ring, samples, (size_t) count);
        }
    }
    return NULL;
}

bool rtnc_audio_init(rtnc_audio_t *audio, const rtnc_audio_config_t *config, rtnc_audio_ring_t *capture_ring) {
    audio_implementation_t *implementation;
    snd_pcm_uframes_t       playback_period = 0U;
    if (audio == NULL || config == NULL || capture_ring == NULL ||
        audio->implementation != NULL) {
        return false;
    }
    implementation = calloc(1U, sizeof(*implementation));
    if (implementation == NULL) {
        return false;
    }
    atomic_init(&implementation->capture_running, false);
    atomic_init(&implementation->capture_xruns, 0U);
    atomic_init(&implementation->playback_xruns, 0U);
    implementation->capture_ring = capture_ring;
    if (snd_pcm_open(&implementation->playback, config->device, SND_PCM_STREAM_PLAYBACK, 0) < 0 ||
        !configure_pcm(implementation->playback, config, &playback_period) ||
        snd_pcm_prepare(implementation->playback) < 0 ||
        snd_pcm_open(&implementation->capture, config->device, SND_PCM_STREAM_CAPTURE, 0) < 0 ||
        !configure_pcm(implementation->capture, config, &implementation->period_frames) ||
        snd_pcm_prepare(implementation->capture) < 0) {
        if (implementation->capture != NULL) {
            (void) snd_pcm_close(implementation->capture);
        }
        if (implementation->playback != NULL) {
            (void) snd_pcm_close(implementation->playback);
        }
        free(implementation);
        return false;
    }
    audio->implementation = implementation;
    return true;
}

bool rtnc_audio_start_capture(rtnc_audio_t *audio) {
    audio_implementation_t *implementation;
    if (audio == NULL || audio->implementation == NULL) {
        return false;
    }
    implementation = audio->implementation;
    if (implementation->thread_started) {
        return true;
    }
    if (snd_pcm_start(implementation->capture) < 0) {
        return false;
    }
    atomic_store_explicit(&implementation->capture_running, true, memory_order_release);
    if (pthread_create(&implementation->capture_thread, NULL, capture_thread_main, implementation) != 0) {
        atomic_store_explicit(&implementation->capture_running, false, memory_order_release);
        (void) snd_pcm_drop(implementation->capture);
        return false;
    }
    implementation->thread_started = true;
    return true;
}

void rtnc_audio_stop_capture(rtnc_audio_t *audio) {
    audio_implementation_t *implementation;
    if (audio == NULL || audio->implementation == NULL) {
        return;
    }
    implementation = audio->implementation;
    if (!implementation->thread_started) {
        return;
    }
    atomic_store_explicit(&implementation->capture_running, false, memory_order_release);
    (void) snd_pcm_drop(implementation->capture);
    (void) pthread_join(implementation->capture_thread, NULL);
    implementation->thread_started = false;
    (void) snd_pcm_prepare(implementation->capture);
}

bool rtnc_audio_send(rtnc_audio_t *audio, const int16_t *samples, size_t count) {
    audio_implementation_t *implementation;
    size_t                  offset = 0U;
    if (audio == NULL || audio->implementation == NULL || samples == NULL) {
        return false;
    }
    implementation = audio->implementation;
    while (offset < count) {
        const snd_pcm_sframes_t written =
            snd_pcm_writei(implementation->playback, &samples[offset], (snd_pcm_uframes_t) (count - offset));
        if (written == -EPIPE) {
            (void) atomic_fetch_add_explicit(&implementation->playback_xruns, 1U, memory_order_relaxed);
            (void) snd_pcm_prepare(implementation->playback);
        } else if (written < 0) {
            if (snd_pcm_recover(implementation->playback, (int) written, 0) < 0) {
                return false;
            }
            (void) atomic_fetch_add_explicit(&implementation->playback_xruns, 1U, memory_order_relaxed);
        } else {
            offset += (size_t) written;
        }
    }
    return true;
}

bool rtnc_audio_wait(rtnc_audio_t *audio) {
    audio_implementation_t *implementation;
    if (audio == NULL || audio->implementation == NULL) {
        return false;
    }
    implementation = audio->implementation;
    return snd_pcm_drain(implementation->playback) >= 0 &&
           snd_pcm_prepare(implementation->playback) >= 0;
}

uint64_t rtnc_audio_capture_xruns(const rtnc_audio_t *audio) {
    const audio_implementation_t *implementation =
        audio != NULL ? audio->implementation : NULL;
    return implementation != NULL
               ? (uint64_t) atomic_load_explicit(&implementation->capture_xruns, memory_order_relaxed)
               : 0U;
}

uint64_t rtnc_audio_playback_xruns(const rtnc_audio_t *audio) {
    const audio_implementation_t *implementation =
        audio != NULL ? audio->implementation : NULL;
    return implementation != NULL
               ? (uint64_t) atomic_load_explicit(&implementation->playback_xruns, memory_order_relaxed)
               : 0U;
}

void rtnc_audio_deinit(rtnc_audio_t *audio) {
    audio_implementation_t *implementation;
    if (audio == NULL || audio->implementation == NULL) {
        return;
    }
    rtnc_audio_stop_capture(audio);
    implementation = audio->implementation;
    (void) snd_pcm_close(implementation->capture);
    (void) snd_pcm_close(implementation->playback);
    free(implementation);
    audio->implementation = NULL;
}
