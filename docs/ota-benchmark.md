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
rtnc_ota_benchmark rtnc.yaml rx bpsk_750_1_64 /var/log/rtnc-ota.csv
```

Then start the transmitter with a radio-frame payload size and count. It tests every entry
in `profiles` in YAML order:

```sh
rtnc_ota_benchmark rtnc.yaml tx bpsk_750_1_64 64 20 1
```

The optional final argument is the number of control announcements before
each block (`1` through `20`, default `5`). One repeat is appropriate for a
short, strong test link; use more repeats when sites are separated or the
control channel is unreliable. The announcements use the control profile
while holding PTT. Each announcement contains a run ID, block
ID, profile name, frame payload size, count, deterministic payload seed, and guard interval. The
receiver switches to that profile without any network control. Test packets
carry sequence numbers and deterministic bodies. Every test packet is exactly
one independently encoded and CRC-checked physical frame: the benchmark does
not use the link fragmentation layer. The complete test series is sent with
PTT held continuously; 100-ms unmodulated gaps delimit frames. There is no END
message. A block is finalized when the next valid announcement arrives or
when the 15-second timeout expires. The timeout is refreshed by every valid
DATA frame.

Profile names are transmitted in the fixed header and may contain at most 20
bytes. The supplied configuration uses compact names of the form
`modulation_baud_fec_payload`, where numeric FEC values match `fec_mode`
(`1` is ROBUST and `2` is NORMAL). Transmitter and receiver must use identical
YAML profile definitions.

Control frames use a separate low-cross-correlation control preamble. RX
keeps two ordinary initialized modem pointers: the control modem remains
active throughout the run, while the data modem pointer is replaced after a
valid announcement. Both acquisition paths run continuously, but only a path
that completes a candidate invokes timing recovery, equalization, and FEC.
Thus losing DATA cannot prevent reception of the next announcement.
The default five-second guard gives the control worker time to FEC-decode the
announcement, initialize the announced DATA modem, and warm its streaming
detector before the first DATA preamble arrives.

`SIGINT` and `SIGTERM` abort transmission immediately. A dedicated signal
thread first drives PTT inactive and then exits without draining queued ALSA
audio or transmitting the remainder of the series. `SIGKILL` cannot provide
this guarantee and must not be used to stop a transmitting benchmark.

The append-only CSV records packet delivery, duplicates, elapsed time,
payload goodput, physical decode attempts, decoded frames, FEC failures, mean
EVM, mean effective SNR, ALSA capture XRUNs, and whether the block completed
by `timeout` or `next_announce`. Reception of an announcement
is printed immediately as `attempt announced` with its run ID, block, profile,
expected frame count, and payload size. Every decoded announcement therefore
produces a CSV row even when no DATA frame follows (`received=0`), but the row
is intentionally delayed until that attempt is finalized. If a new
announcement supersedes the current block, the old block is written
with `completion=next_announce`. A CRC-valid packet whose
deterministic body is wrong is rejected rather than counted.

Run separate 64- and 128-byte passes. Profiles with a smaller payload class
than requested are reported as skipped. Fragmentation and complete Reticulum
packet delivery are intentionally outside this PHY-profile benchmark and must
be measured separately.
