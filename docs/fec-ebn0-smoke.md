# TC128 calibrated Eb/N0 smoke result

This deterministic test compares uncoded 64-bit blocks with CCSDS TC128 using
BPSK over real AWGN at equal information-bit energy. For code rate `R`, noise
standard deviation is `sqrt(1 / (2 R Eb/N0))`. Each point uses 400 blocks and a
fixed seed.

| Eb/N0 (dB) | Uncoded FER | TC128 FER | Uncoded delivered bits/channel bit | TC128 delivered bits/channel bit |
|---:|---:|---:|---:|---:|
| 0 | 0.9950 | 0.9625 | 0.00500 | 0.01875 |
| 2 | 0.9275 | 0.3325 | 0.07250 | 0.33375 |
| 4 | 0.5500 | 0.0025 | 0.45000 | 0.49875 |
| 6 | 0.1375 | 0.0000 | 0.86250 | 0.50000 |

This is a regression/engineering smoke measurement, not a performance claim.
It excludes QPSK acquisition, preamble, header, CRC, final-block padding, audio
filtering, and frame-length signalling. The complete modem goodput sweep is the
decision metric for selecting a production code.
