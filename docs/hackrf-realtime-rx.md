# Realtime HackRF receive path

`rtnc_hackrf_rx` is the first realtime RF frontend for the existing audio
modem. The libhackrf callback performs no DSP: it copies signed 8-bit IQ into a
fixed four-block SPSC queue. The consumer mixes the selected channel to
baseband, uses a LiquidDSP multistage resampler from 2 MHz to 48 kHz,
frequency-demodulates NFM, and passes a sliding audio window to the unchanged
robust-64 modem decoder.

The successful Orange-Pi/radio-to-HackRF bench command was:

```sh
./rtnc_hackrf_rx 446016250 24 12 -10000
```

The HackRF is deliberately tuned 10 kHz above the 446.00625-MHz radio channel,
then digitally shifted by -10 kHz. This keeps the wanted channel away from the
HackRF DC spur. The RF amplifier remains disabled. LNA 24 dB and VGA 12 dB
provided enough SNR without the clipping seen at LNA 32/VGA 24.

One deterministic robust frame produced:

```text
decoded bytes=64 acquisition=0.981 training=0.954 cfo=0.0 evm=0.275 attempts=2 drops=0
```

The decoded payload exactly matched all 64 transmitted bytes and passed FEC
and CRC. This utility is diagnostic rather than the final service: its sliding
window currently invokes the offline decoder synchronously. The production
path still needs bounded ordinary-DSP and low-priority equalizer-retry workers
so a slow decode cannot hold up IQ or ALSA capture.

The next iteration separates NFM production and modem decoding with a fixed
two-frame SPSC queue and a dedicated decoder pthread. This proves that the
HackRF callback and NFM worker need not call the expensive decoder. It also
exposed why periodic blind snapshots were not a viable scanner: no-frame scans
could take long enough that the bounded queue contained stale noise windows
when a new burst arrived.

The producer now measures complex channel power before FM demodulation. An
adaptive noise estimate and faster signal estimate trigger a candidate that
contains 100 ms of pre-trigger audio, the complete carrier burst, and a 50-ms
release interval. A 100-ms cooldown prevents the same tail from retriggering.
Only this candidate enters the decoder queue; noise windows never do.

The repeated OTA check produced exactly one burst and one decoder attempt:

```text
decoded sequence=0 bytes=64 acquisition=0.982 training=0.983 cfo=0.0 evm=0.168 attempts=1
finished bursts=1 attempts=1 iq_drops=0 decode_drops=0
```

All 64 bytes again matched and passed FEC/CRC. The trigger is now the reusable
pure-C `rtnc_burst_detector_t` module with fixed buffers and configurable EMA,
threshold, warmup, pre-trigger, release, and cooldown values. Its deterministic
test covers noise warmup, one burst, release, pre-trigger ordering, and cooldown
suppression. An OTA check after extraction again decoded 64/64 bytes on the
first job with zero IQ/decode drops (acquisition/training 0.983/0.983).

HackRF RX subsequently moved from RF-energy frame segmentation to the reusable
normalized streaming audio acquisition correlator. RF power remains telemetry.
This allows multiple frames under one continuous carrier: five adjacent
Orange-Pi transmissions with zero added gap delivered sequences 0--4 at 5/5
CRC and zero queue drops.

The acquisition trigger is additionally gated by adaptive RF energy.  The
noise estimate freezes while the channel is busy, so a lower normalized
correlation threshold can tolerate OTA multipath without producing candidates
from normalized correlations in receiver noise.  A deterministic regression
feeds five adjacent frames after receiver warmup and requires five fixed-size
candidates.

An August 2026 OTA regression with the currently connected handheld confirmed
8 candidates and 8 decoder attempts from one continuously keyed, zero-gap
500-byte packet (8 radio fragments), with no IQ or decode-queue drops.  The
remaining failure was PHY quality: QPSK-1200 decoded 2/8 fragments, QPSK-800
1/8, and QPSK-1600 0/8, so no complete packet was reassembled.  The full and
20-percent inverse-response TX FIRs both reduced training quality; the retained
baseline is the identity FIR at `filter_gain: 0.25`.

The diagnostic receiver now averages IQ power during its first 750 ms, before
the controlled transmitter is keyed, and reports `noise_iq_dbfs` together with
the peak block power and `carrier_over_noise_db`. A validation run at LNA 24 /
VGA 2 reported -28.69 dBFS background, -21.06 dBFS peak, and a 7.63-dB power
rise while decoding QPSK-1600 at 3/3. This is wideband HackRF input telemetry,
not demodulated SNR or Eb/N0; the initial window must remain signal-free.

An optional final `CAPTURE_PREFIX` argument writes every fixed-size decoder
candidate as `PREFIX_NNN.wav` (mono S16, 48 kHz) and `PREFIX_NNN.txt`.  The
sidecar records acquisition/training scores, timing, CFO, phase, EVM, FEC
iterations and complex equalizer taps.  For example:

```sh
./rtnc_hackrf_rx 446006250 16 14 0 1 qpsk 1200 1650 0.25 \
  500 64 1 0.80 0.65 recordings/ota/qpsk1200
./rtnc_decode_wav recordings/ota/qpsk1200_000.wav 0.80 0.65
```

The first replay set exposed an amplitude-scaling problem in the RLS fallback:
17 taps grew into the hundreds because handheld deviation directly determined
the equalizer input scale.  Training-RMS normalization and reducing the model
to 9 taps changed the same recorded sets from 3/8 to 24/24 CRC-valid frames.
The shorter equalizer also passed the synthetic echo/distortion regressions.
Fresh OTA runs remain variable: one run reached 7/8, while a later fade yielded
8 candidates but 0/8 FEC.  The recordings preserve both channel states for
future deterministic comparisons.

A charged replacement handheld was then tested without changing the Orange Pi
or audio interface.  With HackRF LNA/VGA 32/20 it delivered 39/40 radio frames
and 4/5 complete 500-byte packets at gain 0.25.  Gain 0.30 improved the short
comparison to 24/24 frames and 3/3 packets; gain 0.35 fell to 23/24 and 2/3.
The retained replacement-radio setting is therefore `filter_gain: 0.30`, with
an identity response FIR.  Its typical EVM was 0.20--0.26, materially better
than the previous handheld at comparable RF C/N.
