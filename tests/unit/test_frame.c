#include "rtnc/frame.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

int main(void) {
    static const uint8_t check[] = "123456789";
    const uint8_t        input[] = { 0x00U, 0x55U, 0xaaU, 0xffU, 0x42U };
    uint8_t              encoded[RTNC_FRAME_MAX_ENCODED_SIZE];
    uint8_t              decoded[RTNC_FRAME_MAX_PAYLOAD];
    size_t               encoded_length = 0U;
    size_t               decoded_length = 0U;

    assert(rtnc_crc16_ccitt_false(check, sizeof(check) - 1U) == 0x29b1U);
    assert(rtnc_frame_build(input, sizeof(input), encoded, sizeof(encoded), &encoded_length) == RTNC_FRAME_OK);
    assert(encoded_length == RTNC_FRAME_HEADER_SIZE + sizeof(input) + RTNC_FRAME_CRC_SIZE);
    assert(rtnc_frame_parse(encoded, encoded_length, decoded, sizeof(decoded), &decoded_length) == RTNC_FRAME_OK);
    assert(decoded_length == sizeof(input));
    assert(memcmp(input, decoded, sizeof(input)) == 0);

    encoded[RTNC_FRAME_HEADER_SIZE + 1U] ^= 0x01U;
    assert(rtnc_frame_parse(encoded, encoded_length, decoded, sizeof(decoded), &decoded_length) == RTNC_FRAME_CRC_FAILURE);
    encoded[RTNC_FRAME_HEADER_SIZE + 1U] ^= 0x01U;

    encoded[0] = 0xffU;
    assert(rtnc_frame_parse(encoded, encoded_length, decoded, sizeof(decoded), &decoded_length) == RTNC_FRAME_INVALID_LENGTH);

    assert(rtnc_frame_build(NULL, 0U, encoded, sizeof(encoded), &encoded_length) == RTNC_FRAME_INVALID_LENGTH);
    assert(rtnc_frame_build(input, RTNC_FRAME_MAX_PAYLOAD + 1U, encoded, sizeof(encoded), &encoded_length) == RTNC_FRAME_INVALID_LENGTH);

    {
        uint8_t maximum[RTNC_FRAME_MAX_PAYLOAD];
        size_t  index;
        for (index = 0U; index < sizeof(maximum); ++index) {
            maximum[index] = (uint8_t) index;
        }
        assert(rtnc_frame_build(maximum, sizeof(maximum), encoded, sizeof(encoded), &encoded_length) == RTNC_FRAME_OK);
        assert(encoded[0] == 127U);
        assert(rtnc_frame_parse(encoded, encoded_length, decoded, sizeof(decoded), &decoded_length) == RTNC_FRAME_OK);
        assert(decoded_length == sizeof(maximum));
        assert(memcmp(maximum, decoded, sizeof(maximum)) == 0);
    }
    return 0;
}
