# Two-radio automatic audio-path calibration

`rtnc_radio_calibrate` is one utility with symmetric user-facing modes. It
requires no HackRF, WAV files, or manual tone measurements. Run it on two
Orange Pi/radio sets tuned to the same channel with both squelches open:

```sh
# Transmitting set
rtnc_radio_calibrate rtnc.yaml tx

# Receiving set
rtnc_radio_calibrate rtnc.yaml rx

# Receiving set with a machine-readable report
rtnc_radio_calibrate rtnc.yaml rx radio-calibration.csv
```

The order does not matter. TX repeats batches of three self-identifying
approximately 13.8-second cycles until Ctrl-C. RX listens for at most 60
seconds, detects the first 1-kHz marker, captures the complete three-cycle
batch, and exits. All three cycles in one batch use a single PTT interval so
that keying latency cannot masquerade as response instability. TX deliberately
bypasses the configured response correction while retaining the configured
PTT lead/tail timing.

Each cycle tests 600--2700 Hz at PCM peaks 4000, 8000, 12000, and 16000. RX
rejects a higher level if any test frequency has lost more than 15 percent of
its small-signal gain. It then designs a 17-tap symmetric inverse-response FIR
over the selected PHY profile's occupied band. The reported gain prevents the
equalized waveform from exceeding the last linear test level. Example output:

```yaml
recommended configuration:
tx:
  filter_gain: 0.500
  response_eq_taps: [0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                     1.0,
                     0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
```

Copy these two values to the transmitting set and repeat once to validate the
corrected path. Swap `tx` and `rx` to calibrate the opposite direction.

## Complex response and group delay

The final 1.2 seconds of every two-radio cycle contain an eight-component
phase-coherent multitone. The components use fixed initial phases and are
transmitted simultaneously, so receiver gain changes affect magnitude but do
not destroy their relative phase. RX removes the best constant and linear
phase fit over the occupied band and reports:

- `amplitude_ripple_db`: maximum-to-minimum coherent-tone response;
- `relative_delay_ms`: slope of the fitted linear phase;
- `group_delay_ripple_ms`: peak-to-peak departure of adjacent-tone group
  delay;
- `residual_phase_rms_deg`: RMS phase left after removing constant phase and
  linear delay;
- one complex magnitude and unwrapped phase row for every sounding tone.

Example summary:

```text
occupied_band_hz=900.0..2400.0 amplitude_ripple_db=2.100 \
relative_delay_ms=0.720 group_delay_ripple_ms=0.180 \
residual_phase_rms_deg=4.30 points=6
phase_recommendation=linear-phase response; keep identity TX phase response
```

`relative_delay_ms` includes the arbitrary ALSA/radio capture offset and is
ambiguous by the reciprocal of the 300-Hz tone spacing (3.333 ms). It must not
be treated as an end-to-end latency measurement. Constant phase and linear
delay do not distort PSK; `group_delay_ripple_ms` and
`residual_phase_rms_deg` are the relevant channel-quality results.

The current recommendation calls a response approximately linear-phase when
the measured ripple is at most 15 percent of one symbol period. A larger value
does not automatically enable a phase-inverse TX filter. First save the result
for a deterministic channel replay and compare the existing fractionally
spaced RX equalizer. An inverse phase filter can amplify response nulls and is
specific to one TX-radio/RX-radio direction.

Repeat the measurement after swapping `tx` and `rx`. Two ordinary radios
measure the useful directional combinations TX-A/RX-B and TX-B/RX-A; they
cannot identify the individual radio responses separately.

RX prints a mean and population standard deviation over the three cycles. A
phase standard deviation at most 1 degree and group-delay-ripple standard
deviation at most 0.05 ms are classified as stable. It also prints
conservative screening grades for QPSK, 8PSK, and 16PSK:

| Modulation | `GOOD` phase/group-delay limits |
|---|---:|
| QPSK | 15 degrees / 0.50 ms |
| 8PSK | 8 degrees / 0.35 ms |
| 16PSK | 3 degrees / 0.15 ms |

Up to twice both limits is `MARGINAL`; anything worse is `POOR`. These grades
are deliberately only a screening tool. Dense packet goodput remains the
acceptance criterion because the sounding cycle does not measure receiver
noise sensitivity or time variation outside the batch.

When a report filename is supplied in RX mode, the utility writes one CSV row
per cycle plus a mean row. The file contains amplitude ripple, relative delay,
group-delay ripple, and residual phase and can be archived as a directional
radio-pair record or used to construct a deterministic replay fixture.

The same coherent tone coefficients are also reanalyzed for every entry in
the YAML `profiles` list. Each `profile_suitability` row uses that profile's
carrier, RRC occupied bandwidth, modulation, and symbol period. The `GOOD`
group-delay limits scale with symbol period: 0.50 symbol for BPSK, 0.60 for
QPSK, 0.35 for 8PSK, and 0.15 for 16PSK. Corresponding residual-phase limits
are 30, 15, 8, and 3 degrees. The extended CSV contains the same per-profile
rows, so one radio measurement can screen every configured fixed profile
without retransmitting a different modem waveform.

## HackRF receive backend

When only the transmitting Orange Pi/radio set is available, the native host
build of the same utility can replace the second radio with HackRF. Start the
receiver first:

```sh
build/rtnc_radio_calibrate hackrf-rx \
  446006250 16 14 0 1200 1650 0.25
```

The arguments after `hackrf-rx` are frequency in Hz, LNA gain, VGA gain,
frequency offset in Hz, symbol rate, audio carrier, and RRC rolloff. Then run
the unchanged TX mode on the Orange Pi:

```sh
ssh root@orange-pi /tmp/rtnc_radio_calibrate /tmp/rtnc.yaml tx
```

HackRF captures and NFM-demodulates one complete cycle to 48-kHz audio through
LiquidDSP, then uses the same marker, linear-level, response-fit, and YAML
recommendation rules as the ALSA backend. Stop the repeating TX with Ctrl-C
after the receiver prints its recommendation.

PCM 8000 is the linearity reference. PCM 12000 and 16000 are accepted only
when their normalized fundamental response has compressed by less than 15
percent; the 900-Hz H2/H3 vector must also remain below 25 percent. PCM 4000
remains a low-level diagnostic but is not the reference because its harmonic
estimate can be dominated by receiver noise.

Choose HackRF gains that keep the carrier below clipping and produce
`hackrf_dropped_blocks=0`. The fitted correction is specific to the occupied
band defined by the supplied baud, carrier, and RRC rolloff.

Do not maximize the HackRF level. In the 446.00625-MHz bench setup, LNA 32 /
VGA 20 put the IQ stream around -4 to -9 dBFS and produced false H3/THD from
receiver overload. LNA 24 / VGA 20 put it around -15 dBFS and measured the
same 900-Hz transmission at roughly 17 percent distortion. A practical target
for calibration is about -12 to -18 dBFS peak IQ with clear margin from the
noise floor. The HackRF backend captures for 36 seconds so that it can skip a
partly missed cycle and still analyze the following complete cycle.

Treat the printed FIR as a candidate, not as proof that equalization helps the
packet waveform. On the charged replacement handheld, the 24/20 calibration
completed without drops and proposed `filter_gain: 0.130` plus a strongly
shaped symmetric FIR. A same-path QPSK-1200 OTA comparison decoded all 24
fragments with both configurations, but the candidate produced many EVM
values around 0.7--1.0 while the identity FIR at gain 0.30 stayed at
0.176--0.213. The identity FIR was therefore retained. This packet validation
is mandatory before adopting a generated radio preset.

The FIR is constrained to be symmetric. Configuration validation rejects
asymmetric taps and any response that crosses zero in the selected occupied
band. TX uses centered convolution because the complete packet waveform is
already buffered; consequently the equalizer has zero phase rather than a
causal group delay. It changes amplitude response only. `deemphasis_us` is no
longer a configuration field and is rejected as an unknown key.

This is deliberately an end-to-end directional calibration. With two ordinary
radio sets alone, their individual TX and RX responses cannot be separated;
the measurement contains TX radio A and RX radio B together. That is exactly
the response seen by packets in that direction. HackRF measurements remain an
optional engineering backend for creating reusable model-specific radio
presets, not a requirement for an end user.

The recommendation is invalid if the receiver clips, AGC changes gain during
the cycle, squelch closes, another signal occupies the channel, or capture
reports XRUN/dropped blocks. The radios should remain stationary and their
hardware audio controls must not be changed between calibration and operation.

On the first two-Orange-Pi OTA run, the receiver falsely reported 37--50
percent distortion despite FreeDV reporting 26.4-dB SINAD. Diagnostics exposed
more than 1000 dropped audio blocks and fundamental amplitudes near zero. The
ALSA calibrator had repeatedly rescanned the entire accumulated capture while
looking for the marker, causing quadratic CPU work that could not keep up on
the Orange Pi. Marker detection is now incremental and analyzes each window
once. With unchanged mixer and hardware levels, the corrected run had zero
XRUNs/drops, selected PCM 8000, and measured 3.0-percent distortion at 900 Hz.
It recommended gain 0.359 and a mild symmetric FIR for the measured
192.168.1.54 TX to 192.168.1.197 RX direction. This remains a candidate until
an identity-versus-corrected packet comparison is complete. The immediate
three-packet comparison decoded no complete 500-byte packet with either
setting: identity produced 10 sequence-rejected CRC-valid continuations, while
the candidate produced 8. Both runs detected all 24 bursts with no audio or
decode-queue drops. The candidate improved best training from 0.879 to 0.977
but did not recover FEC, so identity/gain 0.30 was restored.

After fixing the stale ARM build and replacing the realtime decoder
bottleneck, the strong-signal A/B was repeated with ten dense 500-byte packets.
Identity/gain 0.30 delivered 10/10 packets and 80/80 frames. The calibrated
FIR/gain 0.359 delivered 9/10; one missing frame invalidated packet 4, with no
ALSA or queue drops. The FIR is therefore not selected for this direction.
An RF-attenuated near-threshold comparison remains useful as confirmation, but
identity is the default unless that controlled test shows a clear reversal.

The near-threshold confirmation used -70 dB at the .54 transmitter, a 50-ohm
load instead of its antenna, and no antenna on the .197 receiver. Identity
detected 39/40 bursts and delivered 0/5 packets at 5.74-dB average effective
SNR (4.48-dB minimum). The candidate detected 40/40 but also delivered 0/5,
with lower 5.29-dB average and 3.80-dB minimum effective SNR. Together with
the strong-signal 10/10 versus 9/10 result, this rejects the generated FIR for
the .54-TX to .197-RX direction; retain identity/gain 0.30.

A later transition-point A/B/A used TX -60 dB plus dummy load and RX antenna
through -40 dB. The first identity block delivered 1/5 at 7.09-dB average
effective SNR, the FIR block delivered 5/5 at 11.70 dB, and the immediate
identity repeat delivered 4/5 at 11.76 dB. Thus the apparent FIR improvement
coincided with a roughly 4.6-dB channel change; at matched SNR, FIR and identity
were indistinguishable in this short sample. This does not reverse the
strong-signal 10/10 identity versus 9/10 FIR selection.

## Initial coherent two-radio measurement

The first coherent test on 2026-08-12 used the selected
`qpsk_1200_robust64` occupied band (900--2400 Hz). Both captures completed
with zero ALSA XRUNs and zero dropped blocks:

| Direction | Amplitude ripple | Group-delay ripple | Residual phase RMS |
|---|---:|---:|---:|
| `.54` TX to `.197` RX, run 1 | 7.293 dB | 0.316 ms | 7.51 degrees |
| `.54` TX to `.197` RX, run 2 | 7.360 dB | 0.318 ms | 7.54 degrees |
| `.197` TX to `.54` RX | 6.802 dB | 0.306 ms | 7.46 degrees |

The two repeated forward measurements agree closely even though their fitted
relative delays changed from 0.541 to 1.144 ms due to capture alignment. This
is the expected behavior: relative delay is ambiguous, while nonlinear phase
and group-delay ripple are stable. A complete digital multitone/DFT regression
with an arbitrary sample delay reports effectively zero ripple on an ideal
channel, excluding the sounding waveform itself as the source of the measured
approximately 0.31-ms result.

The result is significant relative to a 1200-baud symbol but does not justify
an automatic inverse-phase TX filter. Existing QPSK and 8PSK packet results
show that the RX synchronization/equalizer can tolerate this channel. The
approximately 7.5-degree residual phase is, however, a plausible contributor
to the failed 16PSK experiment because adjacent 16PSK points are only 22.5
degrees apart. The next engineering step is deterministic replay using a
captured complex response, followed by an RX-equalizer comparison.

That deterministic comparison now uses a committed real causal FIR fixture
interpolated from the first `.54` TX to `.197` RX complex measurement. It
preserves the measured nonlinear phase after removing only arbitrary constant
phase and linear capture delay. Twenty different 64-byte 16PSK-1000 ROBUST
frames produced:

| Decoder path | CRC-valid frames | Average EVM |
|---|---:|---:|
| Ordinary one-sample/symbol fast path | 0/20 | 0.280 |
| Fractionally-spaced equalizer fallback | 20/20 | 0.064 |

All 20 successful frames explicitly used the equalizer. For a phase-only
error, EVM 0.064 is approximately 3.7 degrees RMS, close to the practical
16PSK boundary. This shows that the existing RX equalizer can compensate the
deterministic response measured by the calibrator. It does not guarantee OTA
16PSK reliability: the replay contains neither radio noise nor time-varying
phase, and the measured OTA run still lost about half its physical frames.
The next 16PSK improvement should therefore target post-equalizer margin and
time variation rather than adding a fixed TX phase-inverse filter.

The first complete three-cycle procedure then measured `.54` TX to `.197` RX
as follows:

```text
summary cycles=3 amplitude_ripple_db=7.518 stddev=0.111 \
group_delay_ripple_ms=0.316 stddev=0.001 \
residual_phase_rms_deg=7.50 stddev=0.03
suitability qpsk=GOOD 8psk=GOOD 16psk=POOR stability=GOOD
```

The capture had zero XRUNs and zero dropped blocks. The archived report is
`recordings/radio-calibration/2026-08-12_54-to-197.csv`. This confirms that the
approximately 0.31-ms/7.5-degree response is stable within one keyed batch,
not short-term measurement noise.

After adding per-profile evaluation, the same direction was measured again
with a denser 200-Hz coherent grid. All three cycles completed without XRUNs
or dropped blocks. The aggregate stability was GOOD: group-delay ripple was
0.409 +/- 0.003 ms and residual phase was 8.75 +/- 0.09 degrees. The profile
screening result was:

| Result | Profiles |
|---|---|
| GOOD | `bpsk_750_robust64`, `bpsk_1200_robust64`, `qpsk_1200_robust64`, `qpsk_800_robust64`, `8psk_600_robust64`, `8psk_800_robust64` |
| MARGINAL | QPSK 1500/1600 profiles centered near 1650--1800 Hz; all 8PSK 1000/1200/1600 profiles including `turbo` |
| POOR | QPSK profiles whose occupied band extends far below 600 Hz or above 2800 Hz; both experimental 16PSK profiles |

The full per-profile values are archived in
`recordings/radio-calibration/2026-08-12_all-profiles_54-to-197.csv`. These
grades characterize the measured linear channel response and its short-term
stability; they are a screening result, not an OTA packet success test.
