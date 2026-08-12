# Full modem Eb/N0 smoke result

This fixed-seed test sends a 64-byte payload through the complete real-audio
QPSK waveform. Noise is normalized from total waveform energy per delivered
payload bit. Goodput includes training, header, CRC, RRC delay, LDPC padding,
and trailing symbols. Each point uses 20 frames.

| Eb/N0 (dB) | NONE successes | TC128 successes | TC512 successes | NONE B/s | TC128 B/s | TC512 B/s |
|---:|---:|---:|---:|---:|---:|---:|
| 20 | 14/20 | 4/20 | 4/20 | 170.127 | 24.615 | 18.824 |
| 24 | 19/20 | 11/20 | 7/20 | 230.886 | 67.692 | 32.941 |
| 28 | 20/20 | 13/20 | 8/20 | 243.038 | 80.000 | 37.647 |
| 32 | 20/20 | 16/20 | 16/20 | 243.038 | 98.462 | 75.294 |

The uncoded frame occupies 12,640 samples; robust TC128 occupies 24,960. The
segmented TC128 baseline does not improve delivered payload goodput in this
test. Nine codewords multiply block-failure opportunities, while the longer
frame is more sensitive to residual phase slope from the single training-based
CFO estimate. This is evidence against selecting segmented TC128 as the normal
production code without carrier tracking and broader code comparisons.

TC512 uses three 64-byte codewords for the 67-byte protected class-64 area,
producing 32,640 samples. TC128 uses nine 16-byte codewords and 24,960 samples.
TC512's smaller codeword count did not compensate for its additional padding:
at 32 dB both decoded 16/20 frames, while TC512 delivered less goodput. It is
therefore retained as a comparison baseline, not selected as the Normal code.

The sample count currently includes the explicit RRC trailing flush but no PTT
lead/tail silence. The 20-frame points are regression smoke data, not confidence
intervals or an OTA performance claim.

## Rejected tracking experiment

A decision-directed loop driven directly by LiquidDSP's per-symbol modem phase
error was tested after the feed-forward training estimate. It failed the
noiseless robust regression because the real-audio image and current matched
filter produce a systematic phase-error component. The loop was not retained.
Carrier tracking should next be evaluated with explicit pilots/midambles or a
proper carrier synchronizer and must first preserve noiseless loopback.
