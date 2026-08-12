# Combined timing and noise matrix

This deterministic regression combines real-audio sample-clock resampling with
additive Gaussian noise for the longest current robust profile: TC128 with a
128-byte payload. Each cell contains successful frames out of eight trials.

| Clock error | sigma 0 | sigma 0.005 | sigma 0.010 | sigma 0.020 |
|---:|---:|---:|---:|---:|
| -500 ppm | 8 | 8 | 8 | 8 |
| 0 ppm | 8 | 8 | 8 | 8 |
| +200 ppm | 8 | 8 | 8 | 8 |
| +300 ppm | 8 | 8 | 8 | 8 |
| +400 ppm | 8 | 8 | 8 | 8 |
| +500 ppm | 8 | 8 | 8 | 8 |

The former sharp asymmetric boundary was a residual carrier-phase problem, not
an LDPC or timing-detection failure. At +500 ppm, resampling shifts the 1650 Hz
carrier by roughly 0.825 Hz; estimating CFO from only the 32-symbol acquisition
block left enough residual phase drift to corrupt the long payload. Extending
only the CFO/phase estimator to all 96 known symbols removes the boundary while
keeping detection on the shorter sequence. Every matrix cell is now required
to pass. Recorded-channel and fractionally-spaced equalizer work remain.
