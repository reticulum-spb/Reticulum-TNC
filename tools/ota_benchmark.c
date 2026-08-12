#define _POSIX_C_SOURCE 200809L

#include "rtnc/acquisition.h"
#include "rtnc/audio.h"
#include "rtnc/audio_ring.h"
#include "rtnc/burst_detector.h"
#include "rtnc/decode_queue.h"
#include "rtnc/modem.h"
#include "rtnc/ota_benchmark.h"
#include "rtnc/platform_config.h"
#include "rtnc/ptt.h"
#include "rtnc/tx_eq.h"

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum { DEFAULT_ANNOUNCE_REPEATS = 5U,
       MAX_ANNOUNCE_REPEATS = 20U,
       MAX_PACKETS = 1000U,
       RESULT_SLOTS = 16U };

static _Atomic(rtnc_ptt_t *) active_ptt;

typedef struct {
    rtnc_ota_benchmark_message_t messages[RESULT_SLOTS];
    atomic_size_t                producer;
    atomic_size_t                consumer;
} benchmark_result_queue_t;

typedef struct {
    rtnc_modem_t                     modem;
    rtnc_modem_workspace_t           workspace;
    rtnc_acquisition_detector_t      acquisition;
    rtnc_burst_detector_t            burst;
    const rtnc_phy_profile_config_t *profile;
    bool                             modem_ready;
    unsigned int                     decode_attempts;
    unsigned int                     decoded_frames;
    unsigned int                     fec_failures;
    double                           evm_sum;
    double                           snr_sum_db;
    rtnc_decode_queue_t              decode_queue;
    rtnc_decode_job_t                decode_job;
    benchmark_result_queue_t         result_queue;
    atomic_bool                      worker_running;
    pthread_t                        worker;
    bool                             worker_started;
    pthread_mutex_t                  stats_mutex;
    bool                             stats_mutex_ready;
    uint64_t                         candidate_sequence;
} receiver_phy_t;

static void *signal_wait_main(void *argument) {
    sigset_t *signals = argument;
    int       signal_number = 0;
    if (sigwait(signals, &signal_number) == 0) {
        rtnc_ptt_t *ptt = atomic_load_explicit(&active_ptt, memory_order_acquire);
        if (ptt != NULL) {
            (void) rtnc_ptt_set(ptt, false);
        }
        _exit(128 + signal_number);
    }
    return NULL;
}

static uint64_t monotonic_ms(void) {
    struct timespec value = { 0 };
    (void) clock_gettime(CLOCK_MONOTONIC, &value);
    return (uint64_t) value.tv_sec * 1000U + (uint64_t) value.tv_nsec / 1000000U;
}

static bool sleep_ms(uint32_t milliseconds) {
    struct timespec delay = {
        .tv_sec = (time_t) (milliseconds / 1000U),
        .tv_nsec = (long) (milliseconds % 1000U) * 1000000L,
    };
    while (nanosleep(&delay, &delay) != 0) {
        if (errno != EINTR) {
            return false;
        }
    }
    return true;
}

static int16_t clamp_pcm(float value) {
    return value >= 32767.0F    ? 32767
           : value <= -32768.0F ? -32768
                                : (int16_t) lrintf(value);
}

static bool transmit_series(
    const rtnc_platform_config_t    *config,
    const rtnc_phy_profile_config_t *entry,
    rtnc_audio_t                    *audio,
    rtnc_ptt_t                      *ptt,
    rtnc_ota_benchmark_message_t    *message,
    unsigned int                     count,
    rtnc_preamble_t                  preamble
) {
    rtnc_phy_profile_t phy;
    rtnc_modem_t       modem;
    float              waveform[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    int16_t            raw[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    int16_t            pcm[RTNC_MODEM_MAX_AUDIO_SAMPLES];
    int16_t            silence[4800U] = { 0 };
    uint8_t            packet[128U];
    unsigned int       item;
    bool               success = false;
    if (!rtnc_platform_phy_profile_named(config, entry->name, &phy) ||
        !rtnc_modem_init_profile_preamble(
            &modem,
            (fec_mode_t) entry->fec_mode,
            entry->payload_class_bytes,
            &phy,
            preamble
        ) ||
        !rtnc_ptt_set(ptt, true) || !sleep_ms(config->tx.lead_ms)) {
        return false;
    }
    for (item = 0U; item < count; ++item) {
        size_t packet_length = 0U;
        size_t sample_count = 0U;
        size_t index;
        message->sequence = item;
        if (!rtnc_ota_benchmark_encode(message, packet, sizeof(packet), &packet_length) ||
            packet_length > entry->payload_class_bytes ||
            rtnc_modem_tx_audio(&modem, packet, packet_length, waveform, RTNC_MODEM_MAX_AUDIO_SAMPLES, &sample_count) !=
                RTNC_MODEM_OK) {
            goto done;
        }
        for (index = 0U; index < sample_count; ++index) {
            raw[index] = clamp_pcm(waveform[index] * 16383.0F);
        }
        for (index = 0U; index < sample_count; ++index) {
            pcm[index] = clamp_pcm(config->tx.filter_gain * rtnc_tx_eq_apply_sample(config->tx.response_eq_taps, raw, sample_count, index));
        }
        if (!rtnc_audio_send(audio, pcm, sample_count) ||
            (item + 1U < count &&
             !rtnc_audio_send(audio, silence, 4800U))) {
            goto done;
        }
    }
    success = rtnc_audio_wait(audio) && sleep_ms(config->tx.tail_ms);
done:
    if (!rtnc_ptt_set(ptt, false)) {
        success = false;
    }
    rtnc_modem_deinit(&modem);
    return success;
}

static int run_tx(
    const rtnc_platform_config_t *config,
    const char                   *control_name,
    size_t                        payload_size,
    unsigned int                  packet_count,
    unsigned int                  announce_repeats
) {
    const rtnc_phy_profile_config_t *control =
        rtnc_platform_profile_config_named(config, control_name);
    rtnc_audio_ring_t ring;
    rtnc_audio_t      audio = { 0 };
    rtnc_ptt_t        ptt = { 0 };
    uint32_t          run_id = (uint32_t) time(NULL);
    unsigned int      block;
    int               result = 1;
    if (control == NULL || payload_size < RTNC_OTA_BENCHMARK_HEADER_SIZE ||
        payload_size > 128U || packet_count == 0U ||
        packet_count > MAX_PACKETS || announce_repeats == 0U ||
        announce_repeats > MAX_ANNOUNCE_REPEATS) {
        (void) fprintf(stderr, "invalid TX benchmark configuration\n");
        return 2;
    }
    if (!rtnc_audio_ring_init(&ring, 2U) ||
        !rtnc_audio_init(&audio, &config->audio, &ring) ||
        !rtnc_ptt_init(&ptt, &config->ptt)) {
        (void) fprintf(stderr, "TX platform initialization failed\n");
        goto done;
    }
    atomic_store_explicit(&active_ptt, &ptt, memory_order_release);
    for (block = 0U; block < config->profiles_count; ++block) {
        const rtnc_phy_profile_config_t *test = &config->profiles[block];
        rtnc_ota_benchmark_message_t     message = {
                .type = RTNC_OTA_BENCHMARK_ANNOUNCE,
                .run_id = run_id,
                .block_id = block,
                .packet_count = packet_count,
                .payload_size = (uint32_t) payload_size,
                .seed = UINT32_C(0x6d2b79f5) ^ run_id ^ block,
                .guard_ms = 5000U,
        };
        (void) snprintf(message.profile, sizeof(message.profile), "%s", test->name);
        if (payload_size > test->payload_class_bytes) {
            (void) printf("block=%u profile=%s skipped=frame_size_exceeds_payload_class\n", block, test->name);
            continue;
        }
        if (!transmit_series(config, control, &audio, &ptt, &message, announce_repeats, RTNC_PREAMBLE_CONTROL)) {
            (void) fprintf(stderr, "control announcement failed\n");
            goto done;
        }
        if (!sleep_ms(message.guard_ms)) {
            goto done;
        }
        message.type = RTNC_OTA_BENCHMARK_DATA;
        if (!transmit_series(config, test, &audio, &ptt, &message, packet_count, RTNC_PREAMBLE_DATA)) {
            goto done;
        }
        (void) printf("block=%u profile=%s sent=%u bytes=%zu\n", block, test->name, packet_count, payload_size);
        (void) fflush(stdout);
        (void) sleep_ms(1000U);
    }
    result = 0;
done:
    atomic_store_explicit(&active_ptt, NULL, memory_order_release);
    rtnc_ptt_deinit(&ptt);
    rtnc_audio_deinit(&audio);
    return result;
}

static bool result_push(benchmark_result_queue_t *queue, const rtnc_ota_benchmark_message_t *message) {
    const size_t producer = atomic_load_explicit(&queue->producer, memory_order_relaxed);
    const size_t next = (producer + 1U) % RESULT_SLOTS;
    if (next == atomic_load_explicit(&queue->consumer, memory_order_acquire)) {
        return false;
    }
    queue->messages[producer] = *message;
    atomic_store_explicit(&queue->producer, next, memory_order_release);
    return true;
}

static bool result_pop(benchmark_result_queue_t *queue, rtnc_ota_benchmark_message_t *message) {
    const size_t consumer = atomic_load_explicit(&queue->consumer, memory_order_relaxed);
    if (consumer == atomic_load_explicit(&queue->producer, memory_order_acquire)) {
        return false;
    }
    *message = queue->messages[consumer];
    atomic_store_explicit(&queue->consumer, (consumer + 1U) % RESULT_SLOTS, memory_order_release);
    return true;
}

static void *receiver_worker_main(void *argument) {
    receiver_phy_t *receiver = argument;
    while (atomic_load_explicit(&receiver->worker_running, memory_order_acquire) ||
           rtnc_decode_queue_depth(&receiver->decode_queue) > 0U) {
        uint8_t                      payload[RTNC_FRAME_MAX_PAYLOAD];
        size_t                       payload_length = 0U;
        rtnc_sync_metrics_t          metrics = { 0 };
        rtnc_modem_status_t          status;
        rtnc_ota_benchmark_message_t message;
        if (!rtnc_decode_queue_pop(&receiver->decode_queue, &receiver->decode_job)) {
            (void) sleep_ms(1U);
            continue;
        }
        status = rtnc_modem_rx_audio(
            &receiver->modem,
            receiver->decode_job.samples,
            receiver->decode_job.count,
            payload,
            sizeof(payload),
            &payload_length,
            &metrics,
            &receiver->workspace
        );
        (void) pthread_mutex_lock(&receiver->stats_mutex);
        receiver->decode_attempts += 1U;
        if (status == RTNC_MODEM_FRAME_REJECTED && metrics.frame_detected) {
            receiver->fec_failures += 1U;
        }
        if (status == RTNC_MODEM_OK) {
            receiver->decoded_frames += 1U;
            receiver->evm_sum += metrics.evm_rms;
            if (isfinite(metrics.training_snr_db)) {
                receiver->snr_sum_db += metrics.training_snr_db;
            }
        }
        (void) pthread_mutex_unlock(&receiver->stats_mutex);
        if (status != RTNC_MODEM_OK) {
            continue;
        }
        if (rtnc_ota_benchmark_decode(payload, payload_length, &message)) {
            (void) result_push(&receiver->result_queue, &message);
        }
    }
    return NULL;
}

static void receiver_phy_deinit(receiver_phy_t *receiver) {
    if (receiver->worker_started) {
        atomic_store_explicit(&receiver->worker_running, false, memory_order_release);
        (void) pthread_join(receiver->worker, NULL);
        receiver->worker_started = false;
    }
    if (receiver->modem_ready) {
        rtnc_acquisition_detector_deinit(&receiver->acquisition);
        rtnc_modem_deinit(&receiver->modem);
        receiver->modem_ready = false;
    }
}

static bool receiver_phy_init(
    receiver_phy_t                  *receiver,
    const rtnc_platform_config_t    *config,
    const rtnc_phy_profile_config_t *entry,
    rtnc_preamble_t                  preamble
) {
    rtnc_phy_profile_t           phy;
    rtnc_burst_detector_config_t detector_config;
    size_t                       trigger_latency;
    size_t                       guard;
    receiver_phy_deinit(receiver);
    if (!receiver->stats_mutex_ready) {
        if (pthread_mutex_init(&receiver->stats_mutex, NULL) != 0) {
            return false;
        }
        receiver->stats_mutex_ready = true;
    }
    if (!rtnc_platform_phy_profile_named(config, entry->name, &phy) ||
        !rtnc_modem_init_profile_preamble(
            &receiver->modem,
            (fec_mode_t) entry->fec_mode,
            entry->payload_class_bytes,
            &phy,
            preamble
        ) ||
        !rtnc_platform_burst_config(config, &detector_config)) {
        return false;
    }
    receiver->modem_ready = true;
    detector_config.energy_trigger_enabled = false;
    detector_config.external_trigger_requires_energy = false;
    detector_config.cooldown_samples = 0U;
    trigger_latency = (2U * RTNC_MODEM_RRC_DELAY_SYMBOLS +
                       RTNC_MODEM_ACQUISITION_SYMBOLS - 1U) *
                      phy.samples_per_symbol;
    guard = 4U * phy.samples_per_symbol;
    detector_config.capture_samples =
        detector_config.pretrigger_samples +
                    rtnc_modem_frame_samples(&receiver->modem) + guard >
                trigger_latency
            ? detector_config.pretrigger_samples +
                  rtnc_modem_frame_samples(&receiver->modem) + guard -
                  trigger_latency
            : 0U;
    if (detector_config.capture_samples > detector_config.maximum_active_samples ||
        !rtnc_burst_detector_init(&receiver->burst, &detector_config) ||
        !rtnc_acquisition_detector_init_modem(
            &receiver->acquisition,
            &receiver->modem,
            4U
        )) {
        receiver_phy_deinit(receiver);
        return false;
    }
    receiver->profile = entry;
    receiver->decode_attempts = 0U;
    receiver->decoded_frames = 0U;
    receiver->fec_failures = 0U;
    receiver->evm_sum = 0.0;
    receiver->snr_sum_db = 0.0;
    receiver->candidate_sequence = 0U;
    atomic_init(&receiver->result_queue.producer, 0U);
    atomic_init(&receiver->result_queue.consumer, 0U);
    atomic_init(&receiver->worker_running, true);
    if (!rtnc_decode_queue_init(&receiver->decode_queue, config->workers.decode_queue_frames) ||
        pthread_create(&receiver->worker, NULL, receiver_worker_main, receiver) != 0) {
        receiver_phy_deinit(receiver);
        return false;
    }
    receiver->worker_started = true;
    return true;
}

static void write_report(
    FILE                               *report,
    const rtnc_ota_benchmark_message_t *active,
    unsigned int                        received,
    unsigned int                        duplicates,
    uint64_t                            started_ms,
    receiver_phy_t                     *receiver,
    const rtnc_audio_t                 *audio,
    const char                         *completion
) {
    const uint64_t elapsed = monotonic_ms() - started_ms;
    const double   goodput = elapsed > 0U
                                 ? (double) received * active->payload_size *
                                     1000.0 / (double) elapsed
                                 : 0.0;
    unsigned int   decode_attempts;
    unsigned int   decoded_frames;
    unsigned int   fec_failures;
    double         evm_sum;
    double         snr_sum_db;
    (void) pthread_mutex_lock(&receiver->stats_mutex);
    decode_attempts = receiver->decode_attempts;
    decoded_frames = receiver->decoded_frames;
    fec_failures = receiver->fec_failures;
    evm_sum = receiver->evm_sum;
    snr_sum_db = receiver->snr_sum_db;
    (void) pthread_mutex_unlock(&receiver->stats_mutex);
    (void) fprintf(report, "%u,%u,%s,%u,%u,%u,%u,%llu,%.3f,%u,%u,%u,%.4f,%.3f,%llu,%s\n", active->run_id, active->block_id, active->profile, active->payload_size, active->packet_count, received, duplicates, (unsigned long long) elapsed, goodput, decode_attempts, decoded_frames, fec_failures, decoded_frames > 0U ? evm_sum / decoded_frames : 0.0, decoded_frames > 0U ? snr_sum_db / decoded_frames : 0.0, (unsigned long long) rtnc_audio_capture_xruns(audio), completion);
    (void) fflush(report);
    (void) printf("result profile=%s received=%u/%u goodput=%.1f bytes/s\n", active->profile, received, active->packet_count, goodput);
}

static void receiver_phy_capture_triggered(receiver_phy_t *receiver, float sample, bool trigger) {
    const float *candidate = NULL;
    size_t       candidate_count = 0U;
    if (!rtnc_burst_detector_process_triggered(
            &receiver->burst,
            sample * sample,
            sample,
            trigger,
            &candidate,
            &candidate_count
        )) {
        return;
    }
    (void) rtnc_decode_queue_push(
        &receiver->decode_queue,
        candidate,
        candidate_count,
        receiver->candidate_sequence++
    );
}

static int run_rx(
    const rtnc_platform_config_t *config,
    const char                   *control_name,
    const char                   *report_name
) {
    const rtnc_phy_profile_config_t *control =
        rtnc_platform_profile_config_named(config, control_name);
    static receiver_phy_t        control_receiver;
    static receiver_phy_t        data_receiver;
    static rtnc_audio_ring_t     ring;
    rtnc_audio_t                 audio = { 0 };
    rtnc_audio_block_t           block;
    rtnc_ota_benchmark_message_t active = { 0 };
    bool                         seen[MAX_PACKETS] = { false };
    unsigned int                 received = 0U;
    unsigned int                 duplicates = 0U;
    uint64_t                     block_started = 0U;
    uint64_t                     deadline = 0U;
    bool                         data_listening = false;
    FILE                        *report = NULL;
    int                          result = 1;
    if (control == NULL || !rtnc_audio_ring_init(&ring, config->workers.dsp_queue_blocks) ||
        !receiver_phy_init(&control_receiver, config, control, RTNC_PREAMBLE_CONTROL) ||
        !rtnc_audio_init(&audio, &config->audio, &ring)) {
        (void) fprintf(stderr, "RX platform initialization failed\n");
        goto done;
    }
    report = fopen(report_name, "a+");
    if (report == NULL) {
        (void) fprintf(stderr, "cannot open report: %s\n", report_name);
        goto done;
    }
    if (ftell(report) == 0L) {
        (void) fprintf(report, "run_id,block_id,profile,payload_bytes,expected,received,duplicates,elapsed_ms,goodput_bytes_s,decode_attempts,decoded_frames,fec_failures,mean_evm,mean_effective_snr_db,capture_xruns,completion\n");
    }
    if (!rtnc_audio_start_capture(&audio)) {
        goto done;
    }
    (void) printf("benchmark RX control_profile=%s report=%s\n", control_name, report_name);
    for (;;) {
        size_t sample_index;
        if (deadline != 0U && monotonic_ms() > deadline) {
            write_report(report, &active, received, duplicates, block_started, &data_receiver, &audio, "timeout");
            (void) memset(seen, 0, sizeof(seen));
            received = 0U;
            duplicates = 0U;
            deadline = 0U;
            data_listening = false;
        }
        if (!rtnc_audio_ring_pop(&ring, &block)) {
            (void) sleep_ms(1U);
            continue;
        }
        for (sample_index = 0U; sample_index < block.count; ++sample_index) {
            const float sample =
                (float) block.samples[sample_index] / 32768.0F;
            rtnc_modem_t *selected = NULL;
            float         control_score;
            float         data_score;
            const bool    trigger = rtnc_acquisition_detector_process_two(
                &control_receiver.acquisition,
                data_listening ? &data_receiver.acquisition : NULL,
                sample,
                &selected,
                &control_score,
                &data_score
            );
            receiver_phy_capture_triggered(
                &control_receiver,
                sample,
                trigger && selected == &control_receiver.modem
            );
            if (data_listening) {
                receiver_phy_capture_triggered(
                    &data_receiver,
                    sample,
                    trigger && selected == &data_receiver.modem
                );
            }
        }
        {
            rtnc_ota_benchmark_message_t message;
            while (result_pop(&control_receiver.result_queue, &message)) {
                if (message.type != RTNC_OTA_BENCHMARK_ANNOUNCE ||
                    message.packet_count == 0U ||
                    message.packet_count > MAX_PACKETS ||
                    message.payload_size > 128U) {
                    continue;
                }
                const rtnc_phy_profile_config_t *test =
                    rtnc_platform_profile_config_named(config, message.profile);
                if (test != NULL &&
                    message.payload_size > test->payload_class_bytes) {
                    test = NULL;
                }
                if (test != NULL &&
                    (deadline == 0U || message.run_id != active.run_id ||
                     message.block_id != active.block_id)) {
                    if (deadline != 0U) {
                        if (data_receiver.modem_ready) {
                            receiver_phy_deinit(&data_receiver);
                        }
                        write_report(
                            report,
                            &active,
                            received,
                            duplicates,
                            block_started,
                            &data_receiver,
                            &audio,
                            "next_announce"
                        );
                        (void) memset(seen, 0, sizeof(seen));
                        received = 0U;
                        duplicates = 0U;
                    } else if (data_receiver.modem_ready) {
                        receiver_phy_deinit(&data_receiver);
                    }
                    active = message;
                    block_started = monotonic_ms();
                    deadline = block_started + 15000U;
                    (void) printf(
                        "attempt announced run=%u block=%u profile=%s "
                        "expected=%u payload_bytes=%u\n",
                        message.run_id,
                        message.block_id,
                        message.profile,
                        message.packet_count,
                        message.payload_size
                    );
                    (void) fflush(stdout);
                    if (!receiver_phy_init(&data_receiver, config, test, RTNC_PREAMBLE_DATA)) {
                        goto done;
                    }
                    data_listening = true;
                }
            }
            while (data_listening &&
                   result_pop(&data_receiver.result_queue, &message)) {
                if (message.run_id != active.run_id ||
                    message.block_id != active.block_id ||
                    strcmp(message.profile, active.profile) != 0) {
                    continue;
                }
                if (message.type == RTNC_OTA_BENCHMARK_DATA &&
                    message.sequence < active.packet_count) {
                    if (seen[message.sequence]) {
                        duplicates += 1U;
                    } else {
                        seen[message.sequence] = true;
                        received += 1U;
                    }
                    deadline = monotonic_ms() + 15000U;
                }
            }
        }
    }
    result = 0;
done:
    rtnc_audio_stop_capture(&audio);
    if (report != NULL) {
        (void) fclose(report);
    }
    rtnc_audio_deinit(&audio);
    receiver_phy_deinit(&data_receiver);
    receiver_phy_deinit(&control_receiver);
    return result;
}

int main(int argc, char **argv) {
    rtnc_platform_config_t *config = NULL;
    const char             *control_name;
    sigset_t                signals;
    pthread_t               signal_thread;
    int                     result;
    if (argc < 5 || argc > 7 ||
        (strcmp(argv[2], "rx") != 0 && strcmp(argv[2], "tx") != 0)) {
        (void) fprintf(stderr, "usage: %s CONFIG.yaml rx CONTROL_PROFILE REPORT.csv\n"
                               "       %s CONFIG.yaml tx CONTROL_PROFILE PAYLOAD_BYTES COUNT [ANNOUNCE_REPEATS]\n",
                       argv[0],
                       argv[0]);
        return 2;
    }
    (void) sigemptyset(&signals);
    (void) sigaddset(&signals, SIGINT);
    (void) sigaddset(&signals, SIGTERM);
    if (pthread_sigmask(SIG_BLOCK, &signals, NULL) != 0 ||
        pthread_create(&signal_thread, NULL, signal_wait_main, &signals) != 0) {
        (void) fprintf(stderr, "signal safety initialization failed\n");
        return 1;
    }
    if (!rtnc_platform_config_load(argv[1], &config)) {
        (void) fprintf(stderr, "invalid configuration\n");
        return 2;
    }
    control_name = argv[3];
    if (strcmp(argv[2], "rx") == 0) {
        result = argc == 5 ? run_rx(config, control_name, argv[4]) : 2;
    } else {
        result = argc >= 6
                     ? run_tx(config, control_name, (size_t) strtoul(argv[4], NULL, 10), (unsigned int) strtoul(argv[5], NULL, 10), argc == 7 ? (unsigned int) strtoul(argv[6], NULL, 10) : DEFAULT_ANNOUNCE_REPEATS)
                     : 2;
    }
    rtnc_platform_config_free(config);
    return result;
}
