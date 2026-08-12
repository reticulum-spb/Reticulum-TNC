# Offline-site OTA benchmark

`rtnc_ota_benchmark` tests every configured PHY profile between two radio
sites without an IP connection or synchronized clocks. Coordination is sent
over the radio channel itself. This is a test-only protocol and does not add
adaptive profile negotiation to the production modem.

Both installations must use identical YAML files, including profile order,
names, modulation, symbol rate, carrier, FEC mode, payload class, and detector
thresholds. This is an operational requirement: the benchmark intentionally
does not transmit or validate a profile fingerprint and identifies a profile
only by name. Keep a known robust BPSK profile available as the control
profile. Start the receiver first:

```sh
rtnc_ota_benchmark rtnc.yaml rx bpsk_750_robust64 /var/log/rtnc-ota.csv
```

Then start the transmitter with a radio-frame payload size and count. It tests every entry
in `profiles` in YAML order:

```sh
rtnc_ota_benchmark rtnc.yaml tx bpsk_750_robust64 64 20
```

Before each block the transmitter sends the announcement five times using the
control profile while holding PTT. The announcement contains a run ID, block
ID, profile name, frame payload size, count, deterministic payload seed, and guard interval. The
receiver switches to that profile without any network control. Test packets
carry sequence numbers and deterministic bodies. Every test packet is exactly
one independently encoded and CRC-checked physical frame: the benchmark does
not use the link fragmentation layer. The complete test series and three END
frames are sent with PTT held continuously; 100-ms unmodulated gaps delimit
frames. An END message returns the receiver to the control profile. If END is
lost, a timeout also returns it to the control profile.
The timeout is refreshed by every valid DATA frame and expires 15 seconds
after the last one, allowing a receiver that lost END to acquire a later
control announcement instead of remaining stranded on the test profile.

`SIGINT` and `SIGTERM` abort transmission immediately. A dedicated signal
thread first drives PTT inactive and then exits without draining queued ALSA
audio or transmitting the remainder of the series. `SIGKILL` cannot provide
this guarantee and must not be used to stop a transmitting benchmark.

The append-only CSV records packet delivery, duplicates, elapsed time,
payload goodput, physical decode attempts, decoded frames, FEC failures, mean
EVM, mean effective SNR, ALSA capture XRUNs, and whether the block completed
by an `end` frame or `timeout`. A CRC-valid packet whose
deterministic body is wrong is rejected rather than counted.

Run separate 64- and 128-byte passes. Profiles with a smaller payload class
than requested are reported as skipped. Fragmentation and complete Reticulum
packet delivery are intentionally outside this PHY-profile benchmark and must
be measured separately.
