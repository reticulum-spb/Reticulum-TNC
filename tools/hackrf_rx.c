#define _POSIX_C_SOURCE 200809L

#include "rtnc/acquisition.h"
#include "rtnc/burst_detector.h"
#include "rtnc/decode_queue.h"
#include "rtnc/fragmentation.h"
#include "rtnc/wav.h"

#include <libhackrf/hackrf.h>
#include <liquid/liquid.h>

#include <complex.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    IQ_RATE = 2000000U,
    AUDIO_RATE = 48000U,
    IQ_BLOCK_BYTES = 262144U,
    IQ_RING_CAPACITY = 4U,
    IQ_RING_SLOTS = IQ_RING_CAPACITY + 1U,
    IQ_WORK_SAMPLES = 2048U,
    AUDIO_WORK_SAMPLES = 128U,
};

typedef struct {
    uint8_t data[IQ_BLOCK_BYTES];
    size_t  count;
} iq_block_t;

typedef struct {
    iq_block_t           blocks[IQ_RING_SLOTS];
    atomic_size_t        producer;
    atomic_size_t        consumer;
    atomic_uint_fast64_t drops;
} iq_ring_t;

typedef struct {
    iq_ring_t ring;
} rx_context_t;

typedef struct {
    rtnc_decode_queue_t    queue;
    rtnc_modem_t           modem;
    rtnc_modem_workspace_t workspace;
    rtnc_decode_job_t      job;
    atomic_bool            running;
    atomic_uint            decoded_frames;
    atomic_uint            completed_packets;
    unsigned int           target_frames;
    bool                   packet_mode;
    rtnc_reassembly_t      reassembly;
    uint8_t                packet[RTNC_LINK_MAX_MTU];
    unsigned int           attempts;
    float                  best_acquisition;
    float                  best_training;
    rtnc_modem_status_t    best_status;
    double                 decode_wall_seconds;
    double                 decode_cpu_seconds;
    double                 maximum_decode_seconds;
    double                 maximum_queue_seconds;
    unsigned int           equalizer_successes;
    const char            *capture_prefix;
    int16_t                capture_pcm[RTNC_MODEM_MAX_AUDIO_SAMPLES];
} decoder_context_t;

static void save_diagnostic_capture(decoder_context_t *decoder, rtnc_modem_status_t status, const rtnc_sync_metrics_t *metrics) {
    char   wav_path[512];
    char   metadata_path[512];
    FILE  *stream;
    size_t index;
    int    length;
    if (decoder->capture_prefix == NULL || metrics == NULL) {
        return;
    }
    length = snprintf(wav_path, sizeof(wav_path), "%s_%03llu.wav", decoder->capture_prefix, (unsigned long long) decoder->job.sequence);
    if (length < 0 || (size_t) length >= sizeof(wav_path)) {
        return;
    }
    for (index = 0U; index < decoder->job.count; ++index) {
        const float scaled = decoder->job.samples[index] * 32768.0F;
        decoder->capture_pcm[index] = (int16_t) lrintf(
            fmaxf(-32768.0F, fminf(32767.0F, scaled))
        );
    }
    stream = fopen(wav_path, "wb");
    if (stream == NULL) {
        (void) fprintf(stderr, "could not save diagnostic WAV %s\n", wav_path);
        return;
    }
    if (rtnc_wav_write_mono_s16(stream, AUDIO_RATE, decoder->capture_pcm, decoder->job.count) != RTNC_WAV_OK) {
        (void) fclose(stream);
        (void) fprintf(stderr, "could not save diagnostic WAV %s\n", wav_path);
        return;
    }
    if (fclose(stream) != 0) {
        (void) fprintf(stderr, "could not finish diagnostic WAV %s\n", wav_path);
        return;
    }
    length = snprintf(metadata_path, sizeof(metadata_path), "%s_%03llu.txt", decoder->capture_prefix, (unsigned long long) decoder->job.sequence);
    if (length < 0 || (size_t) length >= sizeof(metadata_path)) {
        return;
    }
    stream = fopen(metadata_path, "w");
    if (stream == NULL) {
        (void) fprintf(stderr, "could not save diagnostic metadata %s\n", metadata_path);
        return;
    }
    (void) fprintf(stream, "sequence=%llu\nsamples=%zu\nstatus=%d\n"
                           "frame_detected=%d\ntiming_symbols=%.9g\n"
                           "cfo_hz=%.9g\nphase_radians=%.9g\nevm=%.9g\n"
                           "acquisition=%.9g\ntraining=%.9g\nequalizer_used=%d\n"
                           "equalizer_training_error=%.9g\nfec_converged=%d\n"
                           "fec_iterations=%u\nequalizer_taps=",
                   (unsigned long long) decoder->job.sequence,
                   decoder->job.count,
                   (int) status,
                   metrics->frame_detected ? 1 : 0,
                   (double) metrics->timing_symbols,
                   (double) metrics->carrier_offset_hz,
                   (double) metrics->phase_radians,
                   (double) metrics->evm_rms,
                   (double) metrics->acquisition_correlation,
                   (double) metrics->training_correlation,
                   metrics->equalizer_used ? 1 : 0,
                   (double) metrics->equalizer_training_error,
                   decoder->workspace.fec_stats.converged ? 1 : 0,
                   decoder->workspace.fec_stats.iterations);
    for (index = 0U; index < RTNC_EQUALIZER_DIAGNOSTIC_TAPS; ++index) {
        (void) fprintf(stream, "%s%.9g%+.9gj", index == 0U ? "" : ",", (double) crealf(metrics->equalizer_taps[index]), (double) cimagf(metrics->equalizer_taps[index]));
    }
    (void) fprintf(stream, "\n");
    if (fclose(stream) != 0) {
        (void) fprintf(stderr, "could not finish diagnostic metadata %s\n", metadata_path);
    }
}

static int receive_callback(hackrf_transfer *transfer) {
    rx_context_t *context = transfer != NULL ? transfer->rx_ctx : NULL;
    size_t        producer;
    size_t        next;
    size_t        consumer;
    if (context == NULL || transfer->buffer == NULL ||
        transfer->valid_length <= 0 ||
        (size_t) transfer->valid_length > IQ_BLOCK_BYTES) {
        return 0;
    }
    producer = atomic_load_explicit(&context->ring.producer, memory_order_relaxed);
    next = (producer + 1U) % IQ_RING_SLOTS;
    consumer = atomic_load_explicit(&context->ring.consumer, memory_order_acquire);
    if (next == consumer) {
        (void) atomic_fetch_add_explicit(&context->ring.drops, 1U, memory_order_relaxed);
        return 0;
    }
    context->ring.blocks[producer].count = (size_t) transfer->valid_length;
    (void) memcpy(context->ring.blocks[producer].data, transfer->buffer, (size_t) transfer->valid_length);
    atomic_store_explicit(&context->ring.producer, next, memory_order_release);
    return 0;
}

static bool ring_pop(iq_ring_t *ring, iq_block_t *output) {
    const size_t consumer = atomic_load_explicit(&ring->consumer, memory_order_relaxed);
    const size_t producer = atomic_load_explicit(&ring->producer, memory_order_acquire);
    if (consumer == producer) {
        return false;
    }
    *output = ring->blocks[consumer];
    atomic_store_explicit(&ring->consumer, (consumer + 1U) % IQ_RING_SLOTS, memory_order_release);
    return true;
}

static double monotonic_seconds(void) {
    struct timespec value;
    (void) clock_gettime(CLOCK_MONOTONIC, &value);
    return (double) value.tv_sec + (double) value.tv_nsec / 1.0e9;
}

static double thread_cpu_seconds(void) {
    struct timespec value = { 0 };
    (void) clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value);
    return (double) value.tv_sec + (double) value.tv_nsec / 1.0e9;
}

static void *decoder_thread_main(void *argument) {
    const struct timespec idle = { .tv_sec = 0, .tv_nsec = 1000000L };
    decoder_context_t    *decoder = argument;
    while (atomic_load_explicit(&decoder->running, memory_order_acquire) ||
           rtnc_decode_queue_depth(&decoder->queue) > 0U) {
        uint8_t             payload[128U];
        size_t              payload_length = 0U;
        rtnc_sync_metrics_t metrics = { 0 };
        rtnc_modem_status_t status;
        size_t              index;
        double              wall_start;
        double              cpu_start;
        double              elapsed;
        if (!rtnc_decode_queue_pop(&decoder->queue, &decoder->job)) {
            (void) nanosleep(&idle, NULL);
            continue;
        }
        wall_start = monotonic_seconds();
        cpu_start = thread_cpu_seconds();
        if (decoder->job.enqueued_monotonic_ns > 0U) {
            const double queued =
                wall_start -
                (double) decoder->job.enqueued_monotonic_ns / 1.0e9;
            if (queued > decoder->maximum_queue_seconds) {
                decoder->maximum_queue_seconds = queued;
            }
        }
        status = rtnc_modem_rx_audio(
            &decoder->modem,
            decoder->job.samples,
            decoder->job.count,
            payload,
            sizeof(payload),
            &payload_length,
            &metrics,
            &decoder->workspace
        );
        save_diagnostic_capture(decoder, status, &metrics);
        elapsed = monotonic_seconds() - wall_start;
        decoder->decode_wall_seconds += elapsed;
        decoder->decode_cpu_seconds += thread_cpu_seconds() - cpu_start;
        if (elapsed > decoder->maximum_decode_seconds) {
            decoder->maximum_decode_seconds = elapsed;
        }
        if (metrics.equalizer_used) {
            decoder->equalizer_successes += 1U;
        }
        decoder->attempts += 1U;
        if (metrics.acquisition_correlation > decoder->best_acquisition) {
            decoder->best_acquisition = metrics.acquisition_correlation;
            decoder->best_training = metrics.training_correlation;
            decoder->best_status = status;
        }
        if (status == RTNC_MODEM_OK) {
            (void) printf("decoded sequence=%llu bytes=%zu acquisition=%.3f "
                          "training=%.3f cfo=%.1f evm=%.3f snr=%.2f "
                          "attempts=%u\n",
                          (unsigned long long) decoder->job.sequence,
                          payload_length,
                          (double) metrics.acquisition_correlation,
                          (double) metrics.training_correlation,
                          (double) metrics.carrier_offset_hz,
                          (double) metrics.evm_rms,
                          (double) metrics.training_snr_db,
                          decoder->attempts);
            (void) printf("payload=");
            for (index = 0U; index < payload_length; ++index) {
                (void) printf("%02x", (unsigned int) payload[index]);
            }
            (void) printf("\n");
            (void) fflush(stdout);
            (void) printf("sequence_number=%u\n", ((unsigned int) payload[0] << 24U) | ((unsigned int) payload[1] << 16U) | ((unsigned int) payload[2] << 8U) | (unsigned int) payload[3]);
            if (decoder->packet_mode) {
                size_t         packet_length = 0U;
                const uint64_t now_ms =
                    (uint64_t) (monotonic_seconds() * 1000.0);
                const rtnc_fragment_status_t fragment_status =
                    rtnc_reassembly_push(&decoder->reassembly, payload, payload_length, now_ms, &packet_length);
                if (fragment_status == RTNC_FRAGMENT_OK) {
                    const unsigned int packet_number =
                        ((unsigned int) decoder->packet[0] << 24U) |
                        ((unsigned int) decoder->packet[1] << 16U) |
                        ((unsigned int) decoder->packet[2] << 8U) |
                        (unsigned int) decoder->packet[3];
                    (void) printf("reassembled packet=%u bytes=%zu\n", packet_number, packet_length);
                    (void) atomic_fetch_add_explicit(
                        &decoder->completed_packets,
                        1U,
                        memory_order_release
                    );
                } else if (fragment_status != RTNC_FRAGMENT_INCOMPLETE) {
                    (void) printf("reassembly rejected status=%d\n", (int) fragment_status);
                }
            } else {
                (void) atomic_fetch_add_explicit(&decoder->decoded_frames, 1U, memory_order_release);
            }
            if ((!decoder->packet_mode &&
                 atomic_load_explicit(&decoder->decoded_frames, memory_order_acquire) >=
                     decoder->target_frames) ||
                (decoder->packet_mode &&
                 atomic_load_explicit(&decoder->completed_packets, memory_order_acquire) >=
                     decoder->target_frames)) {
                break;
            }
        }
    }
    return NULL;
}

int main(int argc, char **argv) {
    const struct timespec              idle = { .tv_sec = 0, .tv_nsec = 1000000L };
    const double                       pi = 3.14159265358979323846;
    uint64_t                           frequency_hz = 446006250U;
    uint32_t                           lna_gain = 16U;
    uint32_t                           vga_gain = 8U;
    double                             frequency_offset_hz = 0.0;
    unsigned int                       expected_frames = 1U;
    size_t                             packet_mtu = 0U;
    size_t                             radio_payload_class = 64U;
    fec_mode_t                         fec_mode = FEC_LDPC_ROBUST;
    rtnc_modulation_t                  modulation = RTNC_MODULATION_QPSK;
    uint32_t                           symbol_rate_baud = 1200U;
    float                              carrier_hz = 1650.0F;
    float                              rrc_rolloff = 0.25F;
    float                              acquisition_threshold = 0.90F;
    float                              training_threshold = 0.70F;
    rtnc_phy_profile_t                 phy_profile;
    hackrf_device                     *device = NULL;
    static rx_context_t                context;
    static iq_block_t                  block;
    liquid_float_complex               iq[IQ_WORK_SAMPLES];
    liquid_float_complex               channel[AUDIO_WORK_SAMPLES];
    float                              demodulated[AUDIO_WORK_SAMPLES];
    static rtnc_burst_detector_t       burst_detector;
    static rtnc_acquisition_detector_t acquisition_detector;
    rtnc_burst_detector_config_t       burst_config;
    static decoder_context_t           decoder;
    pthread_t                          decoder_thread;
    msresamp_crcf                      resampler = NULL;
    freqdem                            demodulator = NULL;
    bool                               modem_initialized = false;
    bool                               decoder_thread_started = false;
    double                             mixer_phase = 0.0;
    double                             best_iq_dbfs = -200.0;
    double                             noise_energy_sum = 0.0;
    uint64_t                           noise_sample_count = 0U;
    double                             start;
    double                             receive_timeout_seconds = 30.0;
    uint64_t                           decode_sequence = 0U;
    int                                result = 1;

    if (argc > 1) {
        frequency_hz = strtoull(argv[1], NULL, 10);
    }
    if (argc > 2) {
        lna_gain = (uint32_t) strtoul(argv[2], NULL, 10);
    }
    if (argc > 3) {
        vga_gain = (uint32_t) strtoul(argv[3], NULL, 10);
    }
    if (argc > 4) {
        frequency_offset_hz = strtod(argv[4], NULL);
    }
    if (argc > 5) {
        expected_frames = (unsigned int) strtoul(argv[5], NULL, 10);
    }
    if (argc > 6) {
        if (strcmp(argv[6], "qpsk") == 0) {
            modulation = RTNC_MODULATION_QPSK;
        } else if (strcmp(argv[6], "8psk") == 0) {
            modulation = RTNC_MODULATION_8PSK;
        } else {
            modulation = (rtnc_modulation_t) 99;
        }
    }
    if (argc > 7) {
        symbol_rate_baud = (uint32_t) strtoul(argv[7], NULL, 10);
    }
    if (argc > 8) {
        carrier_hz = strtof(argv[8], NULL);
    }
    if (argc > 9) {
        rrc_rolloff = strtof(argv[9], NULL);
    }
    if (argc > 10) {
        packet_mtu = (size_t) strtoul(argv[10], NULL, 10);
    }
    if (argc > 11) {
        radio_payload_class = (size_t) strtoul(argv[11], NULL, 10);
    }
    if (argc > 12) {
        fec_mode = (fec_mode_t) strtoul(argv[12], NULL, 10);
    }
    if (argc > 13) {
        acquisition_threshold = strtof(argv[13], NULL);
    }
    if (argc > 14) {
        training_threshold = strtof(argv[14], NULL);
    }
    if (argc > 16 || frequency_hz == 0U || expected_frames == 0U ||
        expected_frames > 1000U) {
        (void) fprintf(stderr, "usage: %s [FREQ_HZ [LNA [VGA [OFFSET_HZ "
                               "[EXPECTED_FRAMES [qpsk|8psk [SYMBOL_RATE "
                               "[CARRIER_HZ [RRC_ROLLOFF [PACKET_MTU "
                               "[PAYLOAD_CLASS [FEC_MODE "
                               "[ACQUISITION_THRESHOLD "
                               "[TRAINING_THRESHOLD [CAPTURE_PREFIX]]]]]]]]]]]]]]]\n",
                       argv[0]);
        return 2;
    }
    if (!rtnc_phy_profile_psk(modulation, symbol_rate_baud, carrier_hz, &phy_profile) ||
        rrc_rolloff <= 0.0F || rrc_rolloff > 1.0F ||
        acquisition_threshold <= 0.0F || acquisition_threshold > 1.0F ||
        training_threshold <= 0.0F || training_threshold > 1.0F) {
        (void) fprintf(stderr, "invalid PSK profile\n");
        return 2;
    }
    phy_profile.rrc_rolloff = rrc_rolloff;
    phy_profile.acquisition_threshold = acquisition_threshold;
    phy_profile.training_threshold = training_threshold;
    atomic_init(&context.ring.producer, 0U);
    atomic_init(&context.ring.consumer, 0U);
    atomic_init(&context.ring.drops, 0U);
    if (!rtnc_decode_queue_init(&decoder.queue, RTNC_DECODE_QUEUE_MAX_CAPACITY)) {
        (void) fprintf(stderr, "decode queue initialization failed\n");
        goto done;
    }
    rtnc_burst_detector_default_config(&burst_config);
    atomic_init(&decoder.running, true);
    atomic_init(&decoder.decoded_frames, 0U);
    atomic_init(&decoder.completed_packets, 0U);
    decoder.capture_prefix = argc > 15 ? argv[15] : NULL;
    decoder.target_frames = expected_frames;
    decoder.packet_mode = packet_mtu != 0U;
    decoder.best_status = RTNC_MODEM_NO_FRAME;
    resampler = msresamp_crcf_create((float) AUDIO_RATE / (float) IQ_RATE, 80.0F);
    demodulator = freqdem_create(1.0F);
    if (resampler == NULL || demodulator == NULL ||
        (fec_mode != FEC_LDPC_ROBUST && fec_mode != FEC_LDPC_NORMAL) ||
        !rtnc_modem_init_profile(&decoder.modem, fec_mode, (uint8_t) radio_payload_class, &phy_profile)) {
        (void) fprintf(stderr, "DSP initialization failed\n");
        goto done;
    }
    modem_initialized = true;
    if (decoder.packet_mode &&
        rtnc_reassembly_init(&decoder.reassembly, decoder.packet, packet_mtu, radio_payload_class, 10000U) != RTNC_FRAGMENT_OK) {
        (void) fprintf(stderr, "invalid packet MTU for radio profile\n");
        goto done;
    }
    if (decoder.packet_mode) {
        const size_t fragments =
            rtnc_fragment_count(packet_mtu, radio_payload_class);
        const double expected_airtime =
            (double) expected_frames * (double) fragments *
            (double) rtnc_modem_frame_samples(&decoder.modem) /
            (double) AUDIO_RATE;
        const double packet_timeout = expected_airtime + 20.0;
        if (packet_timeout > receive_timeout_seconds) {
            receive_timeout_seconds = packet_timeout;
        }
    }
    burst_config.energy_trigger_enabled = false;
    burst_config.external_trigger_requires_energy = true;
    burst_config.cooldown_samples = 0U;
    {
        const size_t trigger_latency =
            (2U * RTNC_MODEM_RRC_DELAY_SYMBOLS +
             RTNC_MODEM_ACQUISITION_SYMBOLS - 1U) *
            decoder.modem.profile.samples_per_symbol;
        const size_t guard =
            4U * decoder.modem.profile.samples_per_symbol;
        const size_t frame_samples = rtnc_modem_frame_samples(&decoder.modem);
        burst_config.capture_samples =
            burst_config.pretrigger_samples + frame_samples + guard >
                    trigger_latency
                ? burst_config.pretrigger_samples + frame_samples + guard -
                      trigger_latency
                : 0U;
    }
    if (!rtnc_burst_detector_init(&burst_detector, &burst_config) ||
        !rtnc_acquisition_detector_init(
            &acquisition_detector,
            &decoder.modem.profile,
            decoder.modem.training,
            4U,
            decoder.modem.profile.acquisition_threshold
        )) {
        (void) fprintf(stderr, "streaming detector initialization failed\n");
        goto done;
    }
    if (pthread_create(&decoder_thread, NULL, decoder_thread_main, &decoder) !=
        0) {
        (void) fprintf(stderr, "decoder thread initialization failed\n");
        goto done;
    }
    decoder_thread_started = true;
    if (hackrf_init() != HACKRF_SUCCESS || hackrf_open(&device) != HACKRF_SUCCESS ||
        hackrf_set_sample_rate(device, (double) IQ_RATE) != HACKRF_SUCCESS ||
        hackrf_set_freq(device, frequency_hz) != HACKRF_SUCCESS ||
        hackrf_set_amp_enable(device, 0U) != HACKRF_SUCCESS ||
        hackrf_set_lna_gain(device, lna_gain) != HACKRF_SUCCESS ||
        hackrf_set_vga_gain(device, vga_gain) != HACKRF_SUCCESS ||
        hackrf_start_rx(device, receive_callback, &context) != HACKRF_SUCCESS) {
        (void) fprintf(stderr, "HackRF initialization failed\n");
        goto done;
    }
    (void) printf("listening freq=%llu rate=%u lna=%u vga=%u offset=%.1f "
                  "modulation=%s baud=%u carrier=%.1f alpha=%.2f "
                  "timeout=%.1f\n",
                  (unsigned long long) frequency_hz,
                  IQ_RATE,
                  lna_gain,
                  vga_gain,
                  frequency_offset_hz,
                  modulation == RTNC_MODULATION_8PSK ? "8psk" : "qpsk",
                  symbol_rate_baud,
                  (double) carrier_hz,
                  (double) rrc_rolloff,
                  receive_timeout_seconds);
    (void) fflush(stdout);
    start = monotonic_seconds();
    while (monotonic_seconds() - start < receive_timeout_seconds &&
           ((!decoder.packet_mode &&
             atomic_load_explicit(&decoder.decoded_frames, memory_order_acquire) <
                 decoder.target_frames) ||
            (decoder.packet_mode &&
             atomic_load_explicit(&decoder.completed_packets, memory_order_acquire) <
                 decoder.target_frames))) {
        size_t byte_offset;
        double block_energy = 0.0;
        size_t block_iq_samples = 0U;
        if (!ring_pop(&context.ring, &block)) {
            (void) nanosleep(&idle, NULL);
            continue;
        }
        for (byte_offset = 0U; byte_offset + 1U < block.count;) {
            const size_t available = (block.count - byte_offset) / 2U;
            const size_t input_count =
                available < IQ_WORK_SAMPLES ? available : IQ_WORK_SAMPLES;
            unsigned int output_count = 0U;
            size_t       index;
            for (index = 0U; index < input_count; ++index) {
                const float i = (float) (int8_t) block.data[byte_offset + 2U * index] /
                                128.0F;
                const float q =
                    (float) (int8_t) block.data[byte_offset + 2U * index + 1U] /
                    128.0F;
                block_energy += (double) i * (double) i + (double) q * (double) q;
                block_iq_samples += 1U;
                const liquid_float_complex mixer =
                    (float) cos(mixer_phase) + (float) sin(mixer_phase) * I;
                iq[index] = (i + q * I) * mixer;
                mixer_phase += -2.0 * pi * frequency_offset_hz / (double) IQ_RATE;
                if (mixer_phase > pi) {
                    mixer_phase -= 2.0 * pi;
                } else if (mixer_phase < -pi) {
                    mixer_phase += 2.0 * pi;
                }
            }
            byte_offset += input_count * 2U;
            (void) msresamp_crcf_execute(resampler, iq, (unsigned int) input_count, channel, &output_count);
            if (output_count > AUDIO_WORK_SAMPLES) {
                (void) fprintf(stderr, "resampler output overflow\n");
                goto streaming_done;
            }
            (void) freqdem_demodulate_block(demodulator, channel, output_count, demodulated);
            for (index = 0U; index < output_count; ++index) {
                const float sample_power =
                    crealf(channel[index]) * crealf(channel[index]) +
                    cimagf(channel[index]) * cimagf(channel[index]);
                const float *candidate = NULL;
                size_t       candidate_count = 0U;
                float        correlation_score = 0.0F;
                const bool   correlation_trigger =
                    rtnc_acquisition_detector_process(
                        &acquisition_detector,
                        demodulated[index],
                        &correlation_score
                    );
                if (rtnc_burst_detector_process_triggered(
                        &burst_detector,
                        sample_power,
                        demodulated[index],
                        correlation_trigger,
                        &candidate,
                        &candidate_count
                    )) {
                    (void) rtnc_decode_queue_push(&decoder.queue, candidate, candidate_count, decode_sequence);
                    decode_sequence += 1U;
                }
            }
        }
        if (block_iq_samples > 0U) {
            const double block_mean_energy =
                block_energy / (double) block_iq_samples;
            const double block_dbfs =
                20.0 * log10(sqrt(block_mean_energy));
            if (monotonic_seconds() - start < 0.75) {
                noise_energy_sum += block_energy;
                noise_sample_count += block_iq_samples;
            }
            if (block_dbfs > best_iq_dbfs) {
                best_iq_dbfs = block_dbfs;
            }
        }
    }

streaming_done:
    (void) hackrf_stop_rx(device);
    atomic_store_explicit(&decoder.running, false, memory_order_release);
    if (decoder_thread_started) {
        (void) pthread_join(decoder_thread, NULL);
        decoder_thread_started = false;
    }
    result = (decoder.packet_mode
                  ? atomic_load_explicit(&decoder.completed_packets, memory_order_acquire)
                  : atomic_load_explicit(&decoder.decoded_frames, memory_order_acquire)) > 0U
                 ? 0
                 : 1;
    {
        const double noise_dbfs =
            noise_sample_count > 0U && noise_energy_sum > 0.0
                ? 10.0 * log10(noise_energy_sum / (double) noise_sample_count)
                : -200.0;
        (void) printf("finished decoded=%u/%u packets=%u bursts=%llu attempts=%u iq_drops=%llu "
                      "decode_drops=%llu "
                      "best_status=%d best_acquisition=%.3f best_training=%.3f "
                      "best_iq_dbfs=%.2f noise_iq_dbfs=%.2f "
                      "carrier_over_noise_db=%.2f best_stream_correlation=%.3f\n",
                      atomic_load_explicit(&decoder.decoded_frames, memory_order_relaxed),
                      decoder.target_frames,
                      atomic_load_explicit(&decoder.completed_packets, memory_order_relaxed),
                      (unsigned long long) burst_detector.detected_bursts,
                      decoder.attempts,
                      (unsigned long long) atomic_load_explicit(&context.ring.drops, memory_order_relaxed),
                      (unsigned long long) rtnc_decode_queue_dropped(&decoder.queue),
                      (int) decoder.best_status,
                      (double) decoder.best_acquisition,
                      (double) decoder.best_training,
                      best_iq_dbfs,
                      noise_dbfs,
                      best_iq_dbfs - noise_dbfs,
                      (double) acquisition_detector.best_score);
    }
    (void) printf("decoder queue_max=%zu wall_ms=%.1f cpu_ms=%.1f "
                  "max_decode_ms=%.1f max_queue_ms=%.1f eq_successes=%u\n",
                  rtnc_decode_queue_maximum_depth(&decoder.queue),
                  decoder.decode_wall_seconds * 1000.0,
                  decoder.decode_cpu_seconds * 1000.0,
                  decoder.maximum_decode_seconds * 1000.0,
                  decoder.maximum_queue_seconds * 1000.0,
                  decoder.equalizer_successes);

done:
    atomic_store_explicit(&decoder.running, false, memory_order_release);
    if (decoder_thread_started) {
        (void) pthread_join(decoder_thread, NULL);
    }
    if (device != NULL) {
        (void) hackrf_close(device);
    }
    (void) hackrf_exit();
    if (modem_initialized) {
        rtnc_modem_deinit(&decoder.modem);
    }
    rtnc_acquisition_detector_deinit(&acquisition_detector);
    if (demodulator != NULL) {
        (void) freqdem_destroy(demodulator);
    }
    if (resampler != NULL) {
        (void) msresamp_crcf_destroy(resampler);
    }
    return result;
}
