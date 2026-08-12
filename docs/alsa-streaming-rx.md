# Orange Pi ALSA streaming receiver

`rtnc_alsa_rx` connects the production-style ALSA capture path to the modem
without running DSP in the capture pthread:

```text
ALSA pthread -> 10-ms SPSC blocks -> streaming detector
             -> two-frame decode queue -> modem decoder pthread
```

## Adaptive detection

Power alone is not a reliable packet detector with the handheld squelch open:
band-limited noise can be stronger than the PSK waveform. The receiver still
tracks power adaptively for telemetry and optional gating, using separate noise
attack/release time constants, a fast signal estimate, hysteresis, impulse
clamping, and a maximum-busy bound. ALSA opening is instead decided by a
normalized streaming correlation against the 32 acquisition symbols after
carrier downconversion and the same RRC matched filter as the modem.

The correlator evaluates every four audio samples. Its window spacing comes
from `profile.samples_per_symbol`; carrier, RRC rolloff, symbol rate and
correlation threshold also come from the selected profile/configuration. The
diagnostic power-filter bandwidth is derived as:

```text
0.5 * baud * (1 + RRC alpha) * bandwidth_margin
```

After a correlation hit, the detector captures one bounded maximum window.
The ordinary modem decoder performs its own normalized acquisition check and
is the final authority before FEC and CRC. This avoids making physical-frame
boundaries depend on energy valleys or a hard-coded symbol duration.

The current YAML exposes operational detector settings:

```yaml
detector:
  warmup_ms: 2000
  pretrigger_ms: 100
  cooldown_ms: 100
  noise_attack_ms: 1000
  noise_release_ms: 2000
  signal_attack_ms: 1
  maximum_busy_ms: 2000
  energy_open_ratio: 4.0
  energy_close_ratio: 1.3
  impulse_limit_ratio: 20.0
  correlation_threshold: 0.90
  bandwidth_margin: 1.35
```

Millisecond values are converted using the configured audio sample rate. PHY
bandwidth and symbol spacing are derived from the fixed selected profile, so a
future PSK rate change does not require editing DSP constants in this runner.

## OTA result

A packet-only HackRF transmission through the handheld receiver produced:

```text
decoded sequence=0 bytes=64 acquisition=0.978 training=0.955 evm=0.371 attempts=1
finished bursts=1 attempts=1 audio_drops=0 decode_drops=0 capture_xruns=0
best_stream_correlation=0.977
```

All 64 bytes matched and passed FEC/CRC. A separate calibration tone was also
correctly rejected by the correlation stage. The HackRF IQ generator therefore
has an explicit `CALIBRATION_TONE_0_OR_1` argument for packet-only tests.
