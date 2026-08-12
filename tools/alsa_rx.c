#define _POSIX_C_SOURCE 200809L

#include "rtnc/acquisition.h"
#include "rtnc/alsa_runtime_backend.h"
#include "rtnc/audio.h"
#include "rtnc/audio_ring.h"
#include "rtnc/burst_detector.h"
#include "rtnc/carrier.h"
#include "rtnc/completion.h"
#include "rtnc/decode_queue.h"
#include "rtnc/fragmentation.h"
#include "rtnc/kiss.h"
#include "rtnc/modem.h"
#include "rtnc/platform_config.h"
#include "rtnc/ptt.h"
#include "rtnc/runtime.h"
#include "rtnc/wav.h"

#include <liquid/liquid.h>

#include <complex.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/resource.h>
#include <syslog.h>
#include <time.h>

typedef struct {
    rtnc_decode_queue_t            queue;
    rtnc_modem_t                   modem;
    rtnc_modem_workspace_t         workspace;
    rtnc_decode_job_t              job;
    atomic_bool                    running;
    atomic_uint                   *decoded_frames;
    rtnc_decode_queue_t           *fallback_queue;
    bool                           fast_only;
    unsigned int                   nice_increment;
    int                            actual_nice;
    unsigned int                   target_frames;
    unsigned int                   attempts;
    float                          best_acquisition;
    float                          best_training;
    rtnc_modem_status_t            best_status;
    double                         decode_wall_seconds;
    double                         decode_cpu_seconds;
    double                         maximum_decode_seconds;
    double                         maximum_queue_seconds;
    unsigned int                   equalizer_successes;
    double                         snr_sum_db;
    float                          minimum_snr_db;
    unsigned int                   snr_count;
    rtnc_completion_coordinator_t *completion_coordinator;
    uint32_t                       generation;
    bool                           packet_mode;
    unsigned int                   worker_id;
} decoder_context_t;

typedef struct {
    rtnc_runtime_t *runtime;
    atomic_bool     running;
} tx_worker_context_t;

static volatile sig_atomic_t stop_requested = 0;

static void request_stop(int signal_number) {
    (void) signal_number;
    stop_requested = 1;
}

static double clock_seconds(clockid_t clock_id) {
    struct timespec value = { 0 };
    (void) clock_gettime(clock_id, &value);
    return (double) value.tv_sec + (double) value.tv_nsec / 1.0e9;
}

static void *tx_worker_main(void *argument) {
    const struct timespec idle = { .tv_sec = 0, .tv_nsec = 1000000L };
    tx_worker_context_t  *worker = argument;
    while (atomic_load_explicit(&worker->running, memory_order_acquire) ||
           rtnc_packet_queue_depth(&worker->runtime->tx_queue) > 0U) {
        const rtnc_runtime_status_t status =
            rtnc_runtime_transmit_next(worker->runtime);
        if (status == RTNC_RUNTIME_IDLE || status == RTNC_RUNTIME_BUSY) {
            (void) nanosleep(&idle, NULL);
        } else if (status == RTNC_RUNTIME_OK) {
            const rtnc_runtime_stats_t *stats =
                rtnc_runtime_get_stats(worker->runtime);
            syslog(LOG_INFO, "tx complete packets=%llu bytes=%llu frames=%llu", (unsigned long long) stats->tx_packets, (unsigned long long) stats->tx_bytes, (unsigned long long) stats->tx_frames);
        } else {
            syslog(LOG_WARNING, "tx failed status=%d", (int) status);
        }
    }
    return NULL;
}

static int open_kiss_socket(uint16_t port) {
    struct sockaddr_in address;
    const int          reuse = 1;
    int                descriptor;
    descriptor = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (descriptor < 0) {
        return -1;
    }
    (void) setsockopt(descriptor, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    (void) memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (bind(descriptor, (const struct sockaddr *) &address, sizeof(address)) !=
            0 ||
        listen(descriptor, 1) != 0) {
        (void) close(descriptor);
        return -1;
    }
    return descriptor;
}

static void save_candidate_wav(const char *prefix, uint64_t sequence, const float *samples, size_t count, int16_t *pcm) {
    char   path[512];
    FILE  *stream;
    size_t index;
    int    length;
    if (prefix == NULL || samples == NULL || pcm == NULL ||
        count > RTNC_MODEM_MAX_AUDIO_SAMPLES) {
        return;
    }
    length = snprintf(path, sizeof(path), "%s_%03llu.wav", prefix, (unsigned long long) sequence);
    if (length < 0 || (size_t) length >= sizeof(path)) {
        return;
    }
    for (index = 0U; index < count; ++index) {
        const float scaled = samples[index] * 32768.0F;
        pcm[index] = (int16_t) lrintf(
            fmaxf(-32768.0F, fminf(32767.0F, scaled))
        );
    }
    stream = fopen(path, "wb");
    if (stream == NULL) {
        (void) fprintf(stderr, "could not save candidate WAV %s\n", path);
        return;
    }
    if (rtnc_wav_write_mono_s16(stream, 48000U, pcm, count) != RTNC_WAV_OK) {
        (void) fclose(stream);
        (void) fprintf(stderr, "could not save candidate WAV %s\n", path);
        return;
    }
    if (fclose(stream) != 0) {
        (void) fprintf(stderr, "could not finish candidate WAV %s\n", path);
    }
}

static void *decoder_thread_main(void *argument) {
    const struct timespec idle = { .tv_sec = 0, .tv_nsec = 1000000L };
    decoder_context_t    *decoder = argument;
    if (decoder->nice_increment > 0U) {
        if (setpriority(PRIO_PROCESS, 0, (int) decoder->nice_increment) != 0) {
            (void) fprintf(stderr, "equalizer priority adjustment failed\n");
        }
    }
    decoder->actual_nice = getpriority(PRIO_PROCESS, 0);
    while (atomic_load_explicit(&decoder->running, memory_order_acquire) ||
           rtnc_decode_queue_depth(&decoder->queue) > 0U) {
        uint8_t             payload[128U];
        size_t              payload_length = 0U;
        rtnc_sync_metrics_t metrics = { 0 };
        size_t              index;
        double              wall_start;
        double              cpu_start;
        double              elapsed;
        if (!rtnc_decode_queue_pop(&decoder->queue, &decoder->job)) {
            (void) nanosleep(&idle, NULL);
            continue;
        }
        wall_start = clock_seconds(CLOCK_MONOTONIC);
        cpu_start = clock_seconds(CLOCK_THREAD_CPUTIME_ID);
        if (decoder->job.enqueued_monotonic_ns > 0U) {
            const double queued =
                wall_start -
                (double) decoder->job.enqueued_monotonic_ns / 1.0e9;
            if (queued > decoder->maximum_queue_seconds) {
                decoder->maximum_queue_seconds = queued;
            }
        }
        decoder->attempts += 1U;
        const rtnc_modem_status_t status =
            decoder->fast_only
                ? rtnc_modem_rx_audio_fast(
                      &decoder->modem,
                      decoder->job.samples,
                      decoder->job.count,
                      payload,
                      sizeof(payload),
                      &payload_length,
                      &metrics,
                      &decoder->workspace
                  )
                : rtnc_modem_rx_audio(
                      &decoder->modem,
                      decoder->job.samples,
                      decoder->job.count,
                      payload,
                      sizeof(payload),
                      &payload_length,
                      &metrics,
                      &decoder->workspace
                  );
        syslog(LOG_INFO, "decoder=%u sequence=%llu status=%d detected=%d "
                         "acquisition=%.3f training=%.3f evm=%.3f "
                         "effective_snr_db=%.2f fec_converged=%d fec_iterations=%u",
               decoder->worker_id,
               (unsigned long long) decoder->job.sequence,
               (int) status,
               metrics.frame_detected ? 1 : 0,
               (double) metrics.acquisition_correlation,
               (double) metrics.training_correlation,
               (double) metrics.evm_rms,
               (double) metrics.training_snr_db,
               decoder->workspace.fec_stats.converged ? 1 : 0,
               decoder->workspace.fec_stats.iterations);
        elapsed = clock_seconds(CLOCK_MONOTONIC) - wall_start;
        decoder->decode_wall_seconds += elapsed;
        decoder->decode_cpu_seconds +=
            clock_seconds(CLOCK_THREAD_CPUTIME_ID) - cpu_start;
        if (elapsed > decoder->maximum_decode_seconds) {
            decoder->maximum_decode_seconds = elapsed;
        }
        if (metrics.equalizer_used) {
            decoder->equalizer_successes += 1U;
        }
        if (metrics.evm_rms > 0.0F && isfinite(metrics.training_snr_db)) {
            decoder->snr_sum_db += (double) metrics.training_snr_db;
            if (decoder->snr_count == 0U ||
                metrics.training_snr_db < decoder->minimum_snr_db) {
                decoder->minimum_snr_db = metrics.training_snr_db;
            }
            decoder->snr_count += 1U;
        }
        if (metrics.acquisition_correlation > decoder->best_acquisition) {
            decoder->best_acquisition = metrics.acquisition_correlation;
            decoder->best_training = metrics.training_correlation;
            decoder->best_status = status;
        }
        bool deferred = false;
        if (decoder->fast_only && status == RTNC_MODEM_FRAME_REJECTED &&
            metrics.frame_detected && decoder->fallback_queue != NULL) {
            deferred = rtnc_decode_queue_push(
                decoder->fallback_queue,
                decoder->job.samples,
                decoder->job.count,
                decoder->job.sequence
            );
        }
        if (decoder->completion_coordinator != NULL &&
            (status == RTNC_MODEM_OK || !deferred)) {
            (void) rtnc_completion_submit(
                decoder->completion_coordinator,
                decoder->job.sequence,
                decoder->generation,
                status == RTNC_MODEM_OK,
                payload,
                status == RTNC_MODEM_OK ? payload_length : 0U
            );
        }
        if (status == RTNC_MODEM_OK) {
            if (decoder->packet_mode) {
                continue;
            }
            (void) printf("decoded sequence=%llu bytes=%zu acquisition=%.3f "
                          "training=%.3f cfo=%.1f evm=%.3f attempts=%u\n",
                          (unsigned long long) decoder->job.sequence,
                          payload_length,
                          (double) metrics.acquisition_correlation,
                          (double) metrics.training_correlation,
                          (double) metrics.carrier_offset_hz,
                          (double) metrics.evm_rms,
                          decoder->attempts);
            (void) printf("payload=");
            for (index = 0U; index < payload_length; ++index) {
                (void) printf("%02x", (unsigned int) payload[index]);
            }
            (void) printf("\n");
            (void) fflush(stdout);
            (void) printf("sequence_number=%u\n", ((unsigned int) payload[0] << 24U) | ((unsigned int) payload[1] << 16U) | ((unsigned int) payload[2] << 8U) | (unsigned int) payload[3]);
            (void) atomic_fetch_add_explicit(decoder->decoded_frames, 1U, memory_order_release);
            if (decoder->target_frames != 0U &&
                atomic_load_explicit(decoder->decoded_frames, memory_order_acquire) >=
                    decoder->target_frames) {
                break;
            }
        }
    }
    return NULL;
}

static double monotonic_seconds(void) {
    return clock_seconds(CLOCK_MONOTONIC);
}

static int print_profile_list(const char *config_filename) {
    rtnc_platform_config_t *config = NULL;
    unsigned int            index;

    if (!rtnc_platform_config_load(config_filename, &config)) {
        (void) fprintf(stderr, "invalid configuration: %s\n", config_filename);
        return 1;
    }
    (void) printf("%-32s %s\n", "name", "interface bitrate bps");
    for (index = 0U; index < config->profiles_count; ++index) {
        const rtnc_phy_profile_config_t *entry = &config->profiles[index];
        rtnc_phy_profile_t               profile;
        rtnc_modem_rate_t                rate;
        if (!rtnc_platform_phy_profile_named(config, entry->name, &profile) ||
            !rtnc_modem_profile_rate(
                &profile,
                (fec_mode_t) entry->fec_mode,
                entry->payload_class_bytes,
                &rate
            )) {
            (void) fprintf(stderr, "cannot calculate profile rate: %s\n", entry->name);
            rtnc_platform_config_free(config);
            return 1;
        }
        (void) printf("%-32s %u\n", entry->name, rate.interface_bitrate_bps);
    }
    rtnc_platform_config_free(config);
    return 0;
}

int main(int argc, char **argv) {
    const struct timespec                idle = { .tv_sec = 0, .tv_nsec = 1000000L };
    rtnc_platform_config_t              *config = NULL;
    rtnc_phy_profile_t                   phy_profile;
    rtnc_modem_rate_t                    profile_rate;
    const rtnc_phy_profile_config_t     *selected_profile = NULL;
    const char                          *profile_name = NULL;
    rtnc_burst_detector_config_t         detector_config;
    static rtnc_audio_ring_t             audio_ring;
    static rtnc_burst_detector_t         detector;
    static decoder_context_t             decoder;
    static decoder_context_t             equalizer_decoder;
    static atomic_uint                   decoded_frames;
    static atomic_uint                   completed_packets;
    static rtnc_completion_coordinator_t completion_coordinator;
    static rtnc_runtime_t                packet_runtime;
    static atomic_bool                   channel_busy;
    static int16_t                       capture_pcm[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    rtnc_audio_t                         audio = { 0 };
    rtnc_ptt_t                           ptt = { 0 };
    rtnc_alsa_runtime_context_t          runtime_context = { 0 };
    rtnc_runtime_backend_t               runtime_backend = { 0 };
    rtnc_audio_block_t                   block;
    static rtnc_acquisition_detector_t   acquisition_detector;
    rtnc_carrier_t                       power_carrier = { 0 };
    firfilt_crcf                         power_filter = NULL;
    pthread_t                            decoder_thread;
    pthread_t                            equalizer_thread;
    pthread_t                            tx_thread;
    static tx_worker_context_t           tx_worker;
    bool                                 modem_initialized = false;
    bool                                 equalizer_modem_initialized = false;
    bool                                 thread_started = false;
    bool                                 equalizer_thread_started = false;
    bool                                 capture_started = false;
    bool                                 runtime_initialized = false;
    bool                                 tx_thread_started = false;
    uint64_t                             sequence = 0U;
    bool                                 packet_mode = false;
    bool                                 service_mode = false;
    const char                          *capture_prefix = NULL;
    int                                  listen_socket = -1;
    int                                  packet_client = -1;
    rtnc_kiss_parser_t                   kiss_parser;
    double                               start;
    double                               run_seconds = 30.0;
    double                               maximum_power_ratio = 0.0;
    int                                  result = 1;

    if (argc < 2 || argc > 7) {
        (void) fprintf(stderr, "usage: %s CONFIG.yaml [PROFILE [packet|kiss "
                               "[SECONDS|0 [EXPECTED_FRAMES|0 "
                               "[CAPTURE_PREFIX]]]]]\n",
                       argv[0]);
        return 2;
    }
    if (argc == 2) {
        return print_profile_list(argv[1]);
    }
    openlog(strstr(argv[0], "rtnc_modem") != NULL ? "rtnc_modem" : "rtnc_alsa_rx", LOG_PID | LOG_NDELAY, LOG_DAEMON);
    (void) signal(SIGINT, request_stop);
    (void) signal(SIGTERM, request_stop);
    profile_name = argv[2];
    if (profile_name[0] == '\0') {
        (void) fprintf(stderr, "invalid profile\n");
        return 2;
    }
    if (argc >= 4) {
        if (strcmp(argv[3], "packet") != 0 &&
            strcmp(argv[3], "kiss") != 0) {
            (void) fprintf(stderr, "invalid receive mode\n");
            return 2;
        }
        packet_mode = true;
        service_mode = strcmp(argv[3], "kiss") == 0;
    }
    if (argc >= 5) {
        char *end = NULL;
        run_seconds = strtod(argv[4], &end);
        if (end == argv[4] || *end != '\0' || run_seconds < 0.0 ||
            run_seconds > 86400.0) {
            (void) fprintf(stderr, "invalid duration\n");
            return 2;
        }
    }
    decoder.target_frames = 1U;
    if (argc >= 6) {
        char               *end = NULL;
        const unsigned long parsed = strtoul(argv[5], &end, 10);
        if (end == argv[5] || *end != '\0' || parsed > 1000UL) {
            (void) fprintf(stderr, "invalid expected frame count\n");
            return 2;
        }
        decoder.target_frames = (unsigned int) parsed;
    }
    if (argc == 7) {
        if (argv[6][0] == '\0') {
            (void) fprintf(stderr, "invalid capture prefix\n");
            return 2;
        }
        capture_prefix = argv[6];
    }
    atomic_init(&decoder.running, true);
    atomic_init(&equalizer_decoder.running, true);
    atomic_init(&decoded_frames, 0U);
    atomic_init(&completed_packets, 0U);
    atomic_init(&channel_busy, false);
    decoder.decoded_frames = &decoded_frames;
    decoder.worker_id = 0U;
    equalizer_decoder.decoded_frames = &decoded_frames;
    equalizer_decoder.worker_id = 1U;
    equalizer_decoder.fast_only = false;
    equalizer_decoder.target_frames = decoder.target_frames;
    decoder.packet_mode = packet_mode;
    equalizer_decoder.packet_mode = packet_mode;
    decoder.best_status = RTNC_MODEM_NO_FRAME;
    if (!rtnc_platform_config_load(argv[1], &config) ||
        !rtnc_platform_burst_config(config, &detector_config) ||
        !rtnc_burst_detector_init(&detector, &detector_config)) {
        (void) fprintf(stderr, "invalid detector configuration\n");
        goto done;
    }
    if (!rtnc_audio_ring_init(&audio_ring, config->workers.dsp_queue_blocks)) {
        (void) fprintf(stderr, "invalid audio ring capacity\n");
        goto done;
    }
    decoder.fast_only = false;
    decoder.fallback_queue = NULL;
    equalizer_decoder.nice_increment = config->workers.equalizer_nice;
    if (!rtnc_decode_queue_init(&decoder.queue, config->workers.decode_queue_frames)) {
        (void) fprintf(stderr, "invalid decode queue capacity\n");
        goto done;
    }
    if (config->workers.parallel_equalizer &&
        !rtnc_decode_queue_init(&equalizer_decoder.queue, config->workers.equalizer_queue_frames)) {
        (void) fprintf(stderr, "invalid equalizer queue capacity\n");
        goto done;
    }
    selected_profile = profile_name != NULL
                           ? rtnc_platform_profile_config_named(config, profile_name)
                           : rtnc_platform_selected_profile(config);
    if (selected_profile == NULL ||
        !rtnc_platform_phy_profile_named(config, selected_profile->name, &phy_profile) ||
        !rtnc_modem_profile_rate(&phy_profile, (fec_mode_t) selected_profile->fec_mode, selected_profile->payload_class_bytes, &profile_rate) ||
        !rtnc_modem_init_profile(&decoder.modem, (fec_mode_t) selected_profile->fec_mode, selected_profile->payload_class_bytes, &phy_profile)) {
        (void) fprintf(stderr, "modem initialization failed\n");
        goto done;
    }
    modem_initialized = true;
    if (config->workers.parallel_equalizer &&
        !rtnc_modem_init_profile(&equalizer_decoder.modem, (fec_mode_t) selected_profile->fec_mode, selected_profile->payload_class_bytes, &phy_profile)) {
        (void) fprintf(stderr, "equalizer modem initialization failed\n");
        goto done;
    }
    equalizer_modem_initialized = config->workers.parallel_equalizer;
    if (packet_mode &&
        !rtnc_completion_init(
            &completion_coordinator,
            (size_t) config->workers.decode_queue_frames +
                (size_t) config->workers.equalizer_queue_frames,
            0U,
            1U
        )) {
        (void) fprintf(stderr, "packet completion init failed\n");
        goto done;
    }
    if (packet_mode) {
        decoder.completion_coordinator = &completion_coordinator;
        equalizer_decoder.completion_coordinator = &completion_coordinator;
        decoder.generation = 1U;
        equalizer_decoder.generation = 1U;
    }
    if (equalizer_modem_initialized) {
        equalizer_decoder.modem.profile.acquisition_threshold =
            phy_profile.acquisition_threshold;
    }
    detector_config.capture_samples = detector_config.maximum_active_samples;
    detector_config.energy_trigger_enabled = false;
    detector_config.external_trigger_requires_energy = true;
    detector_config.cooldown_samples = 0U;
    {
        const size_t trigger_latency =
            (2U * RTNC_MODEM_RRC_DELAY_SYMBOLS +
             RTNC_MODEM_ACQUISITION_SYMBOLS - 1U) *
            decoder.modem.profile.samples_per_symbol;
        const size_t guard =
            4U * decoder.modem.profile.samples_per_symbol;
        const size_t frame_samples = rtnc_modem_frame_samples(&decoder.modem);
        detector_config.capture_samples =
            detector_config.pretrigger_samples + frame_samples + guard >
                    trigger_latency
                ? detector_config.pretrigger_samples + frame_samples + guard -
                      trigger_latency
                : 0U;
    }
    if (detector_config.capture_samples > detector_config.maximum_active_samples ||
        !rtnc_burst_detector_init(&detector, &detector_config)) {
        (void) fprintf(stderr, "derived detector window is invalid\n");
        goto done;
    }
    if (!rtnc_acquisition_detector_init(
            &acquisition_detector,
            &decoder.modem.profile,
            decoder.modem.training,
            4U,
            phy_profile.acquisition_threshold
        ) ||
        !rtnc_carrier_init(&power_carrier, config->audio.sample_rate_hz, decoder.modem.profile.carrier_hz)) {
        (void) fprintf(stderr, "acquisition detector initialization failed\n");
        goto done;
    }
    power_filter = firfilt_crcf_create_kaiser(
        129U,
        0.5F * (float) decoder.modem.profile.symbol_rate_baud *
            (1.0F + decoder.modem.profile.rrc_rolloff) *
            selected_profile->detector_bandwidth_margin /
            (float) decoder.modem.profile.sample_rate_hz,
        60.0F,
        0.0F
    );
    if (power_filter == NULL ||
        !rtnc_audio_init(&audio, &config->audio, &audio_ring) ||
        (packet_mode &&
         (!rtnc_ptt_init(&ptt, &config->ptt) ||
          !rtnc_alsa_runtime_backend(&runtime_context, &audio, &ptt, &channel_busy, &runtime_backend) ||
          !rtnc_runtime_init(
              &packet_runtime,
              &phy_profile,
              (fec_mode_t) selected_profile->fec_mode,
              selected_profile->payload_class_bytes,
              config->link.mtu,
              config->link.reassembly_timeout_ms,
              config->runtime.tx_queue_packets,
              config->runtime.rx_queue_packets,
              config->runtime.channel_busy_timeout_ms,
              config->runtime.rx_guard_ms,
              &config->tx,
              &runtime_backend
          )))) {
        (void) fprintf(stderr, "ALSA/filter initialization failed\n");
        goto done;
    }
    runtime_initialized = packet_mode;
    if (service_mode) {
        listen_socket = open_kiss_socket(config->runtime.kiss_tcp_port);
        rtnc_kiss_parser_init(&kiss_parser);
        atomic_init(&tx_worker.running, true);
        tx_worker.runtime = &packet_runtime;
        if (listen_socket < 0 ||
            pthread_create(&tx_thread, NULL, tx_worker_main, &tx_worker) != 0) {
            (void) fprintf(stderr, "packet runtime service initialization failed\n");
            goto done;
        }
        tx_thread_started = true;
    }
    if (config->workers.parallel_equalizer &&
        pthread_create(&equalizer_thread, NULL, decoder_thread_main, &equalizer_decoder) != 0) {
        (void) fprintf(stderr, "equalizer thread initialization failed\n");
        goto done;
    }
    equalizer_thread_started = config->workers.parallel_equalizer;
    if (pthread_create(&decoder_thread, NULL, decoder_thread_main, &decoder) !=
        0) {
        (void) fprintf(stderr, "decoder thread initialization failed\n");
        goto done;
    }
    thread_started = true;
    if (!rtnc_audio_start_capture(&audio)) {
        (void) fprintf(stderr, "capture start failed\n");
        goto done;
    }
    capture_started = true;
    (void) printf("listening audio=%s profile=%s baud=%u bitrate=%u "
                  "raw_bitrate=%u detector open=%.2f "
                  "close=%.2f "
                  "capture_samples=%zu parallel_eq=%d eq_nice=%u\n",
                  config->audio.device,
                  selected_profile->name,
                  selected_profile->symbol_rate_baud,
                  profile_rate.interface_bitrate_bps,
                  profile_rate.raw_bitrate_bps,
                  (double) detector_config.trigger_ratio,
                  (double) detector_config.release_ratio,
                  detector_config.capture_samples,
                  config->workers.parallel_equalizer ? 1 : 0,
                  (unsigned int) config->workers.equalizer_nice);
    (void) fflush(stdout);
    start = monotonic_seconds();
    while (!stop_requested &&
           (run_seconds == 0.0 || monotonic_seconds() - start < run_seconds) &&
           (service_mode ||
            ((!packet_mode &&
              (decoder.target_frames == 0U ||
               atomic_load_explicit(&decoded_frames, memory_order_acquire) <
                   decoder.target_frames)) ||
             (packet_mode &&
              (decoder.target_frames == 0U ||
               atomic_load_explicit(&completed_packets, memory_order_acquire) <
                   decoder.target_frames))))) {
        size_t index;
        if (service_mode) {
            uint8_t tcp_input[4096U];
            if (packet_client < 0) {
                packet_client = accept(listen_socket, NULL, NULL);
                if (packet_client >= 0) {
                    rtnc_kiss_parser_init(&kiss_parser);
                    syslog(LOG_INFO, "KISS TCP client connected");
                }
            }
            if (packet_client >= 0) {
                const ssize_t received =
                    recv(packet_client, tcp_input, sizeof(tcp_input), MSG_DONTWAIT);
                if (received > 0) {
                    ssize_t byte_index;
                    for (byte_index = 0; byte_index < received; ++byte_index) {
                        rtnc_kiss_frame_t kiss_frame;
                        if (rtnc_kiss_parser_push_frame(
                                &kiss_parser,
                                tcp_input[byte_index],
                                &kiss_frame
                            )) {
                            if (kiss_frame.port == 0U &&
                                kiss_frame.command ==
                                    RTNC_KISS_DATA_COMMAND &&
                                kiss_frame.length > 0U) {
                                const rtnc_runtime_status_t submit_status =
                                    rtnc_runtime_submit_packet(
                                        &packet_runtime,
                                        kiss_frame.data,
                                        kiss_frame.length
                                    );
                                syslog(submit_status == RTNC_RUNTIME_OK ? LOG_INFO : LOG_WARNING, "KISS tx enqueue bytes=%zu status=%d "
                                                                                                  "depth=%zu",
                                       kiss_frame.length,
                                       (int) submit_status,
                                       rtnc_packet_queue_depth(&packet_runtime.tx_queue));
                            } else if (kiss_frame.port == 0U && kiss_frame.length == 1U) {
                                const bool applied =
                                    rtnc_runtime_set_kiss_parameter(
                                        &packet_runtime,
                                        kiss_frame.command,
                                        kiss_frame.data[0]
                                    );
                                syslog(applied ? LOG_INFO : LOG_WARNING, "KISS command=%u value=%u applied=%d", (unsigned int) kiss_frame.command, (unsigned int) kiss_frame.data[0], applied ? 1 : 0);
                            }
                        }
                    }
                } else if (received == 0) {
                    (void) close(packet_client);
                    packet_client = -1;
                }
            }
        }
        if (!rtnc_audio_ring_pop(&audio_ring, &block)) {
            (void) nanosleep(&idle, NULL);
            continue;
        }
        for (index = 0U; index < block.count; ++index) {
            const float   sample = (float) block.samples[index] / 32768.0F;
            float complex mixed;
            float complex filtered;
            float         acquisition_score = 0.0F;
            const float  *candidate = NULL;
            size_t        candidate_count = 0U;
            const bool    correlation_trigger =
                rtnc_acquisition_detector_process(
                    &acquisition_detector,
                    sample,
                    &acquisition_score
                );
            (void) rtnc_carrier_downconvert(&power_carrier, sample, &mixed);
            (void) firfilt_crcf_push(power_filter, mixed);
            (void) firfilt_crcf_execute(power_filter, &filtered);
            if (rtnc_burst_detector_process_triggered(
                    &detector,
                    crealf(filtered) * crealf(filtered) +
                        cimagf(filtered) * cimagf(filtered),
                    sample,
                    correlation_trigger,
                    &candidate,
                    &candidate_count
                )) {
                if (capture_prefix != NULL) {
                    save_candidate_wav(capture_prefix, sequence, candidate, candidate_count, capture_pcm);
                }
                decoder_context_t *target_decoder =
                    config->workers.parallel_equalizer &&
                            (sequence & 1U) != 0U
                        ? &equalizer_decoder
                        : &decoder;
                const bool receive_enabled =
                    !service_mode ||
                    atomic_load_explicit(&packet_runtime.state, memory_order_acquire) ==
                        (int) RTNC_RUNTIME_RX_IDLE;
                if ((!receive_enabled ||
                     !rtnc_decode_queue_push(&target_decoder->queue, candidate, candidate_count, sequence)) &&
                    packet_mode) {
                    (void) rtnc_completion_submit(
                        &completion_coordinator,
                        sequence,
                        1U,
                        false,
                        NULL,
                        0U
                    );
                }
                sequence += 1U;
            }
            if (detector.noise_power > 0.0 &&
                detector.samples_seen >= detector.config.warmup_samples) {
                const double ratio =
                    detector.signal_power / detector.noise_power;
                if (ratio > maximum_power_ratio) {
                    maximum_power_ratio = ratio;
                }
                atomic_store_explicit(
                    &channel_busy,
                    detector.active,
                    memory_order_release
                );
            }
        }
        if (packet_mode) {
            rtnc_completion_t completion;
            while (rtnc_completion_pop(&completion_coordinator, &completion)) {
                if (completion.success) {
                    const rtnc_runtime_status_t fragment_status =
                        rtnc_runtime_accept_fragment(
                            &packet_runtime,
                            completion.payload,
                            completion.payload_length,
                            (uint64_t) (monotonic_seconds() * 1000.0)
                        );
                    if (fragment_status == RTNC_RUNTIME_OK) {
                        uint8_t reassembled_packet[RTNC_LINK_MAX_MTU];
                        size_t  packet_length = 0U;
                        if (rtnc_runtime_receive_packet(
                                &packet_runtime,
                                reassembled_packet,
                                sizeof(reassembled_packet),
                                &packet_length
                            ) !=
                            RTNC_RUNTIME_OK) {
                            continue;
                        }
                        const rtnc_runtime_stats_t *runtime_stats =
                            rtnc_runtime_get_stats(&packet_runtime);
                        const uint64_t rx_sequence =
                            runtime_stats->rx_packets - 1U;
                        (void) printf(
                            "reassembled rx_sequence=%llu bytes=%zu\n",
                            (unsigned long long) rx_sequence,
                            packet_length
                        );
                        syslog(LOG_INFO, "reassembled rx_sequence=%llu bytes=%zu", (unsigned long long) rx_sequence, packet_length);
                        if (service_mode && packet_client >= 0 &&
                            packet_length > 0U) {
                            uint8_t encoded[RTNC_KISS_MAX_ENCODED];
                            size_t  encoded_length = 0U;
                            if (!rtnc_kiss_encode(
                                    reassembled_packet,
                                    packet_length,
                                    encoded,
                                    sizeof(encoded),
                                    &encoded_length
                                ) ||
                                send(packet_client, encoded, encoded_length, MSG_NOSIGNAL) !=
                                    (ssize_t) encoded_length) {
                                (void) close(packet_client);
                                packet_client = -1;
                            }
                        }
                        (void) atomic_fetch_add_explicit(
                            &completed_packets,
                            1U,
                            memory_order_release
                        );
                    } else if (fragment_status != RTNC_RUNTIME_RX_INCOMPLETE) {
                        (void) printf("reassembly rejected status=%d\n", (int) fragment_status);
                        syslog(LOG_WARNING, "reassembly rejected status=%d sequence=%llu", (int) fragment_status, (unsigned long long) completion.job_id);
                    }
                }
            }
        }
    }
    if (service_mode) {
        result = 0;
    } else {
        const unsigned int successes =
            packet_mode
                ? atomic_load_explicit(&completed_packets, memory_order_acquire)
                : atomic_load_explicit(&decoded_frames, memory_order_acquire);
        result = successes > 0U ? 0 : 1;
    }

done:
    if (tx_thread_started) {
        atomic_store_explicit(&tx_worker.running, false, memory_order_release);
        (void) pthread_join(tx_thread, NULL);
    }
    if (capture_started) {
        rtnc_audio_stop_capture(&audio);
    }
    atomic_store_explicit(&decoder.running, false, memory_order_release);
    if (thread_started) {
        (void) pthread_join(decoder_thread, NULL);
    }
    atomic_store_explicit(&equalizer_decoder.running, false, memory_order_release);
    if (equalizer_thread_started) {
        (void) pthread_join(equalizer_thread, NULL);
    }
    (void) printf("finished decoded=%u/%u packets=%u bursts=%llu "
                  "decoder0_attempts=%u decoder1_attempts=%u "
                  "audio_drops=%llu "
                  "decode_drops=%llu capture_xruns=%llu best_status=%d "
                  "best_acquisition=%.3f best_training=%.3f\n",
                  atomic_load_explicit(&decoded_frames, memory_order_relaxed),
                  decoder.target_frames,
                  atomic_load_explicit(&completed_packets, memory_order_relaxed),
                  (unsigned long long) detector.detected_bursts,
                  decoder.attempts,
                  equalizer_decoder.attempts,
                  (unsigned long long) rtnc_audio_ring_dropped(&audio_ring),
                  (unsigned long long) rtnc_decode_queue_dropped(&decoder.queue),
                  (unsigned long long) rtnc_audio_capture_xruns(&audio),
                  (int) decoder.best_status,
                  (double) decoder.best_acquisition,
                  (double) decoder.best_training);
    (void) printf("audio_ring queue_max=%zu capacity=%zu\n", rtnc_audio_ring_maximum_depth(&audio_ring), audio_ring.capacity);
    (void) printf("decoder queue_max=%zu wall_ms=%.1f cpu_ms=%.1f "
                  "max_decode_ms=%.1f max_queue_ms=%.1f eq_successes=%u\n",
                  rtnc_decode_queue_maximum_depth(&decoder.queue),
                  decoder.decode_wall_seconds * 1000.0,
                  decoder.decode_cpu_seconds * 1000.0,
                  decoder.maximum_decode_seconds * 1000.0,
                  decoder.maximum_queue_seconds * 1000.0,
                  decoder.equalizer_successes);
    (void) printf("decoder1 queue_max=%zu drops=%llu wall_ms=%.1f cpu_ms=%.1f "
                  "max_decode_ms=%.1f max_queue_ms=%.1f successes=%u nice=%d\n",
                  rtnc_decode_queue_maximum_depth(&equalizer_decoder.queue),
                  (unsigned long long) rtnc_decode_queue_dropped(&equalizer_decoder.queue),
                  equalizer_decoder.decode_wall_seconds * 1000.0,
                  equalizer_decoder.decode_cpu_seconds * 1000.0,
                  equalizer_decoder.maximum_decode_seconds * 1000.0,
                  equalizer_decoder.maximum_queue_seconds * 1000.0,
                  equalizer_decoder.equalizer_successes,
                  equalizer_decoder.actual_nice);
    (void) printf("detector max_power_ratio=%.3f noise_power=%.9g "
                  "signal_power=%.9g best_stream_correlation=%.3f\n",
                  maximum_power_ratio,
                  detector.noise_power,
                  detector.signal_power,
                  (double) acquisition_detector.best_score);
    {
        const unsigned int snr_count =
            decoder.snr_count + equalizer_decoder.snr_count;
        const double snr_sum =
            decoder.snr_sum_db + equalizer_decoder.snr_sum_db;
        const float minimum_snr =
            decoder.snr_count == 0U
                ? equalizer_decoder.minimum_snr_db
            : equalizer_decoder.snr_count == 0U
                ? decoder.minimum_snr_db
                : fminf(decoder.minimum_snr_db, equalizer_decoder.minimum_snr_db);
        (void) printf("effective_snr_db average=%.2f minimum=%.2f samples=%u\n", snr_count > 0U ? snr_sum / (double) snr_count : 0.0, snr_count > 0U ? (double) minimum_snr : 0.0, snr_count);
        syslog(LOG_INFO, "finished packets=%u bursts=%llu decoder0_attempts=%u "
                         "decoder1_attempts=%u audio_drops=%llu decode0_drops=%llu "
                         "decode1_drops=%llu capture_xruns=%llu snr_average=%.2f "
                         "snr_minimum=%.2f snr_samples=%u",
               atomic_load_explicit(&completed_packets, memory_order_relaxed),
               (unsigned long long) detector.detected_bursts,
               decoder.attempts,
               equalizer_decoder.attempts,
               (unsigned long long) rtnc_audio_ring_dropped(&audio_ring),
               (unsigned long long) rtnc_decode_queue_dropped(&decoder.queue),
               (unsigned long long) rtnc_decode_queue_dropped(&equalizer_decoder.queue),
               (unsigned long long) rtnc_audio_capture_xruns(&audio),
               snr_count > 0U ? snr_sum / (double) snr_count : 0.0,
               snr_count > 0U ? (double) minimum_snr : 0.0,
               snr_count);
    }
    closelog();
    if (packet_client >= 0) {
        (void) close(packet_client);
    }
    if (listen_socket >= 0) {
        (void) close(listen_socket);
    }
    if (runtime_initialized) {
        rtnc_runtime_deinit(&packet_runtime);
    }
    rtnc_ptt_deinit(&ptt);
    rtnc_audio_deinit(&audio);
    if (power_filter != NULL) {
        (void) firfilt_crcf_destroy(power_filter);
    }
    rtnc_acquisition_detector_deinit(&acquisition_detector);
    rtnc_carrier_deinit(&power_carrier);
    if (modem_initialized) {
        rtnc_modem_deinit(&decoder.modem);
    }
    if (equalizer_modem_initialized) {
        rtnc_modem_deinit(&equalizer_decoder.modem);
    }
    rtnc_platform_config_free(config);
    return result;
}
