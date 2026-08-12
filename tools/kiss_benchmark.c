#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

enum {
    KISS_FEND = 0xc0,
    KISS_FESC = 0xdb,
    KISS_TFEND = 0xdc,
    KISS_TFESC = 0xdd,
    AX25_HEADER_BYTES = 16U,
    MAX_PAYLOAD_BYTES = 1023U,
};

static void encode_address(uint8_t output[7], const char *call, bool last) {
    const size_t call_length = strlen(call);
    size_t       index;
    for (index = 0U; index < 6U; ++index) {
        const uint8_t character = index < call_length ? (uint8_t) call[index]
                                                      : (uint8_t) ' ';
        output[index] = (uint8_t) (character << 1U);
    }
    output[6] = (uint8_t) (0x60U | (last ? 1U : 0U));
}

static bool write_all(int descriptor, const uint8_t *data, size_t count) {
    size_t written = 0U;
    while (written < count) {
        const ssize_t result = write(descriptor, data + written, count - written);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            return false;
        }
        written += (size_t) result;
    }
    return true;
}

static bool send_kiss_frame(int descriptor, uint8_t command, const uint8_t *frame, size_t frame_length) {
    uint8_t encoded[2U * (AX25_HEADER_BYTES + MAX_PAYLOAD_BYTES) + 3U];
    size_t  write_index = 0U;
    size_t  index;
    encoded[write_index++] = KISS_FEND;
    encoded[write_index++] = command;
    for (index = 0U; index < frame_length; ++index) {
        if (frame[index] == KISS_FEND) {
            encoded[write_index++] = KISS_FESC;
            encoded[write_index++] = KISS_TFEND;
        } else if (frame[index] == KISS_FESC) {
            encoded[write_index++] = KISS_FESC;
            encoded[write_index++] = KISS_TFESC;
        } else {
            encoded[write_index++] = frame[index];
        }
    }
    encoded[write_index++] = KISS_FEND;
    return write_all(descriptor, encoded, write_index);
}

int main(int argc, char **argv) {
    const char        *host = "192.168.1.54";
    unsigned int       port = 8001U;
    size_t             payload_bytes = 64U;
    unsigned int       frame_count = 10U;
    unsigned int       gap_ms = 0U;
    struct sockaddr_in address = { 0 };
    int                descriptor = -1;
    uint8_t            frame[AX25_HEADER_BYTES + MAX_PAYLOAD_BYTES];
    unsigned int       sequence;
    int                result = 1;

    if (argc > 1) {
        host = argv[1];
    }
    if (argc > 2) {
        port = (unsigned int) strtoul(argv[2], NULL, 10);
    }
    if (argc > 3) {
        payload_bytes = (size_t) strtoul(argv[3], NULL, 10);
    }
    if (argc > 4) {
        frame_count = (unsigned int) strtoul(argv[4], NULL, 10);
    }
    if (argc > 5) {
        gap_ms = (unsigned int) strtoul(argv[5], NULL, 10);
    }
    if (argc > 6 || port == 0U || port > 65535U || payload_bytes < 4U ||
        payload_bytes > MAX_PAYLOAD_BYTES || frame_count == 0U ||
        frame_count > 1000U || gap_ms > 10000U) {
        (void) fprintf(stderr, "usage: %s [HOST [PORT [PAYLOAD [COUNT [GAP_MS]]]]]\n", argv[0]);
        return 2;
    }
    encode_address(frame, "BENCH", false);
    encode_address(frame + 7U, "RTNC", true);
    frame[14] = 0x03U;
    frame[15] = 0xf0U;
    descriptor = socket(AF_INET, SOCK_STREAM, 0);
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t) port);
    if (descriptor < 0 || inet_pton(AF_INET, host, &address.sin_addr) != 1 ||
        connect(descriptor, (const struct sockaddr *) &address, sizeof(address)) != 0) {
        (void) perror("KISS connect");
        goto done;
    }
    {
        const uint8_t persist = 255U;
        const uint8_t slot_time = 1U;
        const uint8_t full_duplex = 1U;
        if (!send_kiss_frame(descriptor, 2U, &persist, 1U) ||
            !send_kiss_frame(descriptor, 3U, &slot_time, 1U) ||
            !send_kiss_frame(descriptor, 5U, &full_duplex, 1U)) {
            (void) fprintf(stderr, "KISS benchmark configuration failed\n");
            goto done;
        }
    }
    for (sequence = 0U; sequence < frame_count; ++sequence) {
        size_t          index;
        struct timespec delay;
        for (index = 0U; index < payload_bytes; ++index) {
            frame[AX25_HEADER_BYTES + index] =
                (uint8_t) (index * 37U + sequence * 13U + 0x29U);
        }
        frame[AX25_HEADER_BYTES] = (uint8_t) (sequence >> 24U);
        frame[AX25_HEADER_BYTES + 1U] = (uint8_t) (sequence >> 16U);
        frame[AX25_HEADER_BYTES + 2U] = (uint8_t) (sequence >> 8U);
        frame[AX25_HEADER_BYTES + 3U] = (uint8_t) sequence;
        if (!send_kiss_frame(descriptor, 0U, frame, AX25_HEADER_BYTES + payload_bytes)) {
            (void) fprintf(stderr, "KISS send failed at sequence %u\n", sequence);
            goto done;
        }
        delay.tv_sec = (time_t) (gap_ms / 1000U);
        delay.tv_nsec = (long) (gap_ms % 1000U) * 1000000L;
        while (nanosleep(&delay, &delay) < 0 && errno == EINTR) {
        }
    }
    {
        struct timespec drain = { .tv_sec = 5, .tv_nsec = 0L };
        while (nanosleep(&drain, &drain) < 0 && errno == EINTR) {
        }
    }
    (void) printf("sent=%u payload=%zu host=%s port=%u gap_ms=%u\n", frame_count, payload_bytes, host, port, gap_ms);
    result = 0;

done:
    if (descriptor >= 0) {
        (void) close(descriptor);
    }
    return result;
}
