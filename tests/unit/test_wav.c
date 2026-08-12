#include "rtnc/wav.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    const int16_t input[] = { INT16_MIN, -1234, 0, 1234, INT16_MAX };
    int16_t       output[5] = { 0 };
    uint32_t      sample_rate_hz = 0U;
    size_t        sample_count = 0U;
    size_t        index;
    FILE         *stream = tmpfile();
    assert(stream != NULL);
    assert(rtnc_wav_write_mono_s16(stream, 48000U, input, 5U) == RTNC_WAV_OK);
    assert(fseek(stream, 0L, SEEK_SET) == 0);
    assert(rtnc_wav_read_mono_s16(stream, &sample_rate_hz, output, 5U, &sample_count) == RTNC_WAV_OK);
    assert(sample_rate_hz == 48000U);
    assert(sample_count == 5U);
    for (index = 0U; index < sample_count; ++index) {
        assert(output[index] == input[index]);
    }
    assert(fclose(stream) == 0);
    return 0;
}
