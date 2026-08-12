#define _POSIX_C_SOURCE 200809L

#include <libhackrf/hackrf.h>
#include <liquid/liquid.h>

#include "rtnc/tx_eq.h"

#include <complex.h>
#include <math.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    IQ_RATE = 2000000U,
    SAMPLE_RATE = 48000U,
    RX_SECONDS = 36U,
    MARKER_HZ = 1000U,
    MARKER_MS = 800U,
    MARKER_GAP_MS = 200U,
    TONE_MS = 250U,
    GAP_MS = 50U,
    LEVEL_GAP_MS = 300U,
    TONE_COUNT = 8U,
    LEVEL_COUNT = 4U,
    IQ_BLOCK_BYTES = 262144U,
    IQ_RING_SLOTS = 5U,
    IQ_WORK_SAMPLES = 2048U,
    AUDIO_WORK_SAMPLES = 128U,
};

static const unsigned int tones[TONE_COUNT] = {
    600U,
    900U,
    1200U,
    1500U,
    1800U,
    2100U,
    2400U,
    2700U,
};
static const int levels[LEVEL_COUNT] = { 4000, 8000, 12000, 16000 };

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

static int receive_callback(hackrf_transfer *transfer) {
    iq_ring_t *ring = transfer != NULL ? transfer->rx_ctx : NULL;
    size_t     producer;
    size_t     next;
    size_t     consumer;
    if (ring == NULL || transfer->buffer == NULL || transfer->valid_length <= 0 ||
        (size_t) transfer->valid_length > IQ_BLOCK_BYTES) {
        return 0;
    }
    producer = atomic_load_explicit(&ring->producer, memory_order_relaxed);
    next = (producer + 1U) % IQ_RING_SLOTS;
    consumer = atomic_load_explicit(&ring->consumer, memory_order_acquire);
    if (next == consumer) {
        (void) atomic_fetch_add_explicit(&ring->drops, 1U, memory_order_relaxed);
        return 0;
    }
    ring->blocks[producer].count = (size_t) transfer->valid_length;
    (void) memcpy(ring->blocks[producer].data, transfer->buffer, (size_t) transfer->valid_length);
    atomic_store_explicit(&ring->producer, next, memory_order_release);
    return 0;
}

static bool ring_pop(iq_ring_t *ring, iq_block_t *output) {
    const size_t consumer =
        atomic_load_explicit(&ring->consumer, memory_order_relaxed);
    const size_t producer =
        atomic_load_explicit(&ring->producer, memory_order_acquire);
    if (consumer == producer) {
        return false;
    }
    *output = ring->blocks[consumer];
    atomic_store_explicit(&ring->consumer, (consumer + 1U) % IQ_RING_SLOTS, memory_order_release);
    return true;
}

static double monotonic_seconds(void) {
    struct timespec value = { 0 };
    (void) clock_gettime(CLOCK_MONOTONIC, &value);
    return (double) value.tv_sec + (double) value.tv_nsec / 1.0e9;
}

static double tone_amplitude(const int16_t *samples, size_t count, unsigned int frequency) {
    const double pi = 3.14159265358979323846;
    const double step = 2.0 * pi * (double) frequency / (double) SAMPLE_RATE;
    double       real_part = 0.0;
    double       imaginary_part = 0.0;
    size_t       index;
    for (index = 0U; index < count; ++index) {
        const double phase = step * (double) index;
        real_part += (double) samples[index] * cos(phase);
        imaginary_part -= (double) samples[index] * sin(phase);
    }
    return 2.0 * hypot(real_part, imaginary_part) / (double) count;
}

static bool find_marker(const int16_t *samples, size_t count, size_t *marker_start) {
    const size_t window = SAMPLE_RATE / 10U;
    const size_t step = window / 2U;
    unsigned int consecutive = 0U;
    size_t       offset;
    for (offset = 0U; offset + window <= count; offset += step) {
        const double marker = tone_amplitude(&samples[offset], window, MARKER_HZ);
        const double adjacent = tone_amplitude(&samples[offset], window, 700U) +
                                tone_amplitude(&samples[offset], window, 1300U) +
                                1.0;
        if (marker > 100.0 && marker / adjacent > 4.0) {
            ++consecutive;
            if (consecutive == 5U) {
                *marker_start = offset - 4U * step;
                return true;
            }
        } else {
            consecutive = 0U;
        }
    }
    return false;
}

static unsigned int select_linear_level(
    double amplitudes[LEVEL_COUNT][TONE_COUNT],
    double distortion[LEVEL_COUNT][TONE_COUNT]
) {
    unsigned int selected =
        distortion[1][1] <= 0.25 ? 1U : 0U;
    unsigned int level;
    unsigned int tone;
    for (level = 2U; level < LEVEL_COUNT; ++level) {
        bool linear = true;
        if (selected == 0U) {
            break;
        }
        for (tone = 0U; tone < TONE_COUNT; ++tone) {
            const double reference = amplitudes[1][tone] / (double) levels[1];
            const double normalized = amplitudes[level][tone] /
                                      (double) levels[level];
            if (reference <= 0.0 || normalized < 0.85 * reference) {
                linear = false;
            }
            if (tones[tone] == 900U && distortion[level][tone] > 0.25) {
                linear = false;
            }
        }
        if (linear) {
            selected = level;
        }
    }
    return selected;
}

static int analyze(const int16_t *samples, size_t count, unsigned int baud, double carrier, double rolloff, uint64_t drops) {
    size_t       marker;
    double       amplitudes[LEVEL_COUNT][TONE_COUNT] = { { 0.0 } };
    double       distortion[LEVEL_COUNT][TONE_COUNT] = { { 0.0 } };
    unsigned int selected;
    unsigned int level;
    unsigned int tone;
    float        eq_taps[RTNC_TX_EQ_TAP_COUNT];
    double       gain;
    if (!find_marker(samples, count, &marker)) {
        (void) fprintf(stderr, "calibration marker not found\n");
        return 1;
    }
    {
        const size_t sweep_start =
            marker + (MARKER_MS + MARKER_GAP_MS) * SAMPLE_RATE / 1000U;
        const size_t analysis_skip = 40U * SAMPLE_RATE / 1000U;
        const size_t analysis_count = 160U * SAMPLE_RATE / 1000U;
        for (level = 0U; level < LEVEL_COUNT; ++level) {
            for (tone = 0U; tone < TONE_COUNT; ++tone) {
                const size_t tone_index = level * TONE_COUNT + tone;
                const size_t offset =
                    sweep_start + tone_index * (TONE_MS + GAP_MS) * SAMPLE_RATE / 1000U +
                    level * LEVEL_GAP_MS * SAMPLE_RATE / 1000U +
                    analysis_skip;
                if (offset + analysis_count > count) {
                    (void) fprintf(stderr, "incomplete calibration cycle\n");
                    return 1;
                }
                amplitudes[level][tone] =
                    tone_amplitude(&samples[offset], analysis_count, tones[tone]);
                if (amplitudes[level][tone] > 0.0) {
                    const double harmonic2 =
                        tone_amplitude(&samples[offset], analysis_count, 2U * tones[tone]);
                    const double harmonic3 =
                        tone_amplitude(&samples[offset], analysis_count, 3U * tones[tone]);
                    distortion[level][tone] =
                        hypot(harmonic2, harmonic3) /
                        amplitudes[level][tone];
                }
            }
        }
    }
    selected = select_linear_level(amplitudes, distortion);
    if (distortion[selected][1U] > 0.25) {
        (void) fprintf(
            stderr,
            "calibration rejected: 900 Hz distortion %.1f%% at minimum "
            "usable level PCM%d; reduce the hardware audio gain and retry\n",
            100.0 * distortion[selected][1U],
            levels[selected]
        );
        return 1;
    }
    if (!rtnc_tx_eq_design(
            tones,
            amplitudes[selected],
            TONE_COUNT,
            (float) carrier,
            (float) (carrier - 0.5 * (double) baud * (1.0 + rolloff)),
            (float) (carrier + 0.5 * (double) baud * (1.0 + rolloff)),
            eq_taps
        )) {
        (void) fprintf(stderr, "inverse-response FIR design failed\n");
        return 1;
    }
    gain = (double) levels[selected] / 16000.0;
    {
        double       maximum_magnitude = 1.0;
        const double half_band =
            0.5 * (double) baud * (1.0 + rolloff);
        for (tone = 0U; tone < TONE_COUNT; ++tone) {
            const double magnitude =
                rtnc_tx_eq_magnitude(eq_taps, (double) tones[tone]);
            if ((double) tones[tone] >= carrier - half_band &&
                (double) tones[tone] <= carrier + half_band &&
                magnitude > maximum_magnitude) {
                maximum_magnitude = magnitude;
            }
        }
        gain /= maximum_magnitude;
    }
    (void) printf("marker_sample=%zu linear_pcm_peak=%d\n", marker, levels[selected]);
    for (tone = 0U; tone < TONE_COUNT; ++tone) {
        (void) printf("tone=%uHz amplitude=%.1f distortion=%.3f\n", tones[tone], amplitudes[selected][tone], distortion[selected][tone]);
    }
    (void) printf("recommended configuration:\n"
                  "tx:\n"
                  "  filter_gain: %.3f\n"
                  "  response_eq_taps: [",
                  gain);
    for (tone = 0U; tone < RTNC_TX_EQ_TAP_COUNT; ++tone) {
        (void) printf("%s%.8g", tone == 0U ? "" : ", ", (double) eq_taps[tone]);
    }
    (void) printf("]\nhackrf_dropped_blocks=%llu\n", (unsigned long long) drops);
    return 0;
}

int main(int argc, char **argv) {
    const double         pi = 3.14159265358979323846;
    uint64_t             frequency;
    uint32_t             lna;
    uint32_t             vga;
    double               offset_hz;
    unsigned int         baud;
    double               carrier;
    double               rolloff;
    static iq_ring_t     ring;
    static iq_block_t    block;
    static int16_t       samples[RX_SECONDS * SAMPLE_RATE];
    liquid_float_complex iq[IQ_WORK_SAMPLES];
    liquid_float_complex channel[AUDIO_WORK_SAMPLES];
    float                audio[AUDIO_WORK_SAMPLES];
    hackrf_device       *device = NULL;
    msresamp_crcf        resampler = NULL;
    freqdem              demodulator = NULL;
    double               phase = 0.0;
    size_t               sample_count = 0U;
    double               start;
    int                  result = 1;
    if (argc != 9 || strcmp(argv[1], "hackrf-rx") != 0) {
        (void) fprintf(stderr, "usage: %s hackrf-rx FREQ_HZ LNA VGA OFFSET_HZ "
                               "SYMBOL_RATE CARRIER_HZ RRC_ROLLOFF\n",
                       argv[0]);
        return 2;
    }
    frequency = strtoull(argv[2], NULL, 10);
    lna = (uint32_t) strtoul(argv[3], NULL, 10);
    vga = (uint32_t) strtoul(argv[4], NULL, 10);
    offset_hz = strtod(argv[5], NULL);
    baud = (unsigned int) strtoul(argv[6], NULL, 10);
    carrier = strtod(argv[7], NULL);
    rolloff = strtod(argv[8], NULL);
    if (frequency == 0U || baud == 0U || !isfinite(offset_hz) ||
        !isfinite(carrier) || !isfinite(rolloff) || carrier <= 0.0 ||
        rolloff <= 0.0 || rolloff > 1.0) {
        (void) fprintf(stderr, "invalid calibration parameter\n");
        return 2;
    }
    atomic_init(&ring.producer, 0U);
    atomic_init(&ring.consumer, 0U);
    atomic_init(&ring.drops, 0U);
    resampler = msresamp_crcf_create((float) SAMPLE_RATE / (float) IQ_RATE, 80.0F);
    demodulator = freqdem_create(1.0F);
    if (resampler == NULL || demodulator == NULL ||
        hackrf_init() != HACKRF_SUCCESS ||
        hackrf_open(&device) != HACKRF_SUCCESS ||
        hackrf_set_sample_rate(device, (double) IQ_RATE) != HACKRF_SUCCESS ||
        hackrf_set_freq(device, frequency) != HACKRF_SUCCESS ||
        hackrf_set_amp_enable(device, 0U) != HACKRF_SUCCESS ||
        hackrf_set_lna_gain(device, lna) != HACKRF_SUCCESS ||
        hackrf_set_vga_gain(device, vga) != HACKRF_SUCCESS ||
        hackrf_start_rx(device, receive_callback, &ring) != HACKRF_SUCCESS) {
        (void) fprintf(stderr, "HackRF calibration initialization failed\n");
        goto done;
    }
    (void) printf("calibration HackRF RX listening for one complete cycle "
                  "freq=%llu lna=%u vga=%u offset=%.1f profile=%u/%.1f/%.2f\n",
                  (unsigned long long) frequency,
                  lna,
                  vga,
                  offset_hz,
                  baud,
                  carrier,
                  rolloff);
    (void) fflush(stdout);
    start = monotonic_seconds();
    while (sample_count < RX_SECONDS * SAMPLE_RATE &&
           monotonic_seconds() - start < (double) RX_SECONDS + 2.0) {
        size_t byte_offset;
        if (!ring_pop(&ring, &block)) {
            const struct timespec idle = { .tv_sec = 0, .tv_nsec = 1000000L };
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
                const liquid_float_complex mixer =
                    (float) cos(phase) + (float) sin(phase) * I;
                iq[index] = (i + q * I) * mixer;
                phase -= 2.0 * pi * offset_hz / (double) IQ_RATE;
                if (phase > pi)
                    phase -= 2.0 * pi;
                if (phase < -pi)
                    phase += 2.0 * pi;
            }
            byte_offset += input_count * 2U;
            (void) msresamp_crcf_execute(resampler, iq, (unsigned int) input_count, channel, &output_count);
            if (output_count > AUDIO_WORK_SAMPLES ||
                freqdem_demodulate_block(demodulator, channel, output_count, audio) != LIQUID_OK) {
                (void) fprintf(stderr, "HackRF calibration DSP failure\n");
                goto stop;
            }
            for (index = 0U; index < output_count &&
                             sample_count < RX_SECONDS * SAMPLE_RATE;
                 ++index) {
                const float scaled = audio[index] * 30000.0F;
                samples[sample_count++] =
                    scaled > 32767.0F ? 32767 : scaled < -32768.0F ? -32768
                                                                   : (int16_t) lrintf(scaled);
            }
        }
    }
stop:
    (void) hackrf_stop_rx(device);
    result = analyze(samples, sample_count, baud, carrier, rolloff, atomic_load_explicit(&ring.drops, memory_order_relaxed));
done:
    if (device != NULL)
        (void) hackrf_close(device);
    (void) hackrf_exit();
    if (demodulator != NULL)
        (void) freqdem_destroy(demodulator);
    if (resampler != NULL)
        (void) msresamp_crcf_destroy(resampler);
    return result;
}
