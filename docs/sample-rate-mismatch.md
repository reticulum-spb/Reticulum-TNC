# Sample-rate mismatch characterization

The deterministic channel resamples real audio with linear interpolation to
model a receiver sample clock error. After coarse acquisition and CFO removal,
the receiver uses LiquidDSP RRC symbol timing recovery instead of assuming a
fixed 40 samples/symbol stride.

| Profile | −1000 ppm | −500 ppm | −200 ppm | ±100 ppm | +200 ppm | +500 ppm | +1000 ppm |
|---|---:|---:|---:|---:|---:|---:|---:|
| NONE / 64 | pass | pass | pass | pass | pass | pass | pass |
| NONE / 128 | pass | pass | pass | pass | pass | pass | pass |
| TC128 / 64 | pass | pass | pass | pass | pass | pass | pass |
| TC128 / 128 | pass | pass | pass | pass | pass | pass | pass |

All four profiles pass every deterministic point from −1000 through +1000 ppm.
The initial TC128/128 positive-clock failure was traced to coarse CFO estimation
over only 32 symbols: resampling shifts both symbol timing and the 1650 Hz audio
carrier. Frame detection still uses 32 symbols, while CFO differential and
coherent phase estimation now use all 96 known acquisition/training symbols.
This is not yet an OTA guarantee; voice-band distortion, clipping, and recorded
handheld channels remain to be tested. The planned 2-samples/symbol
fractionally-spaced equalizer is separate work.
