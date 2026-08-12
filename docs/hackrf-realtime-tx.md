# Realtime HackRF transmit backend

`rtnc_hackrf_tx_audio()` accepts one normalized 48-kHz mono waveform and
transmits it directly through libhackrf. The asynchronous callback performs
integer interpolation to the configured IQ rate and NFM phase accumulation;
it does not allocate memory or read files. The caller-owned audio remains valid
for the blocking call.

Default bench settings are 446.00625 MHz, 2.4 Msps, ±2.10-kHz deviation,
TX VGA +20 dB, 100-ms lead/tail, and signed-IQ amplitude 100. The external RF
amplifier is always forced off. The IQ rate must be an integer multiple of the
audio rate.

The backend uses `hackrf_enable_tx_flush()` and returns success only after the
library confirms that the final queued USB buffer was transmitted. Merely
waiting until the callback generated the final buffer truncated the end of the
frame and was rejected during OTA testing.

Transmit one deterministic robust frame with:

```sh
./rtnc_hackrf_tx 446006250 20 2100 64
```

The successful HackRF-to-handheld-to-Orange-Pi run generated 1,856,000 IQ
samples (0.773 s including lead/tail) and decoded all 64 bytes with FEC/CRC:

```text
acquisition=0.980 training=0.962 evm=0.427
best_stream_correlation=0.977
audio_drops=0 decode_drops=0 capture_xruns=0
```

One preceding full transmission synchronized but failed FEC, so this confirms
backend correctness rather than a 100% FER claim. Longer bidirectional runs
remain part of CPU/FER/goodput characterization.

A subsequent sequence-numbered smoke series used five independent robust-64
frames with 750-ms inter-frame gaps. All five libhackrf transfers flushed, and
the Orange Pi received sequences 0 through 4 in order. The result was 5/5 CRC
frames, five detector bursts, five decoder attempts, zero audio/decode drops,
zero capture XRUNs, and EVM 0.403--0.508. The gap is a bench scheduler chosen to
avoid overlapping the current 57,000-sample candidate window; it is not a MAC
timing decision.
