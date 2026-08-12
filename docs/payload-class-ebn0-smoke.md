# Payload class Eb/N0 smoke comparison

This fixed-seed full-audio test compares 64-byte and 128-byte full payloads on
the same calibrated Eb/N0 points. Noise is normalized from complete waveform
energy per payload bit. Each point contains 10 frames, so the values locate the
tradeoff but are not precise FER estimates.

| FEC | Eb/N0 | 64 success | 128 success | 64 B/s | 128 B/s |
|---|---:|---:|---:|---:|---:|
| NONE | 24 dB | 8/10 | 1/10 | 194.430 | 26.853 |
| NONE | 28 dB | 10/10 | 10/10 | 243.038 | 268.531 |
| NONE | 32 dB | 10/10 | 10/10 | 243.038 | 268.531 |
| TC128 | 24 dB | 7/10 | 3/10 | 86.154 | 40.563 |
| TC128 | 28 dB | 5/10 | 4/10 | 61.538 | 54.085 |
| TC128 | 32 dB | 10/10 | 6/10 | 123.077 | 81.127 |

The result confirms that a larger physical frame is not automatically more
efficient. Near acquisition/CRC threshold, class 64 wins because fewer symbols
and fewer LDPC blocks must all survive. Once uncoded FER is negligible, class
128 wins by amortizing fixed overhead. With segmented TC128, class 128 still
loses at these points because it requires 17 independently decoded codewords.

The 10-frame TC128 points have visible Monte Carlo variance (the 64-byte result
at 28 dB is lower than at 24 dB). Future selection runs should use more frames,
confidence intervals, sample-rate mismatch, and recorded handheld channels.
