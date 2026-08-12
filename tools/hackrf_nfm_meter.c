#define _POSIX_C_SOURCE 200809L

#include <libhackrf/hackrf.h>
#include <liquid/liquid.h>

#include <complex.h>
#include <math.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    IQ_RATE = 2000000U,
    DEMOD_RATE = 48000U,
    FFT_RATE = 8000U,
    FFT_SIZE = 1024U,
    IQ_BLOCK_BYTES = 262144U,
    IQ_RING_CAPACITY = 4U,
    IQ_RING_SLOTS = IQ_RING_CAPACITY + 1U,
    IQ_WORK_SAMPLES = 2048U,
    DEMOD_WORK_SAMPLES = 128U,
    FFT_WORK_SAMPLES = 32U,
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

static volatile sig_atomic_t stop_requested = 0;

static void stop_handler(int signal_number) {
    (void) signal_number;
    stop_requested = 1;
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
    struct timespec value = { 0 };
    (void) clock_gettime(CLOCK_MONOTONIC, &value);
    return (double) value.tv_sec + (double) value.tv_nsec / 1.0e9;
}

static double bin_power(const liquid_float_complex *spectrum, unsigned int center) {
    double power = 0.0;
    int    delta;
    for (delta = -2; delta <= 2; ++delta) {
        const int bin = (int) center + delta;
        if (bin > 0 && bin < (int) (FFT_SIZE / 2U)) {
            const double real = crealf(spectrum[bin]);
            const double imaginary = cimagf(spectrum[bin]);
            power += real * real + imaginary * imaginary;
        }
    }
    return power;
}

static void print_meter(liquid_float_complex *time_domain, liquid_float_complex *spectrum, fftplan plan, uint64_t drops, double iq_dbfs) {
    const double pi = 3.14159265358979323846;
    unsigned int index;
    unsigned int fundamental_bin = 0U;
    double       fundamental_power = 0.0;
    double       harmonic_powers[3] = { 0.0, 0.0, 0.0 };
    double       harmonic_power = 0.0;
    double       band_power = 0.0;
    double       signal_power;
    double       noise_power;
    double       thd;
    double       fundamental_peak;
    double       frequency_hz;
    double       deviation_hz;
    double       snr_db;

    for (index = 0U; index < FFT_SIZE; ++index) {
        const float window =
            0.5F - 0.5F *
                       cosf(2.0F * (float) pi * (float) index / (float) (FFT_SIZE - 1U));
        time_domain[index] *= window;
    }
    fft_execute(plan);
    for (index = (300U * FFT_SIZE) / FFT_RATE;
         index <= (3300U * FFT_SIZE) / FFT_RATE;
         ++index) {
        const double real = crealf(spectrum[index]);
        const double imaginary = cimagf(spectrum[index]);
        const double power = real * real + imaginary * imaginary;
        band_power += power;
        if (power > fundamental_power) {
            fundamental_power = power;
            fundamental_bin = index;
        }
    }
    fundamental_power = bin_power(spectrum, fundamental_bin);
    for (index = 2U; index <= 4U; ++index) {
        const unsigned int harmonic_bin = fundamental_bin * index;
        if (harmonic_bin < FFT_SIZE / 2U) {
            harmonic_powers[index - 2U] =
                bin_power(spectrum, harmonic_bin);
            harmonic_power += harmonic_powers[index - 2U];
        }
    }
    signal_power = fundamental_power + harmonic_power;
    noise_power = fmax(1.0e-20, band_power - signal_power);
    thd = fundamental_power > 1.0e-20
              ? sqrt(harmonic_power / fundamental_power)
              : 0.0;
    fundamental_peak =
        4.0 * sqrt(fundamental_power) / (double) FFT_SIZE;
    frequency_hz = (double) fundamental_bin * FFT_RATE / FFT_SIZE;
    deviation_hz = fundamental_peak * DEMOD_RATE / (2.0 * pi);
    snr_db = 10.0 * log10(fundamental_power / noise_power);
    if (deviation_hz < 5.0) {
        (void) printf("\rWEAK f=%7.1fHz dev=%6.1fHzpk SNR=%6.1fdB "
                      "IQ=%6.1fdBFS drops=%llu"
                      "                                      ",
                      frequency_hz,
                      deviation_hz,
                      snr_db,
                      iq_dbfs,
                      (unsigned long long) drops);
    } else {
        const double h2_dbc = harmonic_powers[0] > 0.0
                                  ? 10.0 * log10(harmonic_powers[0] / fundamental_power)
                                  : -200.0;
        const double h3_dbc = harmonic_powers[1] > 0.0
                                  ? 10.0 * log10(harmonic_powers[1] / fundamental_power)
                                  : -200.0;
        const double h4_dbc = harmonic_powers[2] > 0.0
                                  ? 10.0 * log10(harmonic_powers[2] / fundamental_power)
                                  : -200.0;
        (void) printf("\r%7s f=%7.1fHz dev=%6.1fHzpk H2=%6.1fdBc H3=%6.1fdBc "
                      "H4=%6.1fdBc THD=%5.1f%% SNR=%5.1fdB IQ=%5.1fdBFS "
                      "drop=%llu   ",
                      snr_db < 6.0 ? "LOW-SNR" : "TONE",
                      frequency_hz,
                      deviation_hz,
                      h2_dbc,
                      h3_dbc,
                      h4_dbc,
                      100.0 * thd,
                      snr_db,
                      iq_dbfs,
                      (unsigned long long) drops);
    }
    (void) fflush(stdout);
}

int main(int argc, char **argv) {
    const struct timespec idle = { .tv_sec = 0, .tv_nsec = 1000000L };
    const double          pi = 3.14159265358979323846;
    uint64_t              frequency_hz = 446006250U;
    uint32_t              lna_gain = 24U;
    uint32_t              vga_gain = 16U;
    double                offset_hz = -70.0;
    double                duration_seconds = 0.0;
    hackrf_device        *device = NULL;
    static rx_context_t   context;
    static iq_block_t     block;
    liquid_float_complex  iq[IQ_WORK_SAMPLES];
    liquid_float_complex  channel[DEMOD_WORK_SAMPLES];
    liquid_float_complex  filtered[DEMOD_WORK_SAMPLES];
    float                 demodulated[DEMOD_WORK_SAMPLES];
    float                 audio_8k[FFT_WORK_SAMPLES];
    liquid_float_complex  time_domain[FFT_SIZE];
    liquid_float_complex  spectrum[FFT_SIZE];
    msresamp_crcf         channel_resampler = NULL;
    msresamp_rrrf         audio_resampler = NULL;
    firfilt_crcf          channel_filter = NULL;
    freqdem               demodulator = NULL;
    fftplan               plan = NULL;
    float                 dc_previous_input = 0.0F;
    float                 dc_previous_output = 0.0F;
    const float           dc_coefficient =
        expf(-2.0F * (float) pi * 30.0F / (float) DEMOD_RATE);
    size_t       fft_count = 0U;
    unsigned int fft_frames = 0U;
    double       mixer_phase = 0.0;
    double       iq_energy = 0.0;
    uint64_t     iq_energy_count = 0U;
    double       start;
    int          result = 1;

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
        offset_hz = strtod(argv[4], NULL);
    }
    if (argc > 5) {
        duration_seconds = strtod(argv[5], NULL);
    }
    if (argc > 6 || frequency_hz < 1000000U || frequency_hz > 6000000000ULL ||
        lna_gain > 40U || lna_gain % 8U != 0U || vga_gain > 62U ||
        vga_gain % 2U != 0U || !isfinite(offset_hz) ||
        fabs(offset_hz) > 100000.0 || !isfinite(duration_seconds) ||
        duration_seconds < 0.0 || duration_seconds > 86400.0) {
        (void) fprintf(stderr, "usage: %s [FREQ_HZ [LNA_DB [VGA_DB [OFFSET_HZ "
                               "[SECONDS_0_FOR_FOREVER]]]]]\n",
                       argv[0]);
        return 2;
    }
    atomic_init(&context.ring.producer, 0U);
    atomic_init(&context.ring.consumer, 0U);
    atomic_init(&context.ring.drops, 0U);
    channel_resampler =
        msresamp_crcf_create((float) DEMOD_RATE / (float) IQ_RATE, 80.0F);
    audio_resampler =
        msresamp_rrrf_create((float) FFT_RATE / (float) DEMOD_RATE, 80.0F);
    channel_filter = firfilt_crcf_create_kaiser(
        129U,
        6500.0F / (float) DEMOD_RATE,
        60.0F,
        0.0F
    );
    demodulator = freqdem_create(1.0F);
    plan = fft_create_plan(FFT_SIZE, time_domain, spectrum, LIQUID_FFT_FORWARD, 0U);
    if (channel_resampler == NULL || audio_resampler == NULL ||
        channel_filter == NULL || demodulator == NULL || plan == NULL ||
        hackrf_init() != HACKRF_SUCCESS ||
        hackrf_open(&device) != HACKRF_SUCCESS ||
        hackrf_set_sample_rate(device, IQ_RATE) != HACKRF_SUCCESS ||
        hackrf_set_freq(device, frequency_hz) != HACKRF_SUCCESS ||
        hackrf_set_amp_enable(device, 0U) != HACKRF_SUCCESS ||
        hackrf_set_lna_gain(device, lna_gain) != HACKRF_SUCCESS ||
        hackrf_set_vga_gain(device, vga_gain) != HACKRF_SUCCESS ||
        hackrf_start_rx(device, receive_callback, &context) != HACKRF_SUCCESS) {
        (void) fprintf(stderr, "HackRF meter initialization failed\n");
        goto done;
    }
    (void) signal(SIGINT, stop_handler);
    (void) signal(SIGTERM, stop_handler);
    (void) printf("NFM meter freq=%llu LNA=%u VGA=%u offset=%.1f "
                  "FFT=%u@%uHz (Ctrl-C to stop)\n",
                  (unsigned long long) frequency_hz,
                  lna_gain,
                  vga_gain,
                  offset_hz,
                  FFT_SIZE,
                  FFT_RATE);
    start = monotonic_seconds();
    while (!stop_requested &&
           (duration_seconds == 0.0 ||
            monotonic_seconds() - start < duration_seconds)) {
        size_t byte_offset;
        if (!ring_pop(&context.ring, &block)) {
            (void) nanosleep(&idle, NULL);
            continue;
        }
        for (byte_offset = 0U; byte_offset + 2U <= block.count;) {
            const size_t available = (block.count - byte_offset) / 2U;
            const size_t work_count =
                available < IQ_WORK_SAMPLES ? available : IQ_WORK_SAMPLES;
            unsigned int channel_count = 0U;
            unsigned int audio_count = 0U;
            size_t       index;
            for (index = 0U; index < work_count; ++index) {
                const float i =
                    (float) (int8_t) block.data[byte_offset + 2U * index] /
                    128.0F;
                const float q =
                    (float) (int8_t) block.data[byte_offset + 2U * index + 1U] /
                    128.0F;
                const liquid_float_complex mixer =
                    (float) cos(mixer_phase) + (float) sin(mixer_phase) * I;
                iq[index] = (i + q * I) * mixer;
                iq_energy += (double) i * i + (double) q * q;
                iq_energy_count += 1U;
                mixer_phase -= 2.0 * pi * offset_hz / (double) IQ_RATE;
                if (mixer_phase > pi) {
                    mixer_phase -= 2.0 * pi;
                } else if (mixer_phase < -pi) {
                    mixer_phase += 2.0 * pi;
                }
            }
            byte_offset += 2U * work_count;
            if (msresamp_crcf_execute(channel_resampler, iq, (unsigned int) work_count, channel, &channel_count) != LIQUID_OK ||
                channel_count > DEMOD_WORK_SAMPLES) {
                (void) fprintf(stderr, "\nNFM meter channel failure\n");
                goto done;
            }
            for (index = 0U; index < channel_count; ++index) {
                (void) firfilt_crcf_push(channel_filter, channel[index]);
                (void) firfilt_crcf_execute(channel_filter, &filtered[index]);
            }
            if (freqdem_demodulate_block(demodulator, filtered, channel_count, demodulated) != LIQUID_OK ||
                channel_count > DEMOD_WORK_SAMPLES) {
                (void) fprintf(stderr, "\nNFM meter discriminator failure\n");
                goto done;
            }
            for (index = 0U; index < channel_count; ++index) {
                const float input = demodulated[index];
                demodulated[index] =
                    input - dc_previous_input +
                    dc_coefficient * dc_previous_output;
                dc_previous_input = input;
                dc_previous_output = demodulated[index];
            }
            if (msresamp_rrrf_execute(audio_resampler, demodulated, channel_count, audio_8k, &audio_count) != LIQUID_OK ||
                audio_count > FFT_WORK_SAMPLES) {
                (void) fprintf(stderr, "\nNFM meter DSP failure\n");
                goto done;
            }
            for (index = 0U; index < audio_count; ++index) {
                time_domain[fft_count++] = audio_8k[index] + 0.0F * I;
                if (fft_count == FFT_SIZE) {
                    fft_frames += 1U;
                    if (fft_frames % 4U == 0U) {
                        const double iq_dbfs =
                            iq_energy_count > 0U
                                ? 10.0 *
                                      log10(iq_energy / (double) iq_energy_count)
                                : -200.0;
                        print_meter(
                            time_domain,
                            spectrum,
                            plan,
                            atomic_load_explicit(&context.ring.drops, memory_order_relaxed),
                            iq_dbfs
                        );
                        iq_energy = 0.0;
                        iq_energy_count = 0U;
                    }
                    fft_count = 0U;
                }
            }
        }
    }
    (void) printf("\n");
    result = 0;

done:
    if (device != NULL && hackrf_is_streaming(device) == HACKRF_TRUE) {
        (void) hackrf_stop_rx(device);
    }
    if (device != NULL) {
        (void) hackrf_close(device);
    }
    (void) hackrf_exit();
    if (plan != NULL) {
        fft_destroy_plan(plan);
    }
    if (demodulator != NULL) {
        (void) freqdem_destroy(demodulator);
    }
    if (channel_filter != NULL) {
        (void) firfilt_crcf_destroy(channel_filter);
    }
    if (audio_resampler != NULL) {
        (void) msresamp_rrrf_destroy(audio_resampler);
    }
    if (channel_resampler != NULL) {
        (void) msresamp_crcf_destroy(channel_resampler);
    }
    return result;
}
