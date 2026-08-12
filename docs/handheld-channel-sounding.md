# Handheld channel sounding

## HackRF TX to handheld RX

The first over-the-air response measurement used 446.00625 MHz NFM with the
HackRF RF amplifier disabled and TX VGA 20. The receiver was a handheld with
open squelch connected to the Orange Pi default ALSA capture device at 48 kHz.

`rtnc_hackrf_sounder` sends a 500-ms 1-kHz marker followed by equal-amplitude
tones from 300 through 3300 Hz in 100-Hz steps. Each tone lasts 150 ms, has a
5-ms raised edge, and is separated by 50 ms. Lead and tail silence allow the
FM receiver to settle and provide an in-carrier noise reference.

Sweeps at peak deviations of 1 and 2 kHz had the same normalized response. The
2-kHz sweep was almost exactly 6 dB above the 1-kHz sweep through the useful
band, showing that this range is linear. Selected response values from the
2-kHz run, normalized to 1 kHz, are:

| Audio frequency | Relative response |
|---:|---:|
| 300 Hz | +6.5 dB |
| 400 Hz | +7.4 dB |
| 500 Hz | +5.9 dB |
| 800 Hz | +2.1 dB |
| 1000 Hz | 0.0 dB |
| 1500 Hz | -3.8 dB |
| 2000 Hz | -6.4 dB |
| 2500 Hz | -8.9 dB |
| 2800 Hz | -10.4 dB |
| 2900 Hz | -11.3 dB |
| 3000 Hz | -15.9 dB |
| 3100 Hz | -12.9 dB |
| 3300 Hz | -13.4 dB |

The narrow dip near 3 kHz is repeatable in both deviation runs. Above about
2.8 kHz the margin to the in-carrier noise floor becomes small, so a stronger
or longer measurement is needed for precise stop-band values. The open-squelch
transition can touch full scale, but the tone sequence itself did not clip.

This amplitude response alone is insufficient to predict PSK performance. A
phase-coherent sweep/PN sounding waveform is still needed to recover phase,
group delay, and an impulse response suitable for deterministic replay tests.

## WFM receive comparison

The identical 2-kHz-deviation, TX-VGA-20 sweep was repeated with only the
handheld receiver changed from NFM to WFM. Relative to 1 kHz, WFM measured
+7.7 dB at 300 Hz, -3.8 dB at 1.5 kHz, -6.9 dB at 2 kHz, -9.3 dB at 2.5 kHz,
-10.7 dB at 2.8 kHz, and -13.8 dB at 3.3 kHz. This is very close to the broad
NFM slope, indicating that receiver/audio de-emphasis dominates it.

WFM removed the narrow NFM dip around 3 kHz and produced a smooth monotonic
response. With the same small transmit deviation its absolute 1-kHz audio
level was about 6.8 dB lower. The in-carrier noise floor fell by a similar
amount, so useful SNR did not materially improve. WFM therefore provides no
clear benefit for the present approximately 300--2800-Hz modem band, although
the comparison helps separate the NFM channel-filter notch from the common
audio response.

## Realtime transmit-level calibration

`rtnc_hackrf_nfm_meter` receives directly through libhackrf. Its callback only
copies fixed IQ blocks into an SPSC ring; LiquidDSP channel resampling, NFM
discrimination, 48-to-8-kHz resampling, and the 1024-point Hann FFT run outside
the USB callback. It automatically finds the strongest 300--3300-Hz tone and
updates approximately twice per second with frequency, fundamental peak
deviation, H2/H3/H4 in dBc, aggregate THD, spectral SNR, IQ level, and queue
drops. Windows below 6-dB SNR are displayed as `NO TONE`.

Example receiver invocation:

```
build/rtnc_hackrf_nfm_meter 446006250 24 16 -70 0
```

The Orange Pi sounder can hold a calibration tone while the physical level
control is adjusted:

```
/tmp/rtnc_alsa_sounder /tmp/rtnc.yaml 12000 1000 30
```

The initial implementation used the entire approximately 48-kHz discriminator
bandwidth and reported approximately 566-Hz fundamental peak deviation and
49--54% THD. That THD result is invalid: excess out-of-channel FM noise biased
the harmonic estimate. The meter now applies a 129-tap approximately 6.5-kHz
complex channel filter before the discriminator and a 30-Hz DC blocker before
the 8-kHz FFT.

After physical attenuation, the corrected meter found only about 150--165-Hz
fundamental peak deviation. The tone remains audible on another handheld, but
its FFT SNR is -6 to -8 dB and individual harmonic estimates vary too much for
a valid THD result. Physical level should be raised until roughly 800--1200-Hz
fundamental peak deviation and positive spectral SNR are obtained; only then
should the H2/H3/THD minimum be used to select the operating level.

The Orange Pi source was then raised to DAC 63/63, Line Out 31/31, and PCM peak
16000. The handheld transmit path reached a repeatable ceiling of only
566--568-Hz fundamental peak deviation. H2 remained low at approximately
-35 to -39 dBc, but H3 dominated at -5.4 to -6.4 dBc, yielding 48--54% THD at
17--18-dB spectral SNR. Raising source level no longer increased fundamental
deviation. This is a transmitter microphone-path limiter, not a shortage of
Orange Pi output. The physical attenuator should be set immediately below this
limiting knee; additional drive only creates odd-order distortion.

With the physical attenuator fixed near that knee, a full 300--3300-Hz TX
sweep showed that 1 kHz is not the worst-case deviation point. Calibrating the
stepped-tone amplitudes to the realtime meter's approximately 567-Hz result at
1 kHz gives these approximate fundamental peak deviations:

| Audio frequency | TX peak deviation |
|---:|---:|
| 300 Hz | 170 Hz |
| 500 Hz | 250 Hz |
| 800 Hz | 500 Hz |
| 1000 Hz | 570 Hz |
| 1500 Hz | 730 Hz |
| 2000 Hz | 940 Hz |
| 2300 Hz | 1060 Hz |
| 2400 Hz | 1110 Hz |
| 2500 Hz | 1070 Hz |
| 2800 Hz | 900 Hz |
| 3000 Hz | 740 Hz |
| 3300 Hz | 640 Hz |

The transmitter therefore has strong pre-emphasis up to a broad maximum near
2.3--2.4 kHz, followed by its upper audio cutoff. The 2.4-kHz fundamental is
about 5.8 dB stronger than at 1 kHz. Modem level must be constrained by this
maximum rather than by a 1-kHz calibration tone.

The first platform TX correction used fixed one-pole de-emphasis and gain.
That historical correction belonged to the particular radio/audio interface,
not to PHY rate negotiation. An initial 190-us correction with gain 1.7 was too
strongly driven: comparison against the unfiltered recording showed only
1.05 dB attenuation at 2.4 kHz because the handheld limiter still dominated.
With the same 190-us correction at unity gain, the fundamental response over
the central modem band measured as follows, normalized to 1.6 kHz:

| Audio frequency | Corrected response |
|---:|---:|
| 1.1 kHz | -1.4 dB |
| 1.2 kHz | -0.9 dB |
| 1.4 kHz | +0.1 dB |
| 1.6 kHz | 0.0 dB |
| 1.8 kHz | +0.3 dB |

This one-pole implementation has since been removed rather than retained as a
compatibility path. Current configurations use calibrator-generated symmetric
`response_eq_taps` plus `filter_gain`, which can correct non-monotonic radio
responses while preserving zero phase.
| 2.0 kHz | +0.4 dB |
| 2.2 kHz | -1.0 dB |
| 2.4 kHz | -1.0 dB |

The 1.1--2.2-kHz central PSK region is therefore within roughly +/-0.8 dB of
its mid-band level (the 1.1-kHz endpoint is about 1.4 dB low). The radio still
rolls off sharply outside this region. The compensated sweep had no ALSA
underruns and the captured discriminator PCM did not clip. Packet tests must
now determine whether this improves acquisition and FER; the correction must
not be judged from amplitude response alone.
