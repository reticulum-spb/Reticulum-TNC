# HackRF-to-handheld NFM baseline

Date: 2026-08-10. HackRF One transmitted at 446.006250 MHz with TX VGA 0 dB,
RF amplifier disabled, and 2.4 Msps signed 8-bit IQ. A handheld in NFM mode
fed the Orange Pi H3 codec at 48 kHz mono S16. Squelch was open; ALSA `Mic1`
and `ADC Gain` were both set to 0 after the initial recording clipped open
channel noise.

The payload was deterministic ROBUST/64 (`payload[x] = 37*x + 0x29`) and each
3.47-second transmission contained a 1 kHz tone followed by three frames.

| Peak modem deviation | Acquisition range | Training range | EVM range | Result |
|---:|---:|---:|---:|---|
| 1.26 kHz | 0.718--0.742 | 0.597--0.745 | 0.638 measured | no FEC convergence |
| 1.68 kHz | 0.750--0.941 | 0.832--0.904 | 0.579--0.608 | no FEC convergence |
| 2.10 kHz | 0.906--0.925 | 0.846--0.860 | 0.548--0.588 | no FEC convergence |

An experimental acquisition threshold of 0.70 did not falsely detect any of
nine 0.9-second open-squelch noise windows; their maximum correlations ranged
from 0.440 to 0.588. Lowering the threshold is not sufficient: detected frames
still fail FEC with high EVM.

The next experiment must add transmitter pre-emphasis before direct FM. The
current HackRF waveform uses flat audio, while the handheld receiver applies
de-emphasis that would normally be cancelled by a radio transmitter's
pre-emphasis. Keep peak RF deviation constant when comparing the two cases.

That comparison was subsequently run at equal ±2.10 kHz peak modem deviation.
First-order pre-emphasis with 300, 500, and 750 microsecond time constants did
not recover a frame. The best EVM changed only from 0.548 (flat) to 0.537
(300 microseconds); a representative flat frame still had 152/1152 hard-bit
errors after the failed equalizer retry, versus 150/1152 for the best
300-microsecond frame. Pre-emphasis is therefore not the main limitation of
this capture path. The next receiver experiment should use more of the 96
known training symbols for equalizer supervision instead of only the final 32.

Using earlier training symbols was rejected: the early 2-sps timing-recovery
stream is still transient and made RLS diverge, including on the synthetic
echo20 regression. A safer third-stage retry keeps the proven 32-symbol frozen
RLS pass and, only after that fails, repeats it with confidence-gated
decision-directed updates. This recovered 2/3 flat-FM ±2.10 kHz frames and 1/3
750-microsecond pre-emphasis frames with matching payload and CRC. Successful
flat frames had EVM 0.420/0.433 and 57/67 hard-bit errors before TC128 decoding.

`tests/recordings/hackrf_radio_dev2100_frame.wav` is an unnormalized 0.9-second
real-channel regression excerpt (SHA-256
`2453d85ffb060eee84e2931efc14c56df2cd3417a460b3c75b4a4a9adcb524c5`).

Raw recordings are under `recordings/hackrf_to_radio/2026-08-10_session01/`.
Their SHA-256 values are printed during capture and should be moved into a
manifest before selecting additional regression excerpts.

## HackRF TX VGA sweep

Flat FM at ±2.10 kHz peak modem deviation was repeated while changing only
HackRF TX VGA; the RF amplifier remained disabled. The result identifies RF
SNR, rather than deviation or equalizer capability, as the dominant limiter:

| TX VGA | CRC success | Receiver behavior |
|---:|---:|---|
| 0 dB | 2/3 | decision-directed EQ required; EVM 0.420--0.433 on successes |
| 3 dB | 1/3 | one EQ-assisted success; one acquisition miss |
| 4 dB | 2/3 | one EQ-assisted and one fast-path success; one acquisition miss |
| 5 dB | 3/3 | all fast path; EVM 0.278--0.330; 0/2/7 hard errors |

In the initial three-frame sample at 5 dB, acquisition was 0.949--0.971,
training correlation 0.959--0.973, and TC128 converged in nine iterations.

The three-frame result was optimistic. A subsequent continuous 20-frame sweep
at the same deviation produced the following FER waterfall:

| TX VGA | CRC success | FER |
|---:|---:|---:|
| 5 dB | 6/20 | 0.70 |
| 10 dB | 17/20 | 0.15 |
| 15 dB | 19/20 | 0.05 |
| 20 dB | 20/20 | 0.00 observed |

At 20 dB all frames used the ordinary 1-sps fast path. Acquisition was
0.977--0.981, training correlation 0.973--0.985, and EVM 0.192--0.327. Eighteen
frames had zero hard errors and the other two had two each; TC128 converged in
nine iterations for every frame. With the deliberately inserted 250-ms gap,
the observed delivered goodput was 77.7 payload bytes/s. The no-gap waveform
ceiling remains 111.6 bytes/s for this ROBUST/64 profile.

Use TX VGA 20 dB as the stable bench reference, not as a calibrated RF power.
HackRF VGA values describe the device setting only; path loss, antenna geometry,
receiver RSSI, and absolute conducted power were not measured.
