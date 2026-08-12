#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    const double        pi = 3.14159265358979323846;
    FILE               *stream;
    uint8_t             bytes[8192];
    const unsigned long sample_rate = 2000000UL;
    const unsigned long chunk_samples = sample_rate / 10UL;
    unsigned long       in_chunk = 0UL;
    unsigned long       chunk = 0UL;
    unsigned long       clipped = 0UL;
    double              energy = 0.0;
    double              phase_re = 0.0;
    double              phase_im = 0.0;
    double              previous_i = 0.0;
    double              previous_q = 0.0;
    int                 have_previous = 0;
    size_t              count;
    size_t              index;

    if (argc != 2) {
        (void) fprintf(stderr, "usage: %s IQ_S8_FILE\n", argv[0]);
        return 2;
    }
    stream = fopen(argv[1], "rb");
    if (stream == NULL) {
        (void) perror(argv[1]);
        return 1;
    }
    while ((count = fread(bytes, 1U, sizeof(bytes), stream)) > 0U) {
        for (index = 0U; index + 1U < count; index += 2U) {
            const double i = (double) (int8_t) bytes[index];
            const double q = (double) (int8_t) bytes[index + 1U];
            energy += i * i + q * q;
            if (abs((int) i) >= 127 || abs((int) q) >= 127) {
                clipped += 1UL;
            }
            if (have_previous != 0) {
                phase_re += previous_i * i + previous_q * q;
                phase_im += previous_i * q - previous_q * i;
            }
            previous_i = i;
            previous_q = q;
            have_previous = 1;
            in_chunk += 1UL;
            if (in_chunk == chunk_samples) {
                const double rms = sqrt(energy / (double) in_chunk) / 128.0;
                const double dbfs = 20.0 * log10(rms);
                const double offset = atan2(phase_im, phase_re) *
                                      (double) sample_rate / (2.0 * pi);
                (void) printf("%.1f %.2f %.0f %lu\n", (double) chunk / 10.0, dbfs, offset, clipped);
                chunk += 1UL;
                in_chunk = 0UL;
                clipped = 0UL;
                energy = 0.0;
                phase_re = 0.0;
                phase_im = 0.0;
            }
        }
    }
    if (ferror(stream) != 0) {
        (void) fclose(stream);
        return 1;
    }
    (void) fclose(stream);
    return 0;
}
