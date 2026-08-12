# Bidirectional packet runtime and KISS TCP

## Data path

The runtime preserves complete packet boundaries from KISS to the radio and
back:

```text
KISS TCP -> TX packet queue -> fragmentation -> modem TX -> ALSA/PTT
ALSA -> detector -> two decode queues -> ordered completion -> reassembly
     -> RX packet queue -> KISS TCP
```

TX and RX queues are fixed-storage SPSC rings with configurable active
capacities up to eight packets. The maximum storage size is the protocol's
16256-byte link limit; no allocation occurs in the realtime DSP or packet
worker paths.

All fragments of one packet are transmitted without an artificial gap and
under one PTT interval. The TX worker is separate from the capture/streaming
thread, so ALSA capture continues to be drained during a long transmission.
Detected candidates produced by the local transmitter are terminated in the
completion coordinator but are not decoded or passed to reassembly.

## Half-duplex behavior

The state sequence is:

```text
RX_IDLE -> TX_WAIT_BUSY -> TX_KEYING -> TX_ACTIVE -> TX_TAIL
        -> RX_GUARD -> RX_IDLE
```

The current minimal DCD regards a confirmed correlation-triggered modem burst
as busy. Fixed energy ratios were deliberately not retained: with the handheld
squelch open, both the 1.3 close threshold and 4.0 open threshold caused a
permanent false busy indication. Generic carrier/voice DCD needs a separate
adaptive measurement before it can be enabled safely.

PTT is released on successful completion, ALSA send/drain failure, SIGINT,
SIGTERM, and runtime destruction. A packet that remains blocked by DCD for
`channel_busy_timeout_ms` is removed with an explicit timeout statistic.

## KISS subset

The server binds `127.0.0.1:runtime.kiss_tcp_port`, accepts one TCP client and
implements port-zero KISS data frames. `FEND`, `FESC`, `TFEND`, and `TFESC` are
fully streamed and escaped; TCP read boundaries have no significance. Nonzero
ports and unsupported commands are consumed but never forwarded to the radio.

The Reticulum-required timing/MAC commands are applied at runtime:

| KISS command | Value | Runtime effect |
|---|---:|---|
| `CMD_TXDELAY` (1) | 10-ms units | PTT-on to first audio sample |
| `CMD_P` (2) | 0--255 | p-persistence probability `(P+1)/256` |
| `CMD_SLOTTIME` (3) | 10-ms units | delay before another persistence trial |
| `CMD_TXTAIL` (4) | 10-ms units | playback drain to PTT-off delay |

Values are stored atomically because the KISS parser and TX worker run in
different threads. YAML `tx.lead_ms` and `tx.tail_ms` initialize TXDELAY and
TXTAIL; persistence starts at 255 and slot time at 100 ms. A KISS command
overrides the corresponding value until process restart. Modulation, FEC,
payload class, filter response and MTU remain fixed in YAML.

DireWolf `kissutil` command-file lines `d 20`, `p 192`, `s 10`, and `t 15`
were accepted by the ARM runtime as KISS commands 1--4 without keying PTT.

## First OTA validation

On 2026-08-11, `kissutil` on `.54` submitted:

```text
N0CALL>APRS:RTNC KISS TCP TEST 0001
```

The KISS data payload was 39 bytes. `rtnc_modem` transmitted it as one
`turbo` frame; `.197` detected and decoded one frame, immediately reassembled
the packet and returned it to its local `kissutil`. The received monitor frame
matched the input. RX diagnostics were acquisition 0.984, training 0.957,
EVM 0.171, effective SNR 15.33 dB, and nine FEC iterations. Audio, decode and
capture drop/XRUN counters were all zero.
