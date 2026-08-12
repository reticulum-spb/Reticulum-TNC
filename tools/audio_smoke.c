#define _POSIX_C_SOURCE 200809L

#include "rtnc/audio.h"
#include "rtnc/audio_ring.h"
#include "rtnc/platform_config.h"

#include <inttypes.h>
#include <stdio.h>
#include <time.h>

static double elapsed_seconds(const struct timespec *start, const struct timespec *finish) {
    return (double) (finish->tv_sec - start->tv_sec) +
           (double) (finish->tv_nsec - start->tv_nsec) / 1.0e9;
}

int main(int argc, char **argv) {
    const struct timespec   idle = { .tv_sec = 0, .tv_nsec = 1000000L };
    rtnc_platform_config_t *config = NULL;
    rtnc_audio_ring_t       ring;
    rtnc_audio_t            audio = { 0 };
    rtnc_audio_block_t      block;
    struct timespec         start;
    struct timespec         now;
    uint64_t                blocks = 0U;
    uint64_t                samples = 0U;
    uint64_t                target_samples;
    bool                    ok = false;

    if (argc != 2) {
        (void) fprintf(stderr, "usage: %s CONFIG.yaml\n", argv[0]);
        return 2;
    }
    if (!rtnc_platform_config_load(argv[1], &config)) {
        (void) fprintf(stderr, "invalid configuration: %s\n", argv[1]);
        return 1;
    }
    if (!rtnc_audio_ring_init(&ring, config->workers.dsp_queue_blocks)) {
        (void) fprintf(stderr, "invalid audio ring capacity\n");
        goto done;
    }
    target_samples = config->audio.sample_rate_hz;
    if (!rtnc_audio_init(&audio, &config->audio, &ring)) {
        (void) fprintf(stderr, "failed to open ALSA device %s\n", config->audio.device);
        goto done;
    }
    if (!rtnc_audio_start_capture(&audio)) {
        (void) fprintf(stderr, "failed to start ALSA capture\n");
        goto done;
    }
    (void) clock_gettime(CLOCK_MONOTONIC, &start);
    now = start;
    while (samples < target_samples && elapsed_seconds(&start, &now) < 3.0) {
        if (rtnc_audio_ring_pop(&ring, &block)) {
            blocks += 1U;
            samples += block.count;
        } else {
            (void) nanosleep(&idle, NULL);
        }
        (void) clock_gettime(CLOCK_MONOTONIC, &now);
    }
    rtnc_audio_stop_capture(&audio);
    ok = samples >= target_samples;
    (void) printf("capture=%s blocks=%" PRIu64 " samples=%" PRIu64 " elapsed=%.3f depth=%zu drops=%" PRIu64 " xruns=%" PRIu64 "\n", ok ? "ok" : "timeout", blocks, samples, elapsed_seconds(&start, &now), rtnc_audio_ring_depth(&ring), rtnc_audio_ring_dropped(&ring), rtnc_audio_capture_xruns(&audio));

done:
    rtnc_audio_deinit(&audio);
    rtnc_platform_config_free(config);
    return ok ? 0 : 1;
}
