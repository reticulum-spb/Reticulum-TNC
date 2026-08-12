# Bidirectional sequence smoke test

Both realtime directions now accept a frame count and place a big-endian
32-bit sequence number in the first four payload bytes. Independent frames use
a 750-ms bench gap so the current bounded candidate windows do not overlap.
This gap is measurement scaffolding, not a proposed MAC protocol.

## HackRF TX to Orange Pi RX

Five robust-64 frames were generated and NFM-modulated by the callback-driven
HackRF backend. The handheld received RF and delivered audio to the Orange Pi
ALSA pipeline. Results:

- transmitted and USB-flushed: 5/5;
- CRC-valid sequences: 0, 1, 2, 3, 4 (5/5);
- detector bursts / decoder attempts: 5/5;
- EVM: 0.403--0.508;
- audio drops, decode drops, capture XRUNs: zero.

## Orange Pi TX to HackRF RX

The Orange Pi generated each modem waveform, keyed the handheld through GPIO,
played it through ALSA, and released PTT. The HackRF frontend received NFM in
realtime with the 10-kHz DC-spur offset. Results:

- ALSA/PTT transmissions: 5/5; playback XRUNs: zero; PTT off on return;
- RF detector bursts / decoder attempts: 5/5;
- CRC-valid sequences: 0, 1, 2, 4 (4/5, FER 0.20);
- IQ drops and decode-queue drops: zero;
- successful EVM: 0.322--0.756.

Sequence 3 synchronized but failed FEC/CRC. The two directions therefore have
different measured reliability even at fixed bench settings. Longer trials
must report each direction separately and include confidence intervals,
delivered bytes/s, CPU, queue occupancy, and latency.

## Dense fragment-style transmission

The conservative 750-ms gap was then removed in the HackRF-to-Orange direction.
Five complete robust-64 waveforms were concatenated into one libhackrf stream;
there was no added audio gap, device reopen, or RF lead/tail between frames.
The streaming correlator derives its candidate length from the selected PHY
frame duration and re-arms with zero correlation-mode cooldown, so every next
preamble is eligible even while the previous frame is being decoded.

All sequences 0--4 passed CRC on their first decoder attempt. The Orange Pi
reported five bursts, zero audio/decode drops, zero XRUNs, and EVM
0.410--0.470. The single RF stream carried 320 payload bytes in 3.067 seconds,
or approximately 104.3 payload bytes/s including only the outer 100-ms lead
and tail. This is close to the current calculated 111.6-byte/s no-gap ceiling.

The reverse direction now uses the reusable streaming acquisition correlator
after realtime NFM demodulation instead of RF-energy burst segmentation. The
Orange Pi concatenated five PCM waveforms, keyed PTT once, used no added audio
gap, and released PTT after draining the complete series. HackRF RX delivered
ordered sequences 0--4 with 5/5 CRC, one decoder attempt per preamble, zero IQ
or decode drops, and EVM 0.252--0.336. Its on-air duration is likewise about
3.067 seconds, giving approximately 104.3 payload bytes/s.

Thus adjacent independently synchronized PHY frames work in both directions
without artificial inter-frame silence. Fragmentation may keep the transmitter
keyed across a packet's fragment train; only outer lead/tail timing is needed.
