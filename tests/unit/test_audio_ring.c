#include "rtnc/audio_ring.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

static void fill(int16_t *samples, int16_t value) {
    size_t index;
    for (index = 0U; index < RTNC_AUDIO_BLOCK_SAMPLES; ++index) {
        samples[index] = (int16_t) (value + (int16_t) index);
    }
}

int main(void) {
    rtnc_audio_ring_t  ring;
    rtnc_audio_block_t block;
    int16_t            samples[RTNC_AUDIO_BLOCK_SAMPLES];
    size_t             round;
    size_t             index;

    assert(!rtnc_audio_ring_init(&ring, 0U));
    assert(!rtnc_audio_ring_init(&ring, RTNC_AUDIO_RING_MAX_CAPACITY + 1U));
    assert(rtnc_audio_ring_init(&ring, 8U));
    assert(rtnc_audio_ring_depth(&ring) == 0U);
    assert(rtnc_audio_ring_dropped(&ring) == 0U);
    assert(!rtnc_audio_ring_pop(&ring, &block));
    assert(!rtnc_audio_ring_push(&ring, samples, 0U));

    for (index = 0U; index < 8U; ++index) {
        fill(samples, (int16_t) (1000U * index));
        assert(rtnc_audio_ring_push(&ring, samples, RTNC_AUDIO_BLOCK_SAMPLES));
    }
    assert(rtnc_audio_ring_depth(&ring) == 8U);
    assert(rtnc_audio_ring_maximum_depth(&ring) == 8U);
    assert(!rtnc_audio_ring_push(&ring, samples, RTNC_AUDIO_BLOCK_SAMPLES));
    assert(rtnc_audio_ring_dropped(&ring) == 1U);

    for (index = 0U; index < 8U; ++index) {
        assert(rtnc_audio_ring_pop(&ring, &block));
        assert(block.count == RTNC_AUDIO_BLOCK_SAMPLES);
        assert(block.samples[0] == (int16_t) (1000U * index));
        assert(block.samples[RTNC_AUDIO_BLOCK_SAMPLES - 1U] == (int16_t) (1000U * index + RTNC_AUDIO_BLOCK_SAMPLES - 1U));
    }

    for (round = 0U; round < 32U; ++round) {
        fill(samples, (int16_t) round);
        assert(rtnc_audio_ring_push(&ring, samples, 17U));
        assert(rtnc_audio_ring_pop(&ring, &block));
        assert(block.count == 17U);
        assert(block.samples[0] == (int16_t) round);
    }
    assert(rtnc_audio_ring_depth(&ring) == 0U);
    assert(rtnc_audio_ring_dropped(&ring) == 1U);
    return 0;
}
