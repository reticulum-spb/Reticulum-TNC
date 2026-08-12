#define _POSIX_C_SOURCE 200809L

#include "rtnc/acquisition.h"
#include "rtnc/audio.h"
#include "rtnc/audio_ring.h"
#include "rtnc/burst_detector.h"
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

enum { ANNOUNCE_REPEATS = 5U,
       END_REPEATS = 3U,
       MAX_PACKETS = 1000U };

static _Atomic(rtnc_ptt_t *) active_ptt;

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
    bool                             append_end
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
        !rtnc_modem_init_profile(&modem, (fec_mode_t) entry->fec_mode, entry->payload_class_bytes, &phy) ||
        !rtnc_ptt_set(ptt, true) || !sleep_ms(config->tx.lead_ms)) {
        return false;
    }
    for (item = 0U; item < count + (append_end ? END_REPEATS : 0U); ++item) {
        size_t packet_length = 0U;
        size_t sample_count = 0U;
        size_t index;
        message->type = append_end && item >= count
                            ? RTNC_OTA_BENCHMARK_END
                            : message->type;
        message->sequence = item < count ? item : count;
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
            (item + 1U < count + (append_end ? END_REPEATS : 0U) &&
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
    unsigned int                  packet_count
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
        packet_count > MAX_PACKETS) {
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
                .guard_ms = 1500U,
        };
        (void) snprintf(message.profile, sizeof(message.profile), "%s", test->name);
        if (payload_size > test->payload_class_bytes) {
            (void) printf("block=%u profile=%s skipped=frame_size_exceeds_payload_class\n", block, test->name);
            continue;
        }
        if (!transmit_series(config, control, &audio, &ptt, &message, ANNOUNCE_REPEATS, false)) {
            (void) fprintf(stderr, "control announcement failed\n");
            goto done;
        }
        if (!sleep_ms(message.guard_ms)) {
            goto done;
        }
        message.type = RTNC_OTA_BENCHMARK_DATA;
        if (!transmit_series(config, test, &audio, &ptt, &message, packet_count, true)) {
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

static void receiver_phy_deinit(receiver_phy_t *receiver) {
    if (receiver->modem_ready) {
        rtnc_acquisition_detector_deinit(&receiver->acquisition);
        rtnc_modem_deinit(&receiver->modem);
        receiver->modem_ready = false;
    }
}

static bool receiver_phy_init(
    receiver_phy_t                  *receiver,
    const rtnc_platform_config_t    *config,
    const rtnc_phy_profile_config_t *entry
) {
    rtnc_phy_profile_t           phy;
    rtnc_burst_detector_config_t detector_config;
    size_t                       trigger_latency;
    size_t                       guard;
    receiver_phy_deinit(receiver);
    if (!rtnc_platform_phy_profile_named(config, entry->name, &phy) ||
        !rtnc_modem_init_profile(&receiver->modem, (fec_mode_t) entry->fec_mode, entry->payload_class_bytes, &phy) ||
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
        !rtnc_acquisition_detector_init(&receiver->acquisition, &phy, receiver->modem.training, 4U, phy.acquisition_threshold)) {
        receiver_phy_deinit(receiver);
        return false;
    }
    receiver->profile = entry;
    receiver->decode_attempts = 0U;
    receiver->decoded_frames = 0U;
    receiver->fec_failures = 0U;
    receiver->evm_sum = 0.0;
    receiver->snr_sum_db = 0.0;
    return true;
}

static void write_report(
    FILE                               *report,
    const rtnc_ota_benchmark_message_t *active,
    unsigned int                        received,
    unsigned int                        duplicates,
    uint64_t                            started_ms,
    const receiver_phy_t               *receiver,
    const rtnc_audio_t                 *audio,
    const char                         *completion
) {
    const uint64_t elapsed = monotonic_ms() - started_ms;
    const double   goodput = elapsed > 0U
                                 ? (double) received * active->payload_size *
                                     1000.0 / (double) elapsed
                                 : 0.0;
    (void) fprintf(report, "%u,%u,%s,%u,%u,%u,%u,%llu,%.3f,%u,%u,%u,%.4f,%.3f,%llu,%s\n", active->run_id, active->block_id, active->profile, active->payload_size, active->packet_count, received, duplicates, (unsigned long long) elapsed, goodput, receiver->decode_attempts, receiver->decoded_frames, receiver->fec_failures, receiver->decoded_frames > 0U ? receiver->evm_sum / receiver->decoded_frames : 0.0, receiver->decoded_frames > 0U ? receiver->snr_sum_db / receiver->decoded_frames : 0.0, (unsigned long long) rtnc_audio_capture_xruns(audio), completion);
    (void) fflush(report);
    (void) printf("result profile=%s received=%u/%u goodput=%.1f bytes/s\n", active->profile, received, active->packet_count, goodput);
}

static int run_rx(
    const rtnc_platform_config_t *config,
    const char                   *control_name,
    const char                   *report_name
) {
    const rtnc_phy_profile_config_t *control =
        rtnc_platform_profile_config_named(config, control_name);
    static receiver_phy_t        receiver;
    static rtnc_audio_ring_t     ring;
    rtnc_audio_t                 audio = { 0 };
    rtnc_audio_block_t           block;
    rtnc_ota_benchmark_message_t active = { 0 };
    bool                         seen[MAX_PACKETS] = { false };
    unsigned int                 received = 0U;
    unsigned int                 duplicates = 0U;
    uint64_t                     block_started = 0U;
    uint64_t                     deadline = 0U;
    FILE                        *report = NULL;
    int                          result = 1;
    if (control == NULL || !rtnc_audio_ring_init(&ring, config->workers.dsp_queue_blocks) ||
        !receiver_phy_init(&receiver, config, control) ||
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
            write_report(report, &active, received, duplicates, block_started, &receiver, &audio, "timeout");
            (void) memset(seen, 0, sizeof(seen));
            received = 0U;
            duplicates = 0U;
            deadline = 0U;
            if (!receiver_phy_init(&receiver, config, control)) {
                goto done;
            }
        }
        if (!rtnc_audio_ring_pop(&ring, &block)) {
            (void) sleep_ms(1U);
            continue;
        }
        for (sample_index = 0U; sample_index < block.count; ++sample_index) {
            const float  sample = (float) block.samples[sample_index] / 32768.0F;
            const float *candidate = NULL;
            size_t       candidate_count = 0U;
            float        score = 0.0F;
            const bool   trigger = rtnc_acquisition_detector_process(
                &receiver.acquisition,
                sample,
                &score
            );
            if (rtnc_burst_detector_process_triggered(
                    &receiver.burst,
                    sample * sample,
                    sample,
                    trigger,
                    &candidate,
                    &candidate_count
                )) {
                uint8_t                   fragment[RTNC_FRAME_MAX_PAYLOAD];
                size_t                    fragment_length = 0U;
                rtnc_sync_metrics_t       metrics = { 0 };
                const rtnc_modem_status_t decode_status = rtnc_modem_rx_audio(
                    &receiver.modem,
                    candidate,
                    candidate_count,
                    fragment,
                    sizeof(fragment),
                    &fragment_length,
                    &metrics,
                    &receiver.workspace
                );
                receiver.decode_attempts += 1U;
                if (decode_status == RTNC_MODEM_OK) {
                    receiver.decoded_frames += 1U;
                    receiver.evm_sum += metrics.evm_rms;
                    if (isfinite(metrics.training_snr_db)) {
                        receiver.snr_sum_db += metrics.training_snr_db;
                    }
                    rtnc_ota_benchmark_message_t message;
                    if (!rtnc_ota_benchmark_decode(fragment, fragment_length, &message)) {
                        continue;
                    }
                    if (message.type == RTNC_OTA_BENCHMARK_ANNOUNCE &&
                        deadline == 0U && message.packet_count > 0U &&
                        message.packet_count <= MAX_PACKETS &&
                        message.payload_size <= 128U) {
                        const rtnc_phy_profile_config_t *test =
                            rtnc_platform_profile_config_named(
                                config,
                                message.profile
                            );
                        if (test == NULL ||
                            message.payload_size >
                                test->payload_class_bytes) {
                            continue;
                        }
                        active = message;
                        block_started = monotonic_ms();
                        deadline = block_started + 15000U;
                        (void) printf("announce run=%u block=%u profile=%s count=%u bytes=%u\n", message.run_id, message.block_id, message.profile, message.packet_count, message.payload_size);
                        if (!receiver_phy_init(&receiver, config, test)) {
                            goto done;
                        }
                        break;
                    }
                    if (deadline != 0U &&
                        message.run_id == active.run_id &&
                        message.block_id == active.block_id &&
                        strcmp(message.profile, active.profile) == 0) {
                        if (message.type == RTNC_OTA_BENCHMARK_DATA &&
                            message.sequence < active.packet_count) {
                            if (seen[message.sequence]) {
                                duplicates += 1U;
                            } else {
                                seen[message.sequence] = true;
                                received += 1U;
                            }
                            deadline = monotonic_ms() + 15000U;
                        } else if (message.type == RTNC_OTA_BENCHMARK_END) {
                            write_report(report, &active, received, duplicates, block_started, &receiver, &audio, "end");
                            (void) memset(seen, 0, sizeof(seen));
                            received = 0U;
                            duplicates = 0U;
                            deadline = 0U;
                            if (!receiver_phy_init(&receiver, config, control)) {
                                goto done;
                            }
                            break;
                        }
                    }
                } else if (decode_status == RTNC_MODEM_FRAME_REJECTED && metrics.frame_detected) {
                    receiver.fec_failures += 1U;
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
    receiver_phy_deinit(&receiver);
    return result;
}

int main(int argc, char **argv) {
    rtnc_platform_config_t *config = NULL;
    const char             *control_name;
    sigset_t                signals;
    pthread_t               signal_thread;
    int                     result;
    if (argc < 5 || argc > 6 ||
        (strcmp(argv[2], "rx") != 0 && strcmp(argv[2], "tx") != 0)) {
        (void) fprintf(stderr, "usage: %s CONFIG.yaml rx CONTROL_PROFILE REPORT.csv\n"
                               "       %s CONFIG.yaml tx CONTROL_PROFILE PAYLOAD_BYTES COUNT\n",
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
        result = argc == 6
                     ? run_tx(config, control_name, (size_t) strtoul(argv[4], NULL, 10), (unsigned int) strtoul(argv[5], NULL, 10))
                     : 2;
    }
    rtnc_platform_config_free(config);
    return result;
}
