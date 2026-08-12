# Phase 2 FEC candidates

This note records the initial source/license and block-size audit. No external
FEC source has been copied into this repository.

## Labrador-LDPC

The upstream [adamgreig/labrador-ldpc](https://github.com/adamgreig/labrador-ldpc)
repository is MIT licensed and implements published CCSDS codes. It is Rust
(`no_std`) with a C API, so linking it would still add a non-C runtime/build
dependency and is not acceptable as the production implementation here. It is
useful as a reference and oracle for generated vectors/tables.

Relevant code families reported by upstream:

| Code family | Information bits | Codeword bits | Rate | Fit here |
|---|---:|---:|---:|---|
| TC128 | 64 | 128 | 1/2 | only 8 information bytes |
| TC256 | 128 | 256 | 1/2 | only 16 information bytes |
| TC512 | 256 | 512 | 1/2 | only 32 information bytes |
| TM1280 | 1024 | 1280 | 4/5 | exactly 128 information bytes |
| TM1536 | 1024 | 1536 | 2/3 | exactly 128 information bytes |
| TM2048 | 1024 | 2048 | 1/2 | exactly 128 information bytes |

The dimensions are documented in the upstream
[code enumeration](https://docs.rs/labrador-ldpc/latest/labrador_ldpc/codes/enum.LDPCCode.html).
The TC family is too small for a complete 64/128-byte radio payload unless
several codewords are used. The TM k=1024 family fits 128 information bytes,
but our header and CRC also need protection, so padding, shortening, a separate
robust header code, or a smaller payload must be measured rather than assumed.

## LiquidDSP 1.6 baseline

The installed LiquidDSP 1.6 headers expose repetition, Hamming, Golay, SECDED,
convolutional, punctured convolutional, and Reed-Solomon schemes. They do not
expose LDPC. LiquidDSP remains useful for a convolutional baseline, but its
byte-oriented FEC API does not by itself satisfy our required soft-input LDPC
decoder contract.

## Current decision

- Keep the project's small `fec.h` API and signed LLR convention.
- Keep `FEC_NONE` as the integration baseline.
- Use Labrador-LDPC only as an MIT-licensed reference/oracle; do not link Rust.
- Evaluate CCSDS-derived static tables generated offline and a small pure-C
  normalized-min-sum decoder with deterministic workspace.
- Do not select a production code until 64/128-byte framing overhead and
  calibrated FER/goodput measurements are available.

## YAML configuration

FEC is part of a named PHY profile. `modem.profile` only selects that complete
record; it must not contain a separate FEC override. Transmitter and receiver
must select matching profiles because the radio protocol does not negotiate
modulation, baud rate, payload class, or FEC.

| `fec_mode` | C enum | Current implementation |
|---:|---|---|
| `0` | `FEC_NONE` | no redundancy; diagnostics only |
| `1` | `FEC_LDPC_ROBUST` | segmented CCSDS TC128, rate 1/2 |
| `2` | `FEC_LDPC_NORMAL` | punctured TC128, rate 2/3 |

`FEC_LDPC_FAST` is reserved but unsupported and is rejected by the YAML
configuration validator. Ready-to-use QPSK-1600 profile names are
`qpsk_1600_1_64`, `qpsk_1600_1_128`, `qpsk_1600_2_64`, and
`qpsk_1600_2_128`. The suffix selects the independently protected physical
payload class, not a Reticulum MTU.

## Implemented validation code

The CCSDS TC128 `(n=128, k=64)` code is now generated and tested internally.
Its pure-C encoder matches Labrador-LDPC's oracle for information bytes
`00 01 02 03 04 05 06 07`, and the normalized-min-sum core decodes its expanded
512-edge parity matrix.

TC128 is exposed experimentally as `FEC_LDPC_ROBUST` at the standalone FEC API
boundary. Arbitrary non-empty input is split into 8-byte information blocks and
the final block is zero-padded. The decoder returns the padded byte count; the
authenticated frame length determines how many bytes are meaningful. Fixed
payload classes now provide coded-length acquisition in the modem.

`FEC_LDPC_NORMAL` is a punctured TC128 code with effective rate 2/3. Each
8-byte information block transmits all 64 systematic bits and parity positions
96--127; omitted parity positions 64--95 enter the decoder as zero-LLR
erasures. A deterministic AWGN comparison selected this contiguous mask over
alternating, first-half, and fixed random alternatives. At 4.5 dB the selected
mask failed 62 of 2000 blocks versus 71--84 for the alternatives tested.

The resulting coded sizes are 108 bytes for a 67-byte protected class-64 frame
and 204 bytes for a 131-byte protected class-128 frame. `FEC_LDPC_ROBUST`
requires 144 and 272 bytes respectively. The calibrated standalone sweep at
4 dB measured FER 0.080 for NORMAL versus 0.005 for ROBUST, while effective
block goodput was 0.613 versus 0.498 information bits per transmitted bit.
At 6 dB both decoded all 400 deterministic test blocks.

The generated TC512 implementation remains as a reference/oracle test but is
no longer assigned to a runtime FEC mode: its padding made it slower than
segmented TC128 for the current protected lengths.
