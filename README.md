# Reticulum TNC

Reticulum TNC is an experimental narrowband packet modem for the microphone and
speaker audio path of inexpensive FM handheld radios. It is written in C and
uses LiquidDSP for the physical layer.

The modem exposes a local KISS TCP interface for Reticulum-compatible packet
I/O. It uses its own radio framing rather than AX.25 or HDLC.

## Implemented features

- BPSK, QPSK, and 8PSK, plus experimental 16PSK, at 48 kHz audio sample rate
- Named, fixed PHY profiles loaded from YAML
- Root-raised-cosine pulse shaping and matched filtering
- Preamble correlation, symbol timing, carrier/phase recovery, and frame
  diagnostics
- Soft demapping with LLRs preserved through FEC decoding
- CCSDS TC128 LDPC:
  - robust rate 1/2 mode
  - punctured normal rate 2/3 mode
- Training-based fractionally spaced equalizer fallback
- Native length header and CRC-protected radio frames
- Independently synchronized 64-byte and 128-byte radio payload classes
- Fragmentation and ordered reassembly of packets up to the configured MTU
- Fixed-storage audio, decode, completion, TX, and RX queues
- ALSA full-duplex audio backend
- GPIO PTT control through libgpiod
- Adaptive noise tracking and correlation-confirmed burst detection
- Optional linear-phase 17-tap TX response equalization
- Local KISS TCP server with KISS escaping and port-zero data frames
- KISS `TXDELAY`, `P`, `SLOTTIME`, and `TXTAIL` commands
- Runtime and syslog diagnostics for detection, CFO, timing, EVM, effective
  SNR, FEC iterations, queue depth, drops, and ALSA XRUNs
- Deterministic profile bitrate and frame-airtime calculation

Modulation, baud rate, FEC, and payload class are fixed by the selected profile.
There is no over-the-air profile negotiation or adaptive modulation.

## Current radio profiles

Profiles are defined as complete records in [`rtnc.yaml`](rtnc.yaml). The file
contains QPSK and 8PSK profiles from 600 to 2400 baud, robust and normal FEC
variants, and 64-byte or 128-byte radio payload classes.

List every configured profile and its calculated Reticulum interface bitrate:

```sh
rtnc_modem rtnc.yaml
```

Example:

```text
name                       interface bitrate bps
bpsk_750_robust64          299
bpsk_1200_robust64         478
qpsk_1200_robust64         879
qpsk_1600_robust64         1172
...
turbo                      1016
```

The interface bitrate includes preamble, training, PHY header, CRC, FEC
padding, trailing symbols, and the one-byte fragmentation header. It is a
no-error airtime ceiling; PTT delays, channel access, and frame errors reduce
the measured goodput.

The `turbo` profile is the fastest profile that completed the current
two-handheld over-the-air packet test without loss:

| Property | Value |
|---|---:|
| Modulation | 8PSK |
| Symbol rate | 1000 baud |
| Raw PHY rate | 3000 bit/s |
| FEC | TC128 rate 1/2 |
| Radio payload class | 64 bytes |
| Calculated interface bitrate | 1016 bit/s |
| Measured delivered goodput | 1008 bit/s |

That test delivered five dense 500-byte packets as 40 independently protected
radio frames (40/40 frames and 5/5 packets). It is a good-channel result, not a
sensitivity guarantee. The default YAML profile remains
`qpsk_1200_robust64`.

The experimental `bpsk_750_robust64` and `bpsk_1200_robust64` profiles are
robust/HF candidates. They use a dedicated balanced PRBS acquisition/training
sequence and retain soft LLR through ROBUST FEC. Their calculated interface
ceilings are 299 and 478 bit/s respectively. Both pass audio-band loopback and
phase/CFO/timing tests, but neither has yet been validated through a measured
HF fading/Doppler channel or over the air. BPSK-600 is not provided because a
ROBUST/64 frame exceeds the fixed realtime audio buffer.

## Radio packet path

```text
KISS TCP -> packet queue -> fragmentation -> FEC/CRC -> PSK/RRC -> ALSA/PTT
ALSA -> detector -> modem decoders -> ordered completion -> reassembly -> KISS TCP
```

Each radio fragment carries its own preamble, training, FEC, and CRC. The
fragmentation header is one byte and supports up to 128 fragments. The
configurable packet MTU is 500 to 16256 bytes; increasing the MTU does not
increase the physical frame size.

## Dependencies

Production runtime dependencies:

- C11 compiler and CMake 3.20 or newer
- LiquidDSP 1.6
- libcyaml
- ALSA (`libasound`)
- libgpiod
- POSIX threads

HackRF and Codec2 are used only by test and measurement tools and are detected
only when `BUILD_TESTING=ON`.

## Native build

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build -j
```

For a production-only build:

```sh
cmake -S . -B build -DBUILD_TESTING=OFF
cmake --build build -j
```

All project targets compile with strict warnings enabled for GCC and Clang.

## Configuration

The runtime is configured by YAML. A profile is selected by name; all PHY, FEC,
and detector parameters live in that profile record.

```yaml
modem:
  profile: qpsk_1200_robust64

runtime:
  tx_queue_packets: 4
  rx_queue_packets: 4
  channel_busy_timeout_ms: 10000
  rx_guard_ms: 150
  kiss_tcp_port: 8001
```

Validate a configuration and inspect the selected profile:

```sh
rtnc_check_config rtnc.yaml
rtnc_check_config rtnc.yaml turbo
```

The validator reports raw PHY bitrate, nominal post-FEC bitrate, calculated
Reticulum interface bitrate, and full-frame airtime.

Every YAML field, unit, range, and cross-field constraint is documented in
[`docs/config.md`](docs/config.md).

## Running the modem

Command-line syntax:

```text
rtnc_modem CONFIG.yaml [PROFILE [packet|kiss [SECONDS|0 [EXPECTED_FRAMES|0 [CAPTURE_PREFIX]]]]]
```

Run the KISS service indefinitely with the `turbo` profile:

```sh
rtnc_modem rtnc.yaml turbo kiss 0 0
```

The server listens on `127.0.0.1` at `runtime.kiss_tcp_port` and accepts one
client. It supports KISS data on port zero plus `TXDELAY`, `P`, `SLOTTIME`, and
`TXTAIL`. Other ports and commands are ignored.

Diagnostic packet receive mode:

```sh
rtnc_modem rtnc.yaml qpsk_1200_robust64 packet 60 1
```

`SECONDS=0` disables the time limit. `EXPECTED_FRAMES=0` disables the packet or
frame count limit. The process can then be stopped with `SIGINT` or `SIGTERM`.

## Tests

Run the deterministic unit, loopback, and synthetic channel suite:

```sh
ctest --test-dir build -E recorded_hackrf_handheld_replay --output-on-failure
```

The suite covers framing, fragmentation, queues, KISS, LDPC codecs, generated
FEC tables, noiseless modem loopback, calibrated AWGN points, carrier/phase
offset, timing recovery, sample-rate mismatch, voice-band filtering,
distortion, and equalizer state restoration.

The recorded-channel test requires its external recording to be present and is
therefore excluded from the default command above.

## Documentation

- [Offline-site OTA benchmark](docs/ota-benchmark.md)

- [`docs/config.md`](docs/config.md) — complete YAML reference
- [`docs/kiss-runtime.md`](docs/kiss-runtime.md) — KISS and packet runtime
- [`docs/fragmentation.md`](docs/fragmentation.md) — native fragmentation
- [`docs/fec-candidates.md`](docs/fec-candidates.md) — implemented FEC modes
- [`docs/radio-calibration.md`](docs/radio-calibration.md) — radio audio
  calibration
- [`docs/phy-profile-comparison.md`](docs/phy-profile-comparison.md) — measured
  profile comparisons
- [`docs/orangepi-build-and-performance.md`](docs/orangepi-build-and-performance.md)
  — Buildroot and Orange Pi results

## Status

The modem core, ALSA/GPIO runtime, native fragmentation, KISS TCP transport, and
over-the-air handheld packet path are implemented and have been exercised on
Orange Pi hardware. This is still experimental radio software: profile
performance depends strongly on radio audio response, signal level, and
channel conditions.
