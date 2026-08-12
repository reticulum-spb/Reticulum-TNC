#include "rtnc/kiss.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

int main(void) {
    const uint8_t      input[] = { 0x01U, RTNC_KISS_FEND, 0x02U, RTNC_KISS_FESC, 0x03U };
    uint8_t            encoded[32U];
    size_t             encoded_length = 0U;
    rtnc_kiss_parser_t parser;
    const uint8_t     *packet = NULL;
    size_t             packet_length = 0U;
    size_t             index;

    assert(rtnc_kiss_encode(input, sizeof(input), encoded, sizeof(encoded), &encoded_length));
    assert(encoded[0] == RTNC_KISS_FEND);
    assert(encoded[1] == RTNC_KISS_DATA_COMMAND);
    assert(encoded[encoded_length - 1U] == RTNC_KISS_FEND);
    rtnc_kiss_parser_init(&parser);
    for (index = 0U; index < encoded_length; ++index) {
        const bool complete = rtnc_kiss_parser_push(
            &parser,
            encoded[index],
            &packet,
            &packet_length
        );
        assert(complete == (index + 1U == encoded_length));
    }
    assert(packet_length == sizeof(input));
    assert(memcmp(packet, input, sizeof(input)) == 0);

    /* Control frames retain port, command, and their one-byte value. */
    {
        const uint8_t     command[] = { RTNC_KISS_FEND, RTNC_KISS_CMD_TXDELAY, 7U, RTNC_KISS_FEND };
        rtnc_kiss_frame_t frame;
        rtnc_kiss_parser_init(&parser);
        for (index = 0U; index < sizeof(command); ++index) {
            const bool complete = rtnc_kiss_parser_push_frame(
                &parser,
                command[index],
                &frame
            );
            assert(complete == (index + 1U == sizeof(command)));
        }
        assert(frame.port == 0U);
        assert(frame.command == RTNC_KISS_CMD_TXDELAY);
        assert(frame.length == 1U);
        assert(frame.data[0] == 7U);
    }

    /* The data-only compatibility API does not expose control commands. */
    rtnc_kiss_parser_init(&parser);
    assert(!rtnc_kiss_parser_push(&parser, RTNC_KISS_FEND, &packet, &packet_length));
    assert(!rtnc_kiss_parser_push(&parser, 0x01U, &packet, &packet_length));
    assert(!rtnc_kiss_parser_push(&parser, 0x55U, &packet, &packet_length));
    assert(!rtnc_kiss_parser_push(&parser, RTNC_KISS_FEND, &packet, &packet_length));

    /* Invalid escapes reject only the current frame and recover at FEND. */
    assert(!rtnc_kiss_parser_push(&parser, RTNC_KISS_DATA_COMMAND, &packet, &packet_length));
    assert(!rtnc_kiss_parser_push(&parser, RTNC_KISS_FESC, &packet, &packet_length));
    assert(!rtnc_kiss_parser_push(&parser, 0x00U, &packet, &packet_length));
    assert(!rtnc_kiss_parser_push(&parser, RTNC_KISS_FEND, &packet, &packet_length));
    return 0;
}
