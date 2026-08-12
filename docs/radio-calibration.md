# Two-radio automatic audio-path calibration

`rtnc_radio_calibrate` is one utility with symmetric user-facing modes. It
requires no HackRF, WAV files, or manual tone measurements. Run it on two
Orange Pi/radio sets tuned to the same channel with both squelches open:

```sh
# Transmitting set
rtnc_radio_calibrate rtnc.yaml tx

# Receiving set
rtnc_radio_calibrate rtnc.yaml rx
```

The order does not matter. TX repeats a self-identifying approximately
12.3-second cycle until Ctrl-C. RX listens for at most 28 seconds, detects the
1-kHz marker, captures the remainder of that cycle, and exits as soon as the
complete sweep is available. TX deliberately bypasses the configured response
correction while retaining the configured PTT lead/tail timing.

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
