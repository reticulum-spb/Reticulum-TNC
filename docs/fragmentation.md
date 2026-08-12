# Native packet fragmentation

The packet layer uses one byte inside each CRC/FEC-protected radio payload.
The physical frame already carries the exact payload length, so the fragment
header does not repeat the final-fragment length:

```text
bit 7      bits 6..0
0          start frame: number of fragments still to follow
1          continuation: number of fragments still to follow
```

A value of zero in a start frame is a complete single-frame packet. A
continuation counter must decrease by exactly one and the zero continuation is
the final fragment. A new start always resets an incomplete assembly. This is
the same countdown principle as the earlier FreeDV-TNC header, adjusted for
the native variable-length physical frame.

The seven-bit counter permits 128 independently synchronized fragments. The
fragment data capacities and maximum configured MTUs are therefore:

| Radio payload class | Data per fragment | Maximum MTU |
|---:|---:|---:|
| 64 bytes | 63 bytes | 8064 bytes |
| 128 bytes | 127 bytes | 16256 bytes |

`link.mtu` is configurable from 500 through 16256 bytes. Initialization also
rejects an MTU that cannot fit the selected radio payload class. Reassembly
uses a caller-owned buffer allocated before realtime processing and enforces a
configured timeout, exact countdown, physical payload bounds, and MTU bounds.

The one-byte format intentionally serializes packets on one radio link rather
than spending header bits on a packet ID. Parallel decoder workers may finish
radio frames out of order, so their terminal results first enter a bounded
completion coordinator keyed by detection `job_id`. It emits successes and
failures strictly in detection order. A fast-decoder failure forwarded to the
low-priority equalizer is not terminal until that retry completes. Queue drops
are submitted as terminal failures so a missing job cannot stall the reorder
window. A generation number invalidates stale results after a quiesced reset
or profile change.

## Current OTA checks

Orange Pi/handheld TX to HackRF RX at QPSK-1600 produced:

| Packet series | Radio frames | Result | Queue drops |
|---|---:|---:|---:|
| one 500-byte packet | 8 | 1/1 packet, 8/8 frames | 0 |
| one 1000-byte packet | 16 | 1/1 packet, 16/16 frames | 0 |
| two adjacent 500-byte packets | 16 | 2/2 packets, 16/16 frames | 0 |
| five adjacent 500-byte packets | 40 | 5/5 packets, 40/40 frames | 0 |
| drop fragment 3 of packet 0, then send packet 1 | 15 | packet 0 rejected, packet 1 delivered | 0 |
| five 500-byte packets, class 128 | 20 | 5/5 packets, 20/20 frames | 0 |

All transmissions used zero added inter-fragment gap and one PTT session. The
maximum HackRF decode-queue depth was one. The 500-byte waveform occupied
3.44 seconds (about 137 delivered B/s including 100-ms PTT lead and tail); the
1000-byte waveform occupied 6.88 seconds (about 141 delivered B/s including
lead and tail). These are strong-signal measurements, not a FER waterfall.

The explicit loss test omitted fragment index 3 from the first of two packets.
The next countdown was rejected as an invalid sequence, all remaining orphan
continuations were rejected, and the next start frame immediately recovered;
only packet number 1 was delivered. The reorder window did not stall.

For the same QPSK-1600 PHY and 500-byte packet, class 128 needs four physical
frames and 144000 samples versus eight frames and 165120 samples for class 64.
Including one 100-ms PTT lead and tail this is approximately 156 versus
137 delivered B/s for one packet, a 14-percent strong-signal improvement.
Five-packet series delivered 5/5 for both classes at HackRF LNA 24/VGA 12 and
again at VGA 2. Class 128 showed less apparent margin in symbol metrics, with
EVM peaks around 0.45 versus roughly 0.37 for class 64. Receiver-gain changes
are not calibrated SNR, so a controlled attenuation waterfall is still needed
to determine whether the lower frame count offsets the longer-frame FER.
