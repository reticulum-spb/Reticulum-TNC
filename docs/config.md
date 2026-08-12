# `rtnc.yaml` configuration reference

This document describes every YAML field read by the current platform layer.
Unknown fields are errors. The complete configuration is validated at startup;
if any constraint fails, the application reports an invalid configuration and
does not open the audio device or PTT GPIO.

Fields ending in `_ms` are measured in milliseconds, and fields ending in
`_hz` are measured in hertz. Specify every field explicitly, as in the supplied
[`rtnc.yaml`](../rtnc.yaml). Some scalar fields are technically optional in the
YAML schema and default to zero, but zero fails validation for most of them.

## `ptt`

GPIO PTT control through libgpiod.

| Field | Allowed values | Meaning |
|---|---|---|
| `port` | integer `0..255` | GPIO chip number passed to `gpiod_chip_open_by_number()`. This is normally `0` on the tested Orange Pi. |
| `pin` | integer `0..255` | Line number within the selected GPIO chip. This is not necessarily the physical header pin number. |
| `active_high` | `true` or `false` | `true` keys PTT with logic high; `false` uses active-low PTT. The opposite level is driven while idle. |

An invalid chip/line, a busy GPIO, or insufficient permissions is detected when
GPIO is initialized rather than by the YAML validator.

## `audio`

ALSA playback and capture settings. TX and RX open the same named PCM device.

| Field | Allowed values | Meaning |
|---|---|---|
| `device` | string, `1..127` characters | ALSA PCM name, for example `default`, `hw:0,0`, or a name defined in `asound.conf`. |
| `sample_rate_hz` | exactly `48000` | Current internal DSP sample rate. Resampling for other audio rates is not implemented. |
| `period_ms` | exactly `10` | ALSA period duration. At 48 kHz this is 480 samples. |
| `periods` | integer `2..8` | Number of periods in the ALSA buffer. More periods tolerate longer scheduler stalls but add audio latency. |

ALSA must negotiate the exact requested rate and period size. The modem does
not silently accept a nearby sample rate.

## `modem`

| Field | Allowed values | Meaning |
|---|---|---|
| `profile` | name of `1..31` characters present in `profiles` | Default profile used by tools that do not receive a profile name on the command line. Names are case-sensitive. |
| `control_profile` | optional, different profile name present in `profiles` | Enables a second receive-only detector with the control preamble. Omit the field to disable the second detector and its CPU/memory cost. Normal transmission continues to use `profile`; callers that need control transmission explicitly use an initialized control modem pointer. |

`rtnc_modem` can select any profile without modifying the YAML:

```sh
rtnc_modem rtnc.yaml turbo kiss 0 0
```

Running it with only a configuration file lists every available profile:

```sh
rtnc_modem rtnc.yaml
```

## `profiles`

A sequence of `1..24` complete PHY profile records. Every name must be unique.
The selected profile remains fixed while the process runs; modulation, symbol
rate, payload class, and FEC are not negotiated over the air.

### Profile fields

| Field | Allowed values | Meaning |
|---|---|---|
| `name` | unique string, `1..31` characters | Profile name used by `modem.profile` and command-line tools. |
| `modulation` | `bpsk`, `qpsk`, `8psk`, or `16psk` | BPSK, QPSK, 8PSK, and 16PSK carry 1, 2, 3, and 4 bits/symbol respectively. Raw bitrate is `symbol_rate_baud * bits_per_symbol`. BPSK is intended for experimental robust/HF profiles; 16PSK requires a substantially cleaner channel. |
| `symbol_rate_baud` | one of the exact values listed below | Symbol rate. It must produce an integer number of samples per symbol at 48 kHz. |
| `carrier_hz` | finite number, strictly `300 < value < 3000` | PSK center frequency in the audio baseband. The occupied band must also fit the real radio audio response. |
| `rrc_rolloff` | finite number, `0 < value <= 1` | Root-raised-cosine rolloff. A smaller value narrows the theoretical occupied band but increases channel and synchronization requirements. |
| `acquisition_threshold` | finite number, `0 < value <= 1` | Minimum normalized acquisition-preamble correlation. A higher value rejects more false triggers but can miss distorted frames. |
| `training_threshold` | finite number, `0 < value <= 1` | Minimum training-sequence correlation required to continue decoding. |
| `detector_bandwidth_margin` | `1.0..3.0` | Multiplier applied to the theoretical RRC bandwidth for the input power filter. Values above 1 allow for CFO and channel imperfections. |
| `fec_mode` | `0`, `1`, or `2` | FEC selection, described below. |
| `payload_class_bytes` | exactly `64` or `128` | Maximum payload of one independently synchronized, FEC-protected, CRC-checked radio frame. This is not the Reticulum MTU; larger packets are fragmented. |

### Exact `symbol_rate_baud` values

The current DSP requires 20 to 80 integer samples per symbol. At the fixed
48 kHz sample rate, only these symbol rates are valid:

| `symbol_rate_baud` | Samples per symbol |
|---:|---:|
| `600` | 80 |
| `640` | 75 |
| `750` | 64 |
| `800` | 60 |
| `960` | 50 |
| `1000` | 48 |
| `1200` | 40 |
| `1500` | 32 |
| `1600` | 30 |
| `1920` | 25 |
| `2000` | 24 |
| `2400` | 20 |

All other integer values are rejected, including values close to those in the
table. Passing validation does not prove that a high-rate profile fits the
audio bandwidth of a particular NFM radio.

A complete profile must also fit one maximum physical frame in the fixed
90,000-sample realtime buffer. For example, BPSK-600 with ROBUST FEC and a
64-byte payload class is rejected because its frame is too long; use
`bpsk_750_1_64` or `bpsk_1200_1_64` instead.

### Calculated profile rates

The modem calculates these values from each profile; they are not YAML fields:

- `raw_bitrate_bps = symbol_rate_baud * bits_per_symbol`
- `fec_bitrate_bps`: nominal bitrate after the selected FEC rate
- `interface_bitrate_bps`: theoretical Reticulum payload ceiling, rounded down

`interface_bitrate_bps` subtracts the one-byte fragmentation header from the
radio payload and includes acquisition, training, PHY header, CRC, FEC padding,
and trailing symbols in airtime. It is intended as the initial Reticulum
interface bitrate. It does not include PTT lead/tail, channel access delays,
FER, or retries, all of which reduce measured goodput.

Inspect one profile in detail:

```sh
rtnc_check_config rtnc.yaml turbo
```

List all names and interface bitrates without opening ALSA or GPIO:

```sh
rtnc_modem rtnc.yaml
```

The list preserves the order of entries in `profiles`.

### `fec_mode` values

| Value | Internal mode | Meaning |
|---:|---|---|
| `0` | `FEC_NONE` | No FEC; intended primarily for tests. |
| `1` | `FEC_LDPC_ROBUST` | Unpunctured CCSDS TC128 LDPC, rate 1/2. |
| `2` | `FEC_LDPC_NORMAL` | Punctured TC128, rate 2/3; lower redundancy and lower SNR margin. |

`FEC_LDPC_FAST` is reserved in the internal API but is not implemented as a
YAML mode. Value `3` is rejected.

## `tx`

| Field | Allowed values | Meaning |
|---|---|---|
| `lead_ms` | integer `0..1000` | Delay from asserting PTT to the first audio sample. This initializes KISS `TXDELAY`; a KISS command can override it at runtime. |
| `tail_ms` | integer `0..1000` | Time PTT remains asserted after the final audio sample. This initializes KISS `TXTAIL`; a KISS command can override it at runtime. |
| `filter_gain` | finite number `0.1..4.0` | Linear gain applied after the TX response FIR. Set it for sufficient deviation without clipping. |
| `response_eq_taps` | exactly 17 finite floating-point values | FIR pre-equalization for the transmit audio response. Use `[0,...,0,1,0,...,0]` for an identity filter. |

Additional `response_eq_taps` constraints:

- coefficients must be symmetric around `tap[8]` within `1e-5`, ensuring a
  linear-phase response and constant group delay;
- at least one coefficient must have magnitude greater than `1e-6`;
- the sum of coefficient magnitudes must not exceed `8.0`;
- zero-phase gain must remain greater than `0.05` across the occupied band of
  the profile selected by `modem.profile`, checked every 100 Hz.

Generate coefficients with `rtnc_radio_calibrate` rather than tuning them by
hand. Because `filter_gain` is applied after the FIR, recheck the output level
after changing the taps.

## `link`

| Field | Allowed values | Meaning |
|---|---|---|
| `mtu` | integer `500..16256` bytes | Maximum packet accepted or produced by the packet/KISS interface after reassembly. Packets are split into 64-byte or 128-byte radio payloads. Reticulum requires an interface MTU of at least 500. |
| `reassembly_timeout_ms` | integer `1000..60000` | Maximum time an incomplete received packet is retained between fragments. Expiry resets reassembly. |

Both endpoints must allow a sufficiently large MTU. Increasing `mtu` does not
make a PHY frame longer; it only permits more fragments in one packet.

## `workers`

All queues use fixed preallocated storage. These settings do not introduce heap
allocation into the realtime DSP path.

| Field | Allowed values | Meaning |
|---|---|---|
| `dsp_queue_blocks` | integer `2..32` | Capacity of the queue of 10 ms blocks between ALSA capture and DSP. More blocks tolerate longer scheduler stalls but increase possible latency. |
| `decode_queue_frames` | integer `2..8` | Captured frame candidates waiting for the primary decoder worker. |
| `equalizer_queue_frames` | integer `1..8` | Queue capacity for the second/equalizer decoder worker when parallel decoding is enabled. |
| `parallel_equalizer` | `true` or `false` | Enables a second decoder worker and distributes consecutive candidates between the two queues. |
| `equalizer_nice` | integer `0..19` | Linux nice increment for the second worker. `0` gives equal priority; larger values reduce its priority relative to other services. |

## `detector`

The detector maintains adaptive signal and noise power estimates. In
`rtnc_modem`, a preamble correlator and an energy check confirm the start of a
candidate; the capture window is then derived from the selected PHY frame
duration.

| Field | Allowed values | Meaning |
|---|---|---|
| `warmup_ms` | integer `100..2000` | Initial noise-estimation time during which the detector cannot open a frame. |
| `pretrigger_ms` | integer `20..250` | Audio history prepended to a detected candidate so the preamble start is retained. |
| `cooldown_ms` | integer `0..1000` | Delay after closing one candidate before another can open. Realtime `rtnc_modem` currently forces this to zero so dense fragments are not missed; the field remains available to other detector users. |
| `noise_attack_ms` | integer `10..10000`, no greater than `noise_release_ms` | Noise-floor time constant while estimated power rises. A smaller value follows increases more quickly. |
| `noise_release_ms` | integer from `noise_attack_ms` through `10000` | Noise-floor time constant while estimated power falls. |
| `signal_attack_ms` | integer `1..500` | Time constant of the faster current-signal power estimate. |
| `maximum_busy_ms` | integer `500..5000` | Hard upper bound for one captured burst. The compile-time modem buffer can impose a lower effective limit. |
| `energy_open_ratio` | finite number strictly greater than `1.0` | Signal-to-noise ratio for independent energy opening in generic detector modes. Must exceed `energy_close_ratio`. |
| `energy_close_ratio` | finite number satisfying `1.0 < value < energy_open_ratio` | Lower hysteresis threshold and minimum energy used to confirm an external correlation trigger. |
| `impulse_limit_ratio` | finite number strictly greater than `energy_open_ratio` | Limits the contribution of a single high-power impulse to the adaptive noise estimate. |

The correlation threshold is profile-specific and is configured through
`profiles[].acquisition_threshold`.

## `runtime`

Packet runtime and minimal KISS TCP backend settings.

| Field | Allowed values | Meaning |
|---|---|---|
| `tx_queue_packets` | integer `1..8` | Capacity of the complete-packet TX queue before fragmentation. A new packet is rejected when the queue is full. |
| `rx_queue_packets` | integer `1..8` | Capacity of the completely reassembled RX packet queue before delivery to KISS. |
| `channel_busy_timeout_ms` | integer `100..60000` | Maximum time TX waits for channel access before failing. KISS p-persistence trials occur within this interval. |
| `rx_guard_ms` | integer `0..2000` | Guard time after RX activity before local transmission is allowed. |
| `kiss_tcp_port` | integer `1024..65535` | TCP port for the KISS server. It listens on loopback (`127.0.0.1`) and accepts one client. |

KISS `TXDELAY`, `P`, `SLOTTIME`, and `TXTAIL` commands change their runtime
values without restarting the modem. `TXDELAY` and `TXTAIL` start from
`tx.lead_ms` and `tx.tail_ms`; persistence starts at 255, and slot time starts
at 100 ms. KISS time values use 10 ms units.

## Validation

Validate the YAML without opening the radio devices:

```sh
rtnc_check_config rtnc.yaml
```

Both radio endpoints must use matching PHY profile contents, `fec_mode`, and
`payload_class_bytes`. These values are neither transmitted nor negotiated over
the air.
