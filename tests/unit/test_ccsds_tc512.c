#include "../../src/fec/codes/ccsds_tc512.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

int main(void) {
    const uint8_t expected_parity[32] = {
        0xbcU,
        0x92U,
        0x1cU,
        0x98U,
        0xccU,
        0xe2U,
        0x6cU,
        0xe8U,
        0x12U,
        0x3aU,
        0x97U,
        0xffU,
        0x73U,
        0x5bU,
        0xf6U,
        0x9eU,
        0x08U,
        0xcbU,
        0x48U,
        0xc4U,
        0xc3U,
        0x00U,
        0x83U,
        0x0fU,
        0x30U,
        0xe0U,
        0x98U,
        0x59U,
        0xd6U,
        0x06U,
        0x7eU,
        0xbfU,
    };
    uint8_t information[32];
    uint8_t codeword[64];
    size_t  index;
    for (index = 0U; index < sizeof(information); ++index) {
        information[index] = (uint8_t) index;
    }
    assert(ccsds_tc512.variable_count == 512U);
    assert(ccsds_tc512.information_count == 256U);
    assert(ccsds_tc512.edge_count == 2048U);
    assert(rtnc_ldpc_encode_systematic(&ccsds_tc512, information, sizeof(information), codeword, sizeof(codeword)));
    assert(memcmp(codeword, information, sizeof(information)) == 0);
    assert(memcmp(&codeword[32], expected_parity, sizeof(expected_parity)) == 0);
    return 0;
}
