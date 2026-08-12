# Fixed PSK profile comparison

## Two-radio transition-point speed check (2026-08-11)

The bench used .54 TX into a 50-ohm load with 60-dB attenuation and .197 RX
with an antenna behind 40 dB. Fast QPSK profiles used the same 0.70/0.55
acquisition/training thresholds as QPSK-1200; the old 0.90/0.70 values had
rejected otherwise usable bursts.

| Profile | Packets | Bursts | TX samples | Delivered goodput |
|---|---:|---:|---:|---:|
| QPSK-1600 robust64 | 3/5 | 40/40 | 825600 | about 87 B/s |
| QPSK-1600 robust128 | 3/5 | 20/20 | 720000 | about 100 B/s |
| QPSK-1600 normal128 | 1/5 | 20/20 | 556800 | about 43 B/s |
| QPSK-1920 robust64 | 0/5 | 40/40 | 688000 | 0 B/s |
| QPSK-2400 robust64 | 0/5 | 40/40 | 550400 | 0 B/s |

QPSK-1600 robust128 is the preliminary maximum-goodput profile at this channel
point. Do not replace the selected QPSK-1200 profile from this short,
time-varying series alone. Bad 128-byte frames took up to 3.16 seconds to
decode, so latency/CPU must also be improved before selecting it.

Profile selection was completed before fragmentation and Reticulum integration.
All candidates use 48-kHz audio, QPSK, robust TC128 FEC, and a 64-byte radio
payload. The profile is selected by name from `rtnc.yaml`; no rate negotiation
is performed over the air.

## Offline results

Deterministic audio loopback passes at 1200, 1500, 1600, 1920, 2000, and 2400
baud. The robust-64 noiseless ceiling rises from 111.6 B/s at 1200 to 223.3 B/s
at 2400. A fixed-seed calibrated 8/12/16/20-dB Eb/N0 sweep does not show an
intrinsic DSP sensitivity penalty at higher baud rates. Synthetic 300--3000-Hz
filtering passes CRC for every rate, but EVM rises from 0.130 at 1200 to 0.310
at 2400, predicting greater sensitivity to the real handheld response.

## Loaded OTA results

Tests used HackRF NFM TX at VGA 20, the handheld receiver audio path, and an
Orange Pi running `rnsd-rs` and `lxmd-rs`. Dense frames had no added gap. The
32-block capture ring prevented audio drops, so failures below are channel/EQ
failures rather than pipeline loss.

| Profile | Result | Observation |
|---|---:|---|
| 1200/1650/alpha .25 | 18/20 | 98.7 useful B/s over the complete RF series |
| 1500/1800/alpha .25 | 5/20 | 34.1 useful B/s |
| 1600/1650/alpha .25 | 1/8 | all preambles detected |
| 1600/1800/alpha .25 | 4/8 | best 1600 carrier placement |
| 1920/1650/alpha .25 | 0/8 | all preambles detected, payload rejected |
| 2000/1650/alpha .25 | 0/8 | only 3/8 preambles crossed threshold |
| 2400/1650/alpha .25 | 0/8 | stream correlation fell to 0.796 |

For 1600 baud, carrier 1300/1450/1650/1800 variants gave 0/8, 0/8, 1/8, and
4/8 respectively. Narrowing RRC alpha to 0.15 at carrier 1800 gave 0/8.
Changing 1500-baud NFM deviation to 1600 or 2600 Hz gave 4/8 and 3/8, so the
long-series deficit is not corrected by the tested deviation range.

## Existing FreeDV-TNC reference

The deployed FreeDV-TNC configuration is 4FSK at 500 baud, with tones at
1000/1600/2200/2800 Hz and the `H_256_768_22` LDPC code. This is a rate-1/3
code: 256 information bits become 768 coded bits. FreeDV adds a 32-bit unique
word, so one radio frame contains 800 bits, or 400 4FSK symbols, and occupies
exactly 0.800 s at 500 baud. The 32 information bytes contain a one-byte
fragment header and a two-byte CRC, leaving 29 application bytes per frame.

FreeDV uses a 100-symbol (0.200-s) preamble once per burst and no FSK-LDPC
postamble. The current FreeDV-TNC implementation additionally sends 0.500 s of
silence before and 0.250 s after the burst. A 500-byte packet therefore needs
18 independently coded radio frames:

```
0.500 + 0.200 + 18 * 0.800 + 0.250 = 15.350 s
500 / 15.350 = 32.6 application B/s
```

Excluding PTT silence but retaining the acquisition preamble, the ceiling is
34.2 B/s. Thus 500 baud alone understates the robustness cost: this profile
trades two thirds of its raw 1000 bit/s for LDPC parity. Its packet delivery
rate and waterfall still need to be measured over the same radio path; the
calculation is an airtime baseline, not a reliability result.

## Selection

The original QPSK-only sweep retained `qpsk_1200_robust64`, but the subsequent
lower-baud experiment below makes `turbo` (8PSK-1000 ROBUST/64) the leading
experimental candidate. The production default is not changed yet. Fragmentation and
Reticulum integration remain gated on a direct, repeatable FreeDV/PSK
packet-level comparison and a receive-power waterfall.

## Lower-baud 8PSK experiment

The PHY and YAML profile records now select `qpsk` or `8psk` explicitly.
Packing, frame-symbol counts, capture duration, and soft-LLR counts are derived
from bits per symbol; the padded final 8PSK symbol does not add an LLR to the
FEC input. All profiles pass deterministic noiseless, calibrated AWGN, and
synthetic 300--3000-Hz tests.

At the same HackRF/radio settings used above (VGA 20, deviation 2100 Hz), the
first dense OTA series produced:

| Profile | OTA result | Noiseless ceiling | Typical OTA EVM |
|---|---:|---:|---:|
| QPSK-800 | 8/8 | 74.4 B/s | 0.26--0.35 |
| 8PSK-600 | 8/8 | 77.4 B/s | 0.18--0.22 |
| 8PSK-800 | 8/8 | 103.2 B/s | 0.20--0.25 |
| 8PSK-1000 | 8/8, then 20/20 | 129.0 B/s | 0.21--0.31 |

The 20-frame 8PSK-1000 RF series carried 1280 payload bytes in 10.120 s, or
126.5 delivered B/s including HackRF stream padding. It had no EQ retries,
audio XRUNs, or queue drops while `rnsd-rs` and `lxmd-rs` were running. This is
a stronger candidate than QPSK-1200's earlier 18/20 and 98.7 B/s result, but a
power/SNR waterfall and same-path FreeDV baseline are still required before
selecting the production profile.

## Same-direction radio-TX waterfall

To remove the asymmetry of HackRF TX versus handheld TX, both modem waveforms
were next played through the Orange Pi ALSA device and the same handheld
transmitter. HackRF received at 2 Msps with amp disabled and a measured carrier
offset of about -70 Hz. PSK PCM peak was changed from 12000 to 16383 to match
Codec2 `FSK_SCALE`; repeating 8PSK-1000 before and after that change showed no
material difference, so under-deviation was excluded as the failure cause.

FreeDV used its unmodified `send-modem` mode. Each 64-byte test message becomes
three 32-byte information frames (BEGIN/FRAG/END), with 29 application bytes
available per physical frame. The offline reference decoder uses Codec2's
`freedv_rawdatarx()` and separately checks the FreeDV-TNC CRC.

| HackRF RX gain | Approx. signal | FreeDV 4FSK-500 | QPSK-800 | 8PSK-600 | 8PSK-1000 |
|---|---:|---:|---:|---:|---:|
| LNA 24 / VGA 16 | -15 to -16 dBFS | 6/6 PHY, 2/2 messages | 10/10 | 8/10 | 5/10 |
| LNA 24 / VGA 12 | about -20 dBFS | 5/~6 PHY, 1/2 messages | 5/10 | not run | not run |
| LNA 16 / VGA 12 | about -24 dBFS | 0 | 0/10 | not run | 0/10 |

The sample counts are preliminary rather than confidence-grade statistics.
They nevertheless show two useful effects. First, symbol duration dominates
the handheld TX path: QPSK-800 is substantially more reliable than either
8PSK profile at the same receive point. Second, FreeDV's strong rate-1/3 code
has lower physical-frame FER, but needing three dependent frames per 64-byte
message reduces its end-to-end advantage. QPSK-800 currently offers the best
robustness/goodput compromise: 74.4 B/s ceiling versus approximately 34 B/s for
the FreeDV burst before accounting for packet loss.

These gain settings change the HackRF receiver and are not calibrated Eb/N0.
A larger run with measured channel power/noise and complete-message counters is
still required before freezing the production profile.

## Fixed TX-response correction

The handheld TX microphone response was subsequently measured separately and
a fixed 190-us, unity-gain de-emphasis filter was added ahead of ALSA playback.
This moves the upper modem band out of the transmitter limiter and flattens the
central 1.1--2.2-kHz response to roughly +/-0.8 dB. With the physical control,
radio power, and HackRF settings held fixed (LNA 24, VGA 12, amp disabled), the
following dense zero-gap strong-signal series were received:

| Corrected profile | CRC-valid frames | EVM range | Decode queue maximum |
|---|---:|---:|---:|
| QPSK-1200 | 5/5 | 0.219--0.302 | 1 |
| QPSK-1600 | 5/5 | 0.218--0.308 | 1 |
| 8PSK-800 | 5/5 | 0.189--0.203 | 1 |
| 8PSK-1000 | 5/5 | 0.177--0.203 | 1 |
| QPSK-2400 | 5/5 | 0.231--0.510 | 1 |

An additional uninterrupted 8PSK-1000 run delivered 20/20 frames in exactly
20 decoder attempts. It had zero IQ/decode drops, maximum decode-queue depth
one, maximum queue delay 1.1 ms, and maximum decode time 19.1 ms. QPSK-2400
needed equalizer restoration on three of five frames and had two high-EVM
frames, so its 5/5 result has visibly less margin than the lower-rate profiles.

This reverses the earlier poor 8PSK strong-signal result and shows that the
uncorrected transmitter response/limiting was a material impairment. It does
not yet establish sensitivity: received peaks varied around -11.6 to
-17.3 dBFS and every series was a nearby strong-signal test. A calibrated RF
attenuation or measured-SNR waterfall is required before choosing between
QPSK-1600 and 8PSK-1000, or treating QPSK-2400 as usable beyond the bench.

A preliminary corrected-path receiver-gain sweep compared the two leading
profiles without moving either antenna or changing the transmitter:

| HackRF gain (LNA/VGA) | QPSK-1600 | 8PSK-1000 |
|---|---:|---:|
| 24/8 dB | 10/10 | 10/10 |
| 24/4 dB | 10/10 | 10/10 |
| 24/2 dB | 10/10 | 10/10 |
| 24/0 dB | 4/10 | 0/10 |

At VGA 0, QPSK detected five bursts and decoded four; 8PSK detected none and
its best normalized acquisition correlation was only 0.572. There were no IQ
or decode-queue drops. Thus QPSK-1600 shows more margin at the digitizer floor,
while both profiles remain error-free just 2 dB above it in this small run.
Changing receiver gain scales signal and receiver noise together and changes
quantization, so these rows are not an RF sensitivity or Eb/N0 measurement.
They are useful only as a repeatable current-bench boundary. The production
choice still requires an inline RF attenuator or a calibrated injected-noise
test with channel/noise power recorded for every series.

## Two-radio high-rate 8PSK boundary

With antennas on both handhelds and 30 dB inline attenuation at both TX and
RX, five dense 500-byte packets were sent for each profile. No audio XRUN or
capture/decode queue drop occurred.

| Profile | Detected bursts | Complete packets | TX samples | Payload rate |
|---|---:|---:|---:|---:|
| `turbo` (8PSK-1000 ROBUST/64) | 40/40 | 5/5 | 952320 | 126.0 B/s |
| 8PSK-1200 ROBUST/64 | 40/40 | 0/5 | 793600 | 151.2 B/s offered |
| 8PSK-1600 ROBUST/64 | 40/40 | 0/5 | 595200 | 201.6 B/s offered |
| 8PSK-1000 ROBUST/128 | 20/20 | 2/5 | 804480 | 149.2 B/s offered |
| 8PSK-1000 NORMAL/64 | 40/40 | 0/5 | 768000 | 156.2 B/s offered |

The exact current integer-samples-per-symbol profiles leave 1000 and 1200
baud as adjacent candidates, so 1000 baud is the measured reliable 8PSK
symbol-rate ceiling at this channel point. Larger frames and less redundant
FEC both increased offered airtime efficiency but reduced end-to-end packet
delivery. The production default remains QPSK-1200 until a longer sensitivity
waterfall compares it against `turbo` (8PSK-1000 ROBUST/64).

## Experimental 16PSK result

On 2026-08-12, 16PSK was tested in the same `.54` TX to `.197` RX two-radio
direction after both Orange Pi systems had been restarted. The implementation
uses LiquidDSP PSK16 soft demapping and preserves four LLR values per symbol
through the existing FEC input. Both experimental profiles pass deterministic
audio loopback through a synthetic 300--3000 Hz voice-band filter.

The over-the-air result did not justify promoting either profile:

| Profile | Raw rate | Interface ceiling | OTA result |
|---|---:|---:|---:|
| `experimental_16psk_750` ROBUST/64 | 3000 bit/s | 945 bit/s | 0/5 complete 500-byte packets |
| `experimental_16psk_1000` ROBUST/64 | 4000 bit/s | 1260 bit/s | 0/1 complete 500-byte packets; about 4/8 physical fragments decoded |
| `turbo` 8PSK-1000 ROBUST/64 control | 3000 bit/s | 1016 bit/s | 1/1 complete 500-byte packet, 8/8 fragments |

There were no audio, decode-queue, or ALSA XRUN drops. For 16PSK-1000 the
receiver reported 0.991 best acquisition correlation, 0.953 best training
correlation, and about 16.0 dB average effective SNR, yet missing fragments
caused reassembly sequence rejection. The immediately following `turbo`
control delivered the whole packet at only 13.3 dB reported average effective
SNR. The EVM-derived SNR is therefore not directly comparable across these
constellations: residual phase error is much more damaging to 16PSK.

The profiles remain explicitly experimental for further controlled channel
work. `turbo` remains the fastest demonstrated two-radio profile.

## DireWolf 2400 reference

The deployed DireWolf 1.8.2 configuration uses MFJ-compatible V.26B QPSK at
2400 bit/s (1200 baud, 1800-Hz carrier) and IL2P with maximum payload FEC. A
small KISS benchmark sender now injects deterministic AX.25 UI payloads and
forces KISS full-duplex mode during the bench, because an open handheld
squelch otherwise leaves DireWolf's DCD continuously busy.

The first Orange-Pi-TX to handheld to HackRF-RX trial is a diagnostic baseline,
not yet a goodput result. DireWolf accepted three dense 64-byte packets and the
HackRF recording showed the keyed RF burst. The reference `atest` decoder found
the IL2P sync word and collected candidate headers, but decoded 0/3 packets.
The same decoder decoded 3/3 directly generated IL2P/V.26B packets. Reducing
the Orange Pi DAC from 60 to 54 and 48 (about 7 dB per step) still produced
0/3; both V.26A and V.26B receive modes failed. The DAC was restored to 60.

The same deployed `MODEM 2400` configuration has previously worked between two
handheld radios. Consequently, the zero result above must not be attributed to
the DireWolf transmitter or the handheld audio path. It instead exposes a
problem or incompatibility in the HackRF IQ-to-NFM-to-`atest` measurement path,
or a difference in the receiving radio path. DireWolf's nominal 2400-bit/s rate
still must not be compared directly with measured delivered B/s until packet
delivery and active RF airtime are reproduced on a validated receiver.
